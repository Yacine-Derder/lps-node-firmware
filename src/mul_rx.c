/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_rx.h"

#if MUL_RX_DIAG
#include "task.h"
#include "mul_multi_protocol.h"
#include "usbd_cdc_if.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define RX_DIAG_CAPACITY 4u
// Four records plus one deferred USB record. One physical UWB owner.
typedef struct {
 uint64_t now,poll,rearm,beacon;
 uint32_t state,cycle;
 uint16_t timeouts,failures,received;
 uint8_t status[5],irq[5],op,local,point,mode,activity;
} rxDiagRecord_t;
static rxDiagRecord_t diagRing[RX_DIAG_CAPACITY];
static uint8_t diagHead,diagTail,diagCount,diagActivity;
static uint16_t diagLost,diagTimeouts,diagFailures,diagReceived;
static uint8_t diagIrq[5];
static uint64_t diagRearm,diagBeacon,diagPoll,diagEnd;
static uint32_t diagCycle;
static uint8_t diagOp,diagLocal,diagPoints;
static bool diagValid;
void mulRxDiagBeacon(uint64_t stamp){diagBeacon=stamp;}
void mulRxDiagDecision(unsigned event,unsigned stage) {
 if(event==0)diagActivity|=2; //90-byte frame entered planned AA decoder.
 if(event==4&&stage==0x10)diagActivity|=4; // Accepted POLL.
 if(event==8)diagActivity|=8; // AA1 REJECT; consult stage/why there.
}
void mulRxDiagEvent(dwDevice_t *dev,uwbEvent_t event) {
 if(event==eventTimeout)return; // Never reinterpret stale cached IRQ status.
 // Driver clear/reset helpers do not mutate sysstatus: this is its pre-clear read.
 for(unsigned i=0;i<5;i++)diagIrq[i]|=dev->sysstatus[i];
 if(event==eventReceiveTimeout)diagTimeouts++;
 if(event==eventReceiveFailed)diagFailures++;
 if(event==eventPacketReceived){diagReceived++;if(dwGetDataLength(dev)==MUL_MULTI_TWR_BYTES)diagActivity|=1;}
}
static void diagSnapshot(dwDevice_t *dev,uint64_t now,unsigned point) {
 rxDiagRecord_t r={.now=now,.poll=diagPoll,.rearm=diagRearm,.beacon=diagBeacon,
  .cycle=diagCycle,.op=diagOp,.local=diagLocal,.point=point,.mode=dev->deviceMode,
  .activity=diagActivity,.timeouts=diagTimeouts,.failures=diagFailures,.received=diagReceived};
 uint8_t state[4];
 dwSpiRead(dev,0x19,NO_SUB,state,sizeof(state)); // SYS_STATE, APS022 section4.3.
 r.state=(uint32_t)state[0]|(uint32_t)state[1]<<8|(uint32_t)state[2]<<16|(uint32_t)state[3]<<24;
 dwSpiRead(dev,SYS_STATUS,NO_SUB,r.status,sizeof(r.status)); // Read only; no W1C.
 memcpy(r.irq,diagIrq,5);memset(diagIrq,0,5);
 taskENTER_CRITICAL();
 if(diagCount==RX_DIAG_CAPACITY)diagLost++;
 else {diagRing[diagHead]=r;diagHead=(diagHead+1)%RX_DIAG_CAPACITY;diagCount++;}
 taskEXIT_CRITICAL();
}
void mulRxDiagService(dwDevice_t *dev,const mulRangingPlan_t *plan,uint32_t cycle,uint8_t local,uint64_t origin) {
 // Only existing software-timeout callbacks call here, before recovery/TX setup.
 // No dedicated wakeup, no hardware-state reads on the packet/IRQ path.
 dwTime_t time={.full=0};dwGetSystemTimestamp(dev,&time);uint64_t now=mulTimestampMask(time.full);
 if(diagValid&&!(diagPoints&2)&&mulTimestampDifference(now,diagEnd)>=0) {
  diagSnapshot(dev,now,2);diagPoints|=2;
 }
 unsigned count=mulRangingPlanCount(plan,cycle);
 for(unsigned i=0;i<count;i++) {
  mulRangingOperation_t op;(void)mulRangingPlanOperation(plan,cycle,i,&op);
  if(op.kind!=MUL_PLAN_AA||(op.initiator!=local&&op.responder!=local))continue;
  uint64_t end=mulTimestampMask(origin+(uint64_t)op.endUs*638976/10);
  if(mulTimestampDifference(now,end)>=0)continue;
  if(!diagValid||diagCycle!=cycle||diagOp!=i||diagLocal!=local) {
   diagValid=true;diagCycle=cycle;diagOp=i;diagLocal=local;diagPoints=0;diagActivity=0;
   diagPoll=mulTimestampMask(origin+(uint64_t)op.pollUs*638976/10);diagEnd=end;
  }
  if(!(diagPoints&1)&&mulTimestampDifference(now,diagPoll)>=-(int64_t)(2000*638976/10)) {
   diagSnapshot(dev,now,1);diagPoints|=1;
  }
  break;
 }
}
void mulRxDiagPoll(bool (*write)(const char *,unsigned)) {
 static rxDiagRecord_t r;static uint16_t lost;static unsigned part;
 if(!part) {
  taskENTER_CRITICAL();
  if(diagCount){r=diagRing[diagTail];diagTail=(diagTail+1)%RX_DIAG_CAPACITY;diagCount--;lost=diagLost;part=1;}
  taskEXIT_CRITICAL();
 }
 if(!part||!write)return;
 char line[CDC_TRY_WRITE_MAX+1];int n;
#define TS(v) (unsigned)((v)>>32),(uint32_t)(v)
 if(part==1)n=snprintf(line,sizeof(line),"RX1 c=%" PRIu32 " o=%u l=%02X k=%u n=%02X%08" PRIX32 " p=%02X%08" PRIX32 " h=%08" PRIX32 " s=%02X%02X%02X%02X%02X i=%02X%02X%02X%02X%02X a=%02X z=%u\r\n",
  r.cycle,r.op,r.local,r.point,TS(r.now),TS(r.poll),r.state,
  r.status[4],r.status[3],r.status[2],r.status[1],r.status[0],r.irq[4],r.irq[3],r.irq[2],r.irq[1],r.irq[0],r.activity,lost);
 else n=snprintf(line,sizeof(line),"RX2 c=%" PRIu32 " o=%u l=%02X k=%u n=%02X%08" PRIX32 " r=%02X%08" PRIX32 " b=%02X%08" PRIX32 " t=%u f=%u g=%u m=%u z=%u\r\n",
  r.cycle,r.op,r.local,r.point,TS(r.now),TS(r.rearm),TS(r.beacon),r.timeouts,r.failures,r.received,r.mode,lost);
#undef TS
 if(n>0&&(unsigned)n<sizeof(line)&&write(line,(unsigned)n))part=part==1?2:0;
}
#endif


void mulAntennaConfigure(dwDevice_t *dev)
{
  // libdw1000 caches ONE value and commits it to BOTH registers. MUL needs
  // independent directions. Cache TX for legacy driver helpers; write RX
  // explicitly, and never full-commit in MUL. Validation detects such overwrites.
  dev->antennaDelay.full = MUL_ANTENNA_TX_DTU;
  const uint8_t tx[] = {MUL_ANTENNA_TX_DTU & 0xff, MUL_ANTENNA_TX_DTU >> 8};
  const uint8_t rx[] = {MUL_ANTENNA_RX_DTU & 0xff, MUL_ANTENNA_RX_DTU >> 8};
  dwSpiWrite(dev, TX_ANTD, NO_SUB, tx, sizeof(tx));
  dwSpiWrite(dev, LDE_IF, LDE_RXANTD_SUB, rx, sizeof(rx));
}

bool mulAntennaValid(dwDevice_t *dev)
{
  return dwSpiRead16(dev, TX_ANTD, NO_SUB) == MUL_ANTENNA_TX_DTU &&
         dwSpiRead16(dev, LDE_IF, LDE_RXANTD_SUB) == MUL_ANTENNA_RX_DTU &&
         dev->antennaDelay.full == MUL_ANTENNA_TX_DTU;
}

void mulRxRearm(dwDevice_t *dev)
{
  dwNewReceive(dev);
  dwSetDefaults(dev);
  dwStartReceive(dev);
#if MUL_RX_DIAG
  dwTime_t now={.full=0};dwGetSystemTimestamp(dev,&now);
  diagRearm=mulTimestampMask(now.full);
#endif
}

void mulRxInit(dwDevice_t *dev)
{
  // Preserve uwbInit()'s PHY. Disable automatic RX re-enable so timestamps and
  // buffers remain stable until the event callback has captured the observation.
  dwNewConfiguration(dev);
  dwReceivePermanently(dev, false);
  dwSetFrameFilter(dev, false);
  dwSetReceiverAutoReenable(dev, false);
  dwSetReceiveWaitTimeout(dev, 0);
  dwInterruptOnReceiveFailed(dev, true);
  // These changes affect only RX/IRQ controls. A full commit re-tunes the PHY
  // and overwrites live TX_ANTD/LDE_RXANTD from the driver's cached defaults.
  dwWriteSystemConfigurationRegister(dev);
  dwWriteSystemEventMaskRegister(dev);
  mulRxRearm(dev);
}

bool mulRxReadBroadcastNetwork(const mulNetwork_t *network,dwDevice_t *dev,mulTestPacket_t *packet,uint64_t *localRx,mulRelation_t *relation)
{
  uint8_t frame[MUL_RELATION_FRAME_LENGTH];
  mulRelation_t ignored;
  unsigned int length = dwGetDataLength(dev);
  if (length != MUL_TEST_FRAME_LENGTH && length != MUL_RELATION_FRAME_LENGTH) {
    return false;
  }
  dwGetData(dev, frame, length);
  if (!mulBroadcastDecodeNetwork(network,frame,length,packet,relation ? relation : &ignored)) {
    return false;
  }
  dwTime_t arrival = {.full = 0};
  dwGetRawReceiveTimestamp(dev, &arrival);
  *localRx = mulTimestampMask(arrival.full);
  return true;
}

bool mulRxRead(dwDevice_t *dev,mulTestPacket_t *packet,uint64_t *localRx)
{
  return mulRxReadBroadcast(dev,packet,localRx,NULL);
}

bool mulRxReadBroadcast(dwDevice_t *dev,mulTestPacket_t *packet,uint64_t *localRx,mulRelation_t *relation) {
 return mulRxReadBroadcastNetwork(NULL,dev,packet,localRx,relation);
}
