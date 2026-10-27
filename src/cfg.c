/*
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * LPS node firmware.
 *
 * Copyright 2016, Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stm32f0xx_hal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "cfg.h"
#include "eeprom.h"
#include "uwb.h"
#include "uwb_control.h"
#include "task.h"

typedef struct {
  uint8_t *data;
} TlvArea;

// Temporary fix, just buffer the first 100 bytes
#define NUMBER_OF_BYTES_READ 100
#define MAGIC ((uint8_t) 0xBC)

#define SIZE_HEADER 5
#define SIZE_TAIL 1

static uint8_t buffer[NUMBER_OF_BYTES_READ];

static TlvArea tlv;

typedef struct {
  uint8_t magic;
  uint8_t majorVersion;
  uint8_t minorVersion;
  uint16_t tlvSize;
} __attribute__((packed)) CfgHeader;

static CfgHeader * cfgHeader = (CfgHeader*) buffer;
static bool mulStorageUncertain, pendingRestart;
static mulStoredPlan_t cachedPlan;
static mulPlanStoreResult_t cachedPlanStatus=mulPlanAbsent;
static void planBootLoad(void);
static volatile bool writing;
static bool mulStore(ConfigField field,const uint8_t *data,uint8_t length);
bool cfgPendingRestart(void) {return pendingRestart||mulStorageUncertain||writing;}
bool cfgStorageReady(void) {return !mulStorageUncertain&&!writing;}
bool cfgRfEnabled(bool *enabled) {
 uint8_t size,value=1;
 if(mulStorageUncertain||writing)return false;
 if(cfgFieldSize(cfgRfEnable,&size)&&(size!=1||!cfgReadU8(cfgRfEnable,&value)||value>1))return false;
 *enabled=value!=0;return true;
}

static int tlvFindType(TlvArea *tlv, ConfigField type) {
  uint16_t pos = 0;

  while (pos < cfgHeader->tlvSize) {
    if (tlv->data[pos] == type) {
      return pos;
    } else {
      pos += tlv->data[pos+1]+2;
    }
  }
  return -1;
}

bool deckTlvHasElement(TlvArea *tlv, ConfigField type) {
  return tlvFindType(tlv, type) >= 0;
}

/* Reads all the data from the EEPROM */
static bool readData(void) {
  //int i;
  if (eepromRead(0, buffer, NUMBER_OF_BYTES_READ)) {
    /*printf("EEPROM: ");
    for (i = 0; i <NUMBER_OF_BYTES_READ; i++)
      printf("0x%02X ", buffer[i]);
    printf("\r\n");*/
    return true;
  } else {
    printf("CONFIG\t: Failed to read data from EEPROM!\r\n");
    return false;
  }
}

static void write_crc(void) {
  int i;
  uint8_t checksum = 0;

  for (i = 0; i < SIZE_HEADER + cfgHeader->tlvSize ; i++) {
    checksum += buffer[i];
  }
  buffer[SIZE_HEADER + cfgHeader->tlvSize] = checksum;
}

static bool check_crc(void) {
  int total_len = SIZE_HEADER + SIZE_TAIL + cfgHeader->tlvSize;
  uint8_t ref_checksum = buffer[total_len - SIZE_TAIL];
  int i;
  uint8_t checksum = 0;

  for (i = 0; i < SIZE_HEADER + cfgHeader->tlvSize; i++) {
    checksum += buffer[i];
  }
  if (checksum == ref_checksum) {
    return true;
  } else {
    printf("CONFIG\t: EEPROM configuration checksum not correct (0x%02X vs 0x%02X)!\r\n", ref_checksum, checksum);
    return false;
  }
}

static bool check_magic(void) {
  return (cfgHeader->magic == MAGIC);
}

static bool check_content(void) {
  if (check_magic() && cfgHeader->tlvSize < (NUMBER_OF_BYTES_READ - SIZE_HEADER -SIZE_TAIL)) {
    uint16_t pos=0;
    while(pos<cfgHeader->tlvSize) {
      uint16_t remaining=cfgHeader->tlvSize-pos;
      if(remaining<2||tlv.data[pos+1]>remaining-2)return false;
      pos+=2+tlv.data[pos+1];
    }
    return check_crc();
  }
  printf("CONFIG\t: EEPROM magic not found!\r\n");
  return false;
}

static bool write_defaults(void) {
#if !MUL_ONLY
  uint8_t default_anchor_list[] = {1, 2, 3, 4, 5, 6};
#endif

  buffer[0] = MAGIC;
  buffer[1] = 1; // Major version
  buffer[2] = 0; // Minor version
  buffer[3] = 0; // Length of TLV
  buffer[4] = 0; // Length of TLV
  buffer[5] = buffer[0] + buffer[1];
  // Write the default address
#if MUL_ONLY
  if(!cfgWriteU8(cfgAddress,0)||!cfgWriteU8(cfgMode,255)||!cfgWriteU32(cfgMulSlotMs,10))return false;
#else
  if(!cfgWriteU8(cfgAddress, 0)||!cfgWriteU8(cfgMode, MODE_ANCHOR)||!cfgWriteU32(cfgMulSlotMs,10)||
     !cfgWriteU8list(cfgAnchorlist, default_anchor_list, sizeof(default_anchor_list)))return false;
#endif
  write_crc();
  if (!eepromWrite(0, buffer, 7))
    return false;
  HAL_Delay(10);
  if (readData()) {
    if (check_content()) {
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

static void idleConfiguration(void) {
  memset(buffer,0,sizeof(buffer));buffer[0]=MAGIC;buffer[1]=1;buffer[3]=6;
  buffer[5]=cfgAddress;buffer[6]=1;buffer[7]=0;
  buffer[8]=cfgMode;buffer[9]=1;buffer[10]=255;write_crc();
}

void cfgInit(void) {
  pendingRestart=false;cachedPlanStatus=mulPlanIoError;
  mulStorageUncertain=true;
  if (readData()) {
    cfgHeader = (CfgHeader*) buffer;
    tlv.data = &buffer[SIZE_HEADER];
    if (check_content()) {
      mulStorageUncertain=false;
      printf("CONFIG\t: EEPROM configuration read and verified\r\n");
    } else {
      if(buffer[0]==MAGIC) {
        /* A torn legacy page write must not silently erase unrelated settings.
         * Preserve EEPROM for recovery and select the existing idle algorithm. */
        idleConfiguration();
        printf("CONFIG: invalid EEPROM retained; radio idle; explicit recovery required\r\n");
        return;
      }
      printf("CONFIG\t: Writing default EEPROM configuration\r\n");
      mulStorageUncertain=false; // Explicit uninitialized/invalid-magic factory path.
      if (write_defaults()) {
        mulStorageUncertain=false;pendingRestart=false;
      } else {
        mulStorageUncertain=true;
        idleConfiguration();
        printf("CONFIG\t: Error when writing default configuration\r\n");
      }
    }
  } else {
    tlv.data=&buffer[SIZE_HEADER];idleConfiguration();
    printf("CONFIG: EEPROM unreadable; radio idle; data retained\r\n");
  }
  if(!mulStorageUncertain)planBootLoad();
}

bool cfgReset(void) {
  if(uwbControlRequested()||uwbControlRunning())return false;
  taskENTER_CRITICAL();bool busy=writing;if(!busy)writing=true;taskEXIT_CRITICAL();
  if(busy)return false;
  const uint8_t zero[4]={0};uint8_t check[4];
  // Invalidate the legacy boot image first. A cut while clearing either plan
  // commit then boots idle defaults, never an older selected radio owner.
  bool ok=eepromWrite(0,(void *)zero,1)&&eepromRead(0,check,1)&&check[0]==0;
  HAL_Delay(10);
  for(unsigned slot=0;slot<2&&ok;slot++) {
    unsigned at=MUL_PLAN_STORE_BASE+slot*MUL_PLAN_STORE_STRIDE+92;
    ok=eepromWrite((int)at,(void *)zero,4)&&eepromRead((int)at,check,4)&&!memcmp(zero,check,4);
  }
  if(ok){cachedPlanStatus=mulPlanAbsent;pendingRestart=true;}
  else mulStorageUncertain=true;
  writing=false;return ok;
}

bool cfgReadU8(ConfigField field, uint8_t * value) {
  int pos = tlvFindType(&tlv, field);

  if (pos > -1 && tlv.data[pos+1]==1) {
    *value = tlv.data[pos+2];
  }

  return (pos > -1 && tlv.data[pos+1]==1);
}

bool cfgWriteU8(ConfigField field, uint8_t value) {
#if MUL_ONLY
 if(field==cfgMode&&value!=MODE_MUL_ALR&&value!=MODE_MUL_RLR&&value!=255)return false;
#endif
 return mulStore(field,&value,1);
}

bool cfgReadU32(ConfigField field, uint32_t * value) {
  int pos = tlvFindType(&tlv, field);

  if (pos > -1 && tlv.data[pos+1]==sizeof(uint32_t)) {
    memcpy(value, &tlv.data[pos+2], sizeof(uint32_t));
  }

  return (pos > -1 && tlv.data[pos+1]==sizeof(uint32_t));
}

bool cfgWriteU32(ConfigField field, uint32_t value) {
 return mulStore(field,(const uint8_t *)&value,sizeof(value));
}

bool cfgReadU8list(ConfigField field, uint8_t list[], uint8_t length) {
  int pos = tlvFindType(&tlv, field);

  if (pos > -1 && length<=tlv.data[pos+1]) {
    memcpy(list, &tlv.data[pos+2], length);
  }

  return (pos > -1 && length<=tlv.data[pos+1]);
}

bool cfgFieldSize(ConfigField field, uint8_t * size) {
  int pos = tlvFindType(&tlv, field);

  if (pos > -1) {
    *size = tlv.data[pos+1];
  }

  return (pos > -1);
}

bool cfgWriteU8list(ConfigField field, uint8_t list[], uint8_t length) {
 uint8_t size;if(cfgFieldSize(field,&size))return false;
 return mulStore(field,list,length);
}

bool cfgReadFP32listLength(ConfigField field, uint8_t * size) {
  bool success = cfgFieldSize(field, size);
  *size /= 4;
  return success;
}

bool cfgReadFP32list(ConfigField field, float list[], uint8_t length) {
  int pos = tlvFindType(&tlv, field);

  if (pos > -1 && (unsigned)length*sizeof(float)<=tlv.data[pos+1]) {
    memcpy(list, &tlv.data[pos+2], length*sizeof(float));
  }

  return (pos > -1 && (unsigned)length*sizeof(float)<=tlv.data[pos+1]);
}

bool cfgWriteFP32list(ConfigField field, float list[], uint8_t length) {
 if(length>23)return false;
 return mulStore(field,(const uint8_t *)list,length*sizeof(float));
}

static bool binaryMode = false;

void cfgSetBinaryMode(bool enable)
{
  binaryMode = enable;
}

bool cfgIsBinaryMode()
{
  return binaryMode;
}


/* Bounded additive TLV update. Existing field IDs/layout and unrelated bytes
 * remain untouched. Existing EEPROM checksum is retained; this is not a new
 * power-fail journal. Report write/readback failures rather than false success. */
static bool mulStore(ConfigField field,const uint8_t *data,uint8_t length) {
  /* LPP can write from the higher-priority UWB task. Serialize writers without
   * holding any lock across I2C, sleeps or readback. Busy requests fail closed. */
  taskENTER_CRITICAL();
  bool busy=writing||mulStorageUncertain;if(!busy)writing=true;
  taskEXIT_CRITICAL();
  if(busy)return false;
  bool ok=false;
  uint8_t previous[NUMBER_OF_BYTES_READ];memcpy(previous,buffer,sizeof(previous));
  int at=tlvFindType(&tlv,field);
  if(at>=0) {
    if(tlv.data[at+1]!=length)goto done;
    if(!memcmp(tlv.data+at+2,data,length)){ok=true;goto done;}
  } else {
    if(SIZE_HEADER+cfgHeader->tlvSize+2+length+SIZE_TAIL>=NUMBER_OF_BYTES_READ)goto done;
    at=cfgHeader->tlvSize;tlv.data[at]=field;tlv.data[at+1]=length;cfgHeader->tlvSize+=2+length;
  }
  memcpy(tlv.data+at+2,data,length);write_crc();
  uint8_t expected[NUMBER_OF_BYTES_READ];memcpy(expected,buffer,sizeof(expected));
  ok=eepromWrite(0,buffer,sizeof(buffer));HAL_Delay(10);
  if(ok)ok=readData()&&!memcmp(expected,buffer,sizeof(buffer));
  if(!ok){memcpy(buffer,previous,sizeof(buffer));mulStorageUncertain=true;}
  else if(field!=cfgRfEnable&&field!=cfgMulSlotMs&&field!=cfgMulSchedule&&field!=cfgMulClock&&field!=cfgMulGeometrySource)pendingRestart=true;
done:
  writing=false;return ok;
}
bool cfgMulRead(uint8_t node,uint8_t *role,uint8_t *reference,uint8_t *follower) {
  if(mulStorageUncertain||writing)return false;
  uint8_t size,values[3]={node==0x84?0:node==0x85?1:2,0x84,0x85};
  if(cfgFieldSize(cfgMulNetwork,&size)) {
    if(size!=3||!cfgReadU8list(cfgMulNetwork,values,3))return false;
  }
  if(values[0]>3)return false;
  uint8_t mode=0;cfgReadU8(cfgMode,&mode);
  if(values[0]==2&&mode==MODE_MUL_RLR)values[0]=3;
  if(values[0]==0)values[1]=node;
  if(values[0]==1)values[2]=node;
  mulNetwork_t network;if(!mulNetworkInit(&network,values[1],values[2]))return false;
  *role=values[0];*reference=values[1];*follower=values[2];return true;
}
bool cfgMulWrite(uint8_t role,uint8_t reference,uint8_t follower) {
  uint8_t node=0;if(!cfgReadU8(cfgAddress,&node)||role>3)return false;
  if((role==0&&reference!=node)||(role==1&&follower!=node))return false;
  mulNetwork_t network;if(!mulNetworkInit(&network,reference,follower)||(role==3&&(node==reference||node==follower)))return false;
  uint8_t values[3]={role,reference,follower};return mulStore(cfgMulNetwork,values,3);
}
bool cfgMulChangeAddress(uint8_t address) {
  if(cachedPlanStatus==mulPlanStored&&cachedPlan.selected)return false;
  uint8_t old=0,role,reference,follower,mode=0,size;
  if(!cfgReadU8(cfgAddress,&old)||!cfgMulRead(old,&role,&reference,&follower))return false;
  cfgReadU8(cfgMode,&mode);
  uint8_t ref=role==0?address:reference,fol=role==1?address:follower;
  mulNetwork_t network;
  if(mode>=MODE_MUL_ALR&&(!mulNetworkInit(&network,ref,fol)||
     ((mode==MODE_MUL_RLR||role==3)&&(address==ref||address==fol))))return false;
  /* Freeze a legacy-derived role BEFORE changing its address. A failed second
   * write is reported on failure; legacy EEPROM has no power-cut journal. */
  if(!cfgFieldSize(cfgMulNetwork,&size)&&!cfgMulWrite(role,reference,follower))return false;
  return mulStore(cfgAddress,&address,1);
}

/* Old EEPROMs had no slot field: retain the previous 500ms deployment until
 * the operator explicitly coordinates a change. Fresh factory images use10. */
bool cfgMulSlotRead(uint16_t *slotMs) {
 uint8_t size;uint32_t value=500;
 if(!slotMs||!cfgStorageReady())return false;
 if(cachedPlanStatus==mulPlanStored&&cachedPlan.selected){*slotMs=cachedPlan.plan.broadcast.slotMs;return true;}
 if(cfgFieldSize(cfgMulSchedule,&size)) {
  uint8_t bytes[MUL_SCHEDULE_BYTES];mulSchedule_t schedule;
  if(size!=sizeof(bytes)||!cfgReadU8list(cfgMulSchedule,bytes,sizeof(bytes))||!mulScheduleDecode(bytes,&schedule))return false;
  *slotMs=schedule.slotMs;return true;
 }
 if(!slotMs||!cfgStorageReady())return false;
 if(cfgFieldSize(cfgMulSlotMs,&size)&&(size!=4||!cfgReadU32(cfgMulSlotMs,&value)))return false;
 if(!mulScheduleTag(value))return false;
 *slotMs=(uint16_t)value;return true;
}
bool cfgMulSlotWrite(uint16_t slotMs) {
 if(cachedPlanStatus==mulPlanStored&&cachedPlan.selected)return false;
 if(!mulSlotValid(slotMs))return false;
 uint8_t size;
 if(cfgFieldSize(cfgMulSchedule,&size)) {
  uint8_t node;mulSchedule_t schedule;
  if(!cfgReadU8(cfgAddress,&node)||!cfgMulScheduleRead(node,&schedule))return false;
  schedule.slotMs=slotMs;return cfgMulScheduleWrite(node,&schedule);
 }
 if(!mulScheduleTag(slotMs))return false;
 return cfgWriteU32(cfgMulSlotMs,slotMs);
}

bool cfgMulScheduleRead(uint8_t node,mulSchedule_t *schedule) {
 uint8_t size;if(!schedule||!cfgStorageReady())return false;
 if(cachedPlanStatus==mulPlanStored&&cachedPlan.selected){*schedule=cachedPlan.plan.broadcast;return true;}
 if(cfgFieldSize(cfgMulSchedule,&size)) {
  uint8_t bytes[MUL_SCHEDULE_BYTES];
  return size==sizeof(bytes)&&cfgReadU8list(cfgMulSchedule,bytes,sizeof(bytes))&&mulScheduleDecode(bytes,schedule);
 }
 uint8_t role,ref,fol;uint16_t slot;
 if(!cfgMulRead(node,&role,&ref,&fol)||!cfgMulSlotRead(&slot))return false;
 mulScheduleLegacy(schedule,ref,fol,slot);return !mulScheduleValidate(schedule);
}
bool cfgMulScheduleWrite(uint8_t node,const mulSchedule_t *schedule) {
 if(cachedPlanStatus==mulPlanStored&&cachedPlan.selected)return false;
 if(mulScheduleValidate(schedule)||mulScheduleIndex(schedule,node)<0)return false;
 uint8_t bytes[MUL_SCHEDULE_BYTES];if(!mulScheduleEncode(bytes,schedule))return false;
 return mulStore(cfgMulSchedule,bytes,sizeof(bytes));
}

bool cfgMulClockRead(mulClockFitConfig_t *config) {
 if(!config||!cfgStorageReady())return false;
 uint8_t size;
 if(!cfgFieldSize(cfgMulClock,&size)){mulClockFitDefaults(config);return true;}
 uint8_t bytes[MUL_CLOCK_CONFIG_BYTES];
 return size==sizeof(bytes)&&cfgReadU8list(cfgMulClock,bytes,sizeof(bytes))&&mulClockFitConfigDecode(bytes,config);
}
bool cfgMulClockWrite(const mulClockFitConfig_t *config) {
 uint8_t bytes[MUL_CLOCK_CONFIG_BYTES];
 return mulClockFitConfigEncode(bytes,config)&&config->periodMs==1000&&mulStore(cfgMulClock,bytes,sizeof(bytes));
}

/* Full ranging plans use a separate commit-last journal, never the legacy image. */
static bool planRead(void *ctx,unsigned at,void *out,size_t n) {
 (void)ctx;return eepromRead((int)at,out,n);
}
static bool planWrite(void *ctx,unsigned at,const void *data,size_t n) {
 (void)ctx;return eepromWrite((int)at,(void *)data,n);
}
static mulPlanStoreWorkspace_t planWorkspace;
static const mulPlanStore_t planStorage={NULL,planRead,planWrite,&planWorkspace};
static void planBootLoad(void) {
 cachedPlanStatus=mulPlanStoreLoad(&planStorage,&cachedPlan);
 if(cachedPlanStatus==mulPlanIoError||cachedPlanStatus==mulPlanCorrupt)mulStorageUncertain=true;
}
mulPlanStoreResult_t cfgMulPlanCached(const mulStoredPlan_t **stored) {
 if(!stored)return mulPlanIoError;
 *stored=NULL;
 if(!cfgStorageReady())return cachedPlanStatus==mulPlanCorrupt?mulPlanCorrupt:mulPlanIoError;
 if(cachedPlanStatus==mulPlanStored)*stored=&cachedPlan;
 return cachedPlanStatus;
}
mulPlanStoreResult_t cfgMulPlanLoad(mulStoredPlan_t *stored) {
 if(uwbControlRequested()||uwbControlRunning())return mulPlanIoError;
 taskENTER_CRITICAL();bool busy=writing||mulStorageUncertain;if(!busy)writing=true;taskEXIT_CRITICAL();
 if(busy)return mulPlanIoError;
 mulPlanStoreResult_t result=mulPlanStoreLoad(&planStorage,stored);writing=false;return result;
}
bool cfgMulPlanSave(const mulRangingPlan_t *plan,bool selected,uint32_t *generation) {
 if(!generation||mulRangingPlanValidate(plan,NULL)||uwbControlRequested()||uwbControlRunning())return false;
 taskENTER_CRITICAL();bool busy=writing||mulStorageUncertain;if(!busy)writing=true;taskEXIT_CRITICAL();
 if(busy)return false;
 bool ok=mulPlanStoreSave(&planStorage,plan,selected,generation);
 if(ok){planBootLoad();ok=cachedPlanStatus==mulPlanStored&&!mulStorageUncertain;}
 // A failed final readback may follow a committed new state. Block activation
 // until reboot/reload rather than claiming the old plan is still current.
 if(!ok)mulStorageUncertain=true;
 writing=false;return ok;
}

bool cfgGeometrySourceRead(mulDelaySource_t *source) {
 uint8_t size=0,value=0;if(!source||!cfgStorageReady())return false;
 if(cfgFieldSize(cfgMulGeometrySource,&size)&&(size!=1||!cfgReadU8(cfgMulGeometrySource,&value)))return false;
 if(value>mulDelayNone)return false;
 *source=(mulDelaySource_t)value;return true;
}
bool cfgGeometrySourceWrite(mulDelaySource_t source) {
 if(source>mulDelayNone||uwbControlRequested()||uwbControlRunning())return false;
 uint8_t value=(uint8_t)source;return mulStore(cfgMulGeometrySource,&value,1);
}
