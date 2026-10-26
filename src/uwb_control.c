/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "uwb_control.h"
#include "phy_interop_dw.h"
#include "mul_diag_dw.h"
#include "mul_ads_dw.h"
static volatile bool requested,running;
static bool allowed;
static volatile uint16_t requestedSlot;
static uint32_t events[5];
void uwbControlInit(const uwbConfig_t *c) {
 uint8_t role=2,ref,fol;bool enabled=false;
 bool network=cfgMulRead(c->address[0],&role,&ref,&fol);
 uint8_t mode=255,node=0;
 bool identity=cfgReadU8(cfgMode,&mode)&&mode==c->mode&&cfgReadU8(cfgAddress,&node)&&node==c->address[0];
 allowed=cfgStorageReady()&&identity&&network&&c->mode<=MODE_MUL_RLR;
 if(c->mode>=MODE_MUL_ALR)allowed=allowed&&network&&
   (c->mode==MODE_MUL_ALR?role<2: c->address[0]!=ref&&c->address[0]!=fol);
 if(role==3&&c->mode!=MODE_MUL_RLR)allowed=false;
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
 allowed=cfgStorageReady()&&identity&&network&&role!=3;
#endif
 uint16_t slot=0;if(!cfgMulSlotRead(&slot))allowed=false;requestedSlot=slot;
 requested=uwbControlCanEnable()&&cfgRfEnabled(&enabled)&&enabled;running=false;
}
bool uwbControlCanEnable(void) {
 mulSchedule_t schedule;mulClockFitConfig_t clockConfig;
 const uwbConfig_t *c=uwbGetConfig();
 if(!cfgMulClockRead(&clockConfig))return false;
 if(cfgPendingRestart()||!cfgMulScheduleRead(c->address[0],&schedule))return false;
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
 if(schedule.profile==MUL_PROFILE_MULTI)return false;
#endif
 if(c->mode<MODE_MUL_ALR)return allowed;
 uint8_t storedBytes;
 if(schedule.profile==MUL_PROFILE_LEGACY&&!cfgFieldSize(cfgMulSchedule,&storedBytes))return allowed;
 uint8_t mode,node;
 return cfgStorageReady()&&cfgReadU8(cfgMode,&mode)&&mode==MODE_MUL_ALR&&c->mode==mode&&
   cfgReadU8(cfgAddress,&node)&&node==c->address[0]&&mulScheduleIndex(&schedule,node)>=0;
}
bool uwbControlRequest(bool enabled) {
 if(enabled&&!uwbControlCanEnable())return false;
 requested=enabled;return true;
}
bool uwbControlRequestSlot(uint16_t slotMs) {
 if(!mulSlotValid(slotMs)||requested||running||!uwbControlCanEnable())return false;
 requestedSlot=slotMs;return true;
}
bool uwbControlRequested(void) {return requested;}
bool uwbControlRunning(void) {return running;}
bool uwbControlApply(dwDevice_t *dev,uwbAlgorithm_t *algorithm) {
 /* Slot updates are applied only by the radio owner after RF OFF has
  * completed. Reinitializing on the next ON discards all old timing models. */
 if(!running) {
   uwbConfig_t *c=uwbGetConfig();
   (void)cfgMulScheduleRead(c->address[0],&c->mulSchedule);
   (void)cfgMulClockRead(&c->mulClockConfig);
   c->mulNetwork.slotMs=requestedSlot;
   if(c->mode>=MODE_MUL_ALR&&c->mode<=MODE_MUL_RLR) {
     int slot=mulScheduleIndex(&c->mulSchedule,c->address[0]);
     if(slot>=0) {mulSchedulePair(&c->mulSchedule,slot?slot:1,&c->mulNetwork);
       c->mulRole=c->mode==MODE_MUL_ALR?(slot?mulAlrFollower:mulAlrReference):mulAlrUnsupported;c->mulConfigured=true;}
   }
 }
 if(uwbGetConfig()->mulSchedule.profile==MUL_PROFILE_MULTI&&!uwbControlCanEnable())requested=false;
 if(requested==running)return false;
 dwIdle(dev);dwClearAllStatus(dev);running=false;
 if(requested){algorithm->init(uwbGetConfig(),dev);running=true;}
 return true;
}
bool uwbControlEvent(uwbEvent_t event) {
 if(!running||!requested)return false;
 if((unsigned)event<5&&events[event]!=UINT32_MAX)events[event]++;
 return true;
}
uint32_t uwbControlCount(unsigned event) {return event<5?events[event]:0;}
bool uwbControlResetCounters(void) {
 if(requested||running)return false;
 for(unsigned i=0;i<5;i++)events[i]=0;
 return true;
}
