/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_alr.h"
#include "mul_common.h"
#include "mul_clock.h"
#include "mul_rx.h"
#include "mul_twr.h"
#include "mul_relation.h"
#include "mul_role_state.h"
#include "mul_timing.h"
#include "mul_clock_workspace.h"
#include "mul_geometry.h"
#include "task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// DW1000 time units: 499.2 MHz * 128. These are not STM32 milliseconds.
#define MUL_DTU_PER_MS UINT64_C(63897600)
#define MUL_TX_PERIOD_DTU (transmitPeriodDtu())
#define MUL_TX_LEAD_DTU (20 * MUL_DTU_PER_MS)
#define MUL_TX_MIN_LEAD_DTU (minimumLeadDtu())
#define MUL_TX_COMPLETION_MS 100
#define MUL_TX_ALIGNMENT UINT64_C(512)

// SYS_STATUS bits 27 (HPDWARN) and 34 (TXPUTE), read at byte offset 3.
// Match Decawave dwt_starttx()'s delayed-start check. libdw1000 returns void.
#define MUL_TX_ERROR_STATUS UINT16_C(0x0408)

static const mulNetwork_t *alrNetwork;
static const mulRangingPlan_t *activePlan;
static uint32_t servedOperations;
typedef struct {uint64_t start,poll,end;bool compensated;} mulLocalReservation_t;
static struct {
 mulLocalReservation_t local;uint64_t epoch,prepare;
 uint32_t cycle,context;mulDelaySource_t source;bool ready;
} followerPrep;
static uint8_t source[MUL_ADDRESS_LENGTH];
// dwSetData() reads length + 2 bytes when hardware FCS is enabled.
#define txFrame mulRoleIo.alr.frame
static uint32_t nextSequence;
static uint32_t pendingSequence;
static uint64_t nextRawTx;
static uint64_t pendingTimestamp;
static uint64_t pendingRawTx;
static struct {uint8_t tx:1,acquired:1,multi:1;} flags;
#define referenceAcquired flags.acquired
#define multiMode flags.multi
static int8_t alrRole = mulAlrUnsupported;
/* Legacy diagnostic counters and multi configuration are mode-exclusive. */
static union {
 mulSourceState_t legacy;
 struct {bool hasSequence;uint32_t sequence;const mulSchedule_t *schedule;
   uint8_t slot,served;uint64_t txTimestamp,rxTimestamp;} multi;
} referenceState;
#define followerReference referenceState.legacy
#define schedule referenceState.multi.schedule
#define localSlot referenceState.multi.slot
#define servedWindows referenceState.multi.served
static uint32_t refSequence(void){return multiMode?referenceState.multi.sequence:followerReference.sequence;}
static uint64_t refRx(void){return multiMode?referenceState.multi.rxTimestamp:followerReference.rxTimestamp;}
static uint64_t refTx(void){return multiMode?referenceState.multi.txTimestamp:followerReference.txTimestamp;}

// Follower decision codes: 0 armed,1 late before setup,2 future/stale,
// 3 DX readback,4 payload,5 late after setup,6 hardware warning,
// 7 RX age,8 rejected frame,9 old/duplicate. No formatting in the measured path.
static uint8_t slotReason;
#if MUL_SLOT_TRACE
static mulLogRecord_t slotTrace;
#define SLOT_TIME(i,v) do {if(mulLogLevel()==mulLogDebug)slotTrace.t[i]=(v);} while(0)
static void slotPublish(void) {
 if(mulLogLevel()!=mulLogDebug)return;
 slotTrace.cycle=refSequence();slotTrace.local=source[0];slotTrace.why=slotReason;slotTrace.event=1;
 mulLogPush(&slotTrace);
}
void mulAlrTimingRecord(unsigned event,uint32_t cycle,unsigned initiator,unsigned responder,unsigned why,
 uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
 mulLogRecord_t r={.t={a,b,c,d},.cycle=cycle,.local=source[0],.why=why,.event=event,
   .initiator=initiator,.responder=responder};mulLogPush(&r);
}
void mulAlrSlotPoll(bool (*write)(const char *,unsigned)){mulLogPoll(write);}
#else
#define SLOT_TIME(i,v) ((void)0)
#endif

static TickType_t lastReferenceTick;
#define followerClock mulRoleIo.alr.clock
#define ranging mulRoleScratch.twr
static uint8_t followerSeen;
static mulReferenceEvidence_t restartEvidence;
// One observation owned by the UWB task until TX completes/aborts. The packet,
// RX marker and observation time are immutable; no refit may intervene.
static struct {
 mulClockFitPending_t fit;mulTestPacket_t packet;uint64_t rx;
 uint32_t now;int32_t rawSkew;uint8_t rawStatus;bool postponed;
} deferredClock;
static __attribute__((noinline)) void invalidateFollowerRanging(dwDevice_t *dev) {
 followerPrep.ready=false;
 mulTwrInvalidateAa(&ranging);mulTwrCancel(&ranging,dev,"clock_epoch",true);ranging.attempted=false;
}
static uint64_t readHardwareTime(dwDevice_t *dev);
static void finishClock(dwDevice_t *dev,uint64_t before) {
 (void)before;
 if(!deferredClock.fit.active)return;
 if(deferredClock.fit.epoch!=followerClock.epoch){deferredClock.fit.active=false;return;}
 mulClockWorkspace_t *w=mulClockWorkspace();
 followerClock.skewPpb=deferredClock.rawSkew;followerClock.status=deferredClock.rawStatus;
 mulClockFitFinish(alrNetwork,&followerClock,&w->fit,&w->config,&deferredClock.packet,
   deferredClock.rx,deferredClock.now,&deferredClock.fit);
 uint64_t after=readHardwareTime(dev);
 unsigned budget=activePlan&&activePlan->policy==MUL_PLAN_TDMA?activePlan->kalmanBudgetMs:10;
 if(mulTimestampDifference(after,before)>(int64_t)(budget*MUL_DTU_PER_MS))mulLogCount(logOverrun);
 mulAlrTiming(3,deferredClock.packet.sequence,alrNetwork->addresses[0][0],source[0],0,
   before,after,deferredClock.packet.txTimestamp,deferredClock.rx);
 if(followerClock.sequence!=refSequence())followerClock.status=mulClockWarmup;
 if(deferredClock.fit.result==mulClockDiscontinuity) {
  invalidateFollowerRanging(dev);
 }
}


static uint64_t transmitPeriodDtu(void){return (uint64_t)(multiMode?schedule->periodMs:1000)*MUL_DTU_PER_MS;}
static uint64_t minimumLeadDtu(void){return ((uint64_t)(multiMode?mulScheduleLeadUs(schedule):5000)*MUL_DTU_PER_MS+999)/1000;}

#define MUL_REFERENCE_LOSS_MS MUL_SOURCE_TIMEOUT_MS

static uint64_t readHardwareTime(dwDevice_t *dev)
{
  dwTime_t now = {.full = 0};
  dwGetSystemTimestamp(dev, &now);
  return mulTimestampMask(now.full);
}

static void clearTxErrors(dwDevice_t *dev)
{
  // Write-one-to-clear only these two flags, without clearing other IRQ events.
  const uint8_t errors[] = {0x08, 0x04};
  dwSpiWrite(dev, SYS_STATUS, 3, errors, sizeof(errors));
}

static uint32_t waitForNextTx(dwDevice_t *dev)
{
  int64_t remaining = mulTimestampDifference(nextRawTx, readHardwareTime(dev));
  remaining -= (int64_t)MUL_TX_LEAD_DTU;
  if (remaining <= 0) {
    return 1; // Never spin at the UWB task's higher priority.
  }
  // Round up when converting hardware time to the event interface's milliseconds.
  return (uint32_t)(((uint64_t)remaining + MUL_DTU_PER_MS - 1) / MUL_DTU_PER_MS);
}

static bool readBroadcast(dwDevice_t *dev,mulTestPacket_t *packet,uint64_t *arrival,unsigned *slot) {
 if(!multiMode)return mulRxReadBroadcastNetwork(alrNetwork,dev,packet,arrival,NULL);
 unsigned n=dwGetDataLength(dev);if(n!=MUL_MULTI_REFERENCE_BYTES&&n!=MUL_MULTI_FOLLOWER_BYTES)return false;
 uint8_t frame[MUL_MULTI_FOLLOWER_BYTES];mulMultiBroadcast_t decoded;dwGetData(dev,frame,n);
 if(!(activePlan?mulPlanBroadcastDecode(activePlan,frame,n,&decoded):mulMultiBroadcastDecode(schedule,frame,n,&decoded)))return false;
 dwTime_t rx={.full=0};dwGetRawReceiveTimestamp(dev,&rx);
 int64_t age=mulTimestampDifference(readHardwareTime(dev),rx.full);
 SLOT_TIME(0,mulTimestampMask(rx.full));
 if(age<0||age>(int64_t)(15*MUL_DTU_PER_MS)){slotReason=7;return false;}
 *arrival=mulTimestampMask(rx.full);*packet=decoded.packet;packet->slotMs=schedule->slotMs;
 if(slot)*slot=decoded.slot;
 return true;
}

static void mulAlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  deferredClock.fit.active=false;followerPrep.ready=false;restartEvidence.valid=false;
  activePlan=config->mulPlan;servedOperations=0;
  dwIdle(dev);
  mulAntennaConfigure(dev);
  dwReceivePermanently(dev, false);
  memcpy(source, config->address, sizeof(source));
  nextSequence = 0;
  flags.tx = false;
  alrNetwork=config->mulConfigured?&config->mulNetwork:&mulDefaultNetwork;
  memset(&mulRoleIo,0,sizeof(mulRoleIo));
  alrRole=config->mulConfigured?config->mulRole:mulNetworkRole(alrNetwork,config->address);
  if(config->mulConfigured&&config->mulSchedule.profile==MUL_PROFILE_LEGACY) {
    int index=mulScheduleIndex(&config->mulSchedule,source[0]);
    if(index>=0){mulSchedulePair(&config->mulSchedule,1,&config->mulNetwork);alrRole=index?mulAlrFollower:mulAlrReference;}
  }
  if(!mulNetworkValid(alrNetwork)||mulNetworkRole(alrNetwork,source)!=alrRole)alrRole=mulAlrUnsupported;
  memset(&referenceState,0,sizeof(referenceState));
  multiMode=config->mulConfigured&&config->mulSchedule.profile>=MUL_PROFILE_MULTI;
  if(multiMode) {
    schedule=&config->mulSchedule;int index=mulScheduleIndex(schedule,source[0]);
    if(mulScheduleValidate(schedule)||index<0)alrRole=mulAlrUnsupported;
    else {localSlot=(uint8_t)index;alrRole=index?mulAlrFollower:mulAlrReference;
      (void)mulSchedulePair(schedule,index?index:1,&config->mulNetwork);
      if(mulNetworkRole(alrNetwork,source)!=alrRole)alrRole=mulAlrUnsupported;}
  }
  if(activePlan&&(!multiMode||mulRangingPlanValidate(activePlan,NULL)||mulScheduleFingerprint(&activePlan->broadcast)!=mulScheduleFingerprint(&config->mulSchedule)))alrRole=mulAlrUnsupported;
  if(!multiMode&&!mulScheduleTag(mulNetworkSlotMs(alrNetwork)))alrRole=mulAlrUnsupported;
#if !MUL_HYBRID_CH5
  if(multiMode&&schedule->profile==MUL_PROFILE_EXPERIMENTAL&&(config->lowBitrate||config->longPreamble))alrRole=mulAlrUnsupported;
#endif
  mulGeometryPair(alrNetwork);
  referenceAcquired = false;
  mulClockReset(&followerClock);
  mulClockWorkspace_t *workspace=mulClockWorkspace();mulClockFitReset(&workspace->fit);
  workspace->config=config->mulClockConfig;
  if(!mulClockFitConfigValid(&workspace->config))mulClockFitDefaults(&workspace->config);
  if(multiMode)workspace->config.periodMs=schedule->periodMs;
  mulTwrInit(&ranging);mulTwrConfigureNetwork(alrNetwork);
  followerSeen = false;

  if (alrRole != mulAlrReference) {
    if (alrRole == mulAlrFollower) {
      mulRxInit(dev);
    }
    return; // Unknown identities remain idle; no default transmit behavior.
  }
  nextRawTx = mulTimestampMask((readHardwareTime(dev) + MUL_TX_LEAD_DTU +
                               MUL_TX_ALIGNMENT - 1) & ~(MUL_TX_ALIGNMENT - 1));
  mulRxInit(dev); // Reference listens for the follower and directed TWR responses.
}

static bool selectedDelay(uint32_t now,mulClockDelay_t *delay) {
 mulDelaySource_t source=mulGeometrySource();
 return source==mulDelayTwr?mulTwrDelay(&ranging,&followerClock,delay):
  source==mulDelayExternal?mulGeometryDelay(alrNetwork,&followerClock,now,delay):false;
}

// Shared, tested Stage 2 TX setup. The caller supplies a raw local-clock target
// and a live TX_ANTD reading; no remote timestamp is used for hardware scheduling.
static bool startDelayedTx(dwDevice_t *dev, dwTime_t rawTx, uint16_t txAntennaDelay)
{
  pendingSequence = multiMode&&alrRole==mulAlrFollower?refSequence():nextSequence++;
  dwNewTransmit(dev);
  dwSetDefaults(dev);
  clearTxErrors(dev);
  dwWaitForResponse(dev, false);
  dwSetTxRxTime(dev, rawTx); // Raw RMARKER time: low 9 bits already zero.

  // Verify what the radio was actually programmed to use before encoding it.
  // DX_TIME denotes the raw RMARKER, not the start of the preamble. The driver
  // already clears its low 9 bits; no preamble or software latency is added.
  dwTime_t programmed = {.full = 0};
  dwSpiRead(dev, DX_TIME, NO_SUB, programmed.raw, LEN_DX_TIME);
  if (programmed.full != rawTx.full) {
    dwIdle(dev);
    slotReason=3;

    return false;
  }
  pendingTimestamp = mulTimestampMask(programmed.full + txAntennaDelay);
  unsigned int length=MUL_TEST_FRAME_LENGTH;
  if(alrRole!=mulAlrFollower) {
  if(multiMode)length=activePlan?mulPlanBroadcastEncode(txFrame,sizeof(txFrame),activePlan,0,pendingSequence,pendingTimestamp,NULL):mulMultiBroadcastEncode(txFrame,sizeof(txFrame),schedule,0,pendingSequence,pendingTimestamp,NULL);
  else (void)mulEncodeScheduledPacket(txFrame,source,pendingSequence,pendingTimestamp,mulNetworkSlotMs(alrNetwork));
  }
  if (alrRole==mulAlrFollower) {
    mulClockDelay_t delay;
    bool supplied=selectedDelay((uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,&delay);
    mulRelation_t relation=mulRelationPublishSourceNetwork(alrNetwork,&followerClock,supplied ? &delay : NULL,
      pendingTimestamp,readHardwareTime(dev),(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,mulGeometrySource()==mulDelayTwr&&mulTwrDelayPowerCorrected(&ranging),mulGeometrySource());
    if(followerClock.sequence!=refSequence()) {
      relation=(mulRelation_t){.version=relation.version,.maxAgeMs=relation.maxAgeMs,
        .epoch=followerClock.epoch,.referenceSequence=refSequence(),
        .period10ms=relation.period10ms,.externalPose=relation.externalPose};
    }
    if (relation.version==2 && !supplied&&mulGeometrySource()==mulDelayTwr)
      relation.delayState=mulTwrPropagationState(&ranging,&followerClock,(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS);
    mulTestPacket_t packet={.sequence=pendingSequence,.txTimestamp=pendingTimestamp,.slotMs=mulNetworkSlotMs(alrNetwork)}; memcpy(packet.source,source,8);
    if(multiMode)length=activePlan?mulPlanBroadcastEncode(txFrame,sizeof(txFrame),activePlan,localSlot,pendingSequence,pendingTimestamp,&relation):mulMultiBroadcastEncode(txFrame,sizeof(txFrame),schedule,localSlot,pendingSequence,pendingTimestamp,&relation);
    else {mulRelationEncode(txFrame,&packet,&relation);length=MUL_RELATION_FRAME_LENGTH;}
  }
  if(!length){slotReason=4;dwIdle(dev);return false;}
  dwSetData(dev, txFrame, length);

  // Check again after SPI setup, before arming. Leave room for preamble/power-up.
  uint64_t prepared=readHardwareTime(dev);SLOT_TIME(4,prepared);
  if (mulTimestampDifference(rawTx.full, prepared) <
      (int64_t)MUL_TX_MIN_LEAD_DTU) {
    dwIdle(dev);
    slotReason=5;

    return false;
  }
  pendingRawTx = rawTx.full;
  flags.tx = true;
  dwStartTransmit(dev);
  SLOT_TIME(5,readHardwareTime(dev));
  if (dwSpiRead16(dev, SYS_STATUS, 3) & MUL_TX_ERROR_STATUS) {
    dwIdle(dev);
    clearTxErrors(dev);
    flags.tx = false;
    slotReason=6;

    return false;
  }
  return true;
}

static uint32_t scheduleTx(dwDevice_t *dev)
{
  uint64_t now = readHardwareTime(dev);
  int64_t remaining = mulTimestampDifference(nextRawTx, now);
  if (remaining < (int64_t)MUL_TX_MIN_LEAD_DTU) {
    // Missed a deadline: skip whole periods to retain phase, without a catch-up loop.
    uint64_t periods = ((uint64_t)((int64_t)MUL_TX_MIN_LEAD_DTU - remaining) /
                        MUL_TX_PERIOD_DTU) + 1;
    nextRawTx = mulTimestampMask(nextRawTx + periods * MUL_TX_PERIOD_DTU);

    return waitForNextTx(dev);
  }
  if (remaining > (int64_t)(MUL_TX_PERIOD_DTU + MUL_TX_LEAD_DTU)) {
    // A long task stall makes modular phase ambiguous. Rebase safely, never burst.
    nextRawTx = mulTimestampMask((now + MUL_TX_LEAD_DTU + MUL_TX_ALIGNMENT - 1) &
                                ~(MUL_TX_ALIGNMENT - 1));
    return waitForNextTx(dev);
  }

  dwTime_t rawTx = {.full = nextRawTx};
  nextRawTx = mulTimestampMask(nextRawTx + MUL_TX_PERIOD_DTU);
  mulTwrCancel(&ranging, dev, NULL, false);
  followerSeen = false;
  if(multiMode){servedWindows=0;servedOperations=0;}
  uint16_t antennaDelay = dwSpiRead16(dev, TX_ANTD, NO_SUB);
  bool started = startDelayedTx(dev, rawTx, antennaDelay);
  mulTwrFlush(&ranging);
  if (started) {
    return MUL_TX_COMPLETION_MS;
  }
  mulAlrTiming(2,pendingSequence,source[0],0,4,rawTx.full,now,slotReason,0);
  return waitForNextTx(dev);
}

// Follower states: searching (!referenceAcquired), listening (acquired, no TX),
// or one delayed TX pending. Completion/failure returns to listening, never to a
// free-running schedule. Every arm consumes one fresh reference sequence.
static void abortFollowerTx(dwDevice_t *dev)
{
  dwIdle(dev);
  clearTxErrors(dev);
  flags.tx = false;
}

static bool mapReservation(const mulRangingOperation_t *op,uint32_t now,mulLocalReservation_t *out,uint64_t *prepare) {
 if(prepare&&op->startUs<MUL_PLAN_SERVICE_MARGIN_US)return false;
 mulClockDelay_t delay;bool supplied=selectedDelay((uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,&delay);
 uint32_t offsets[4]={op->startUs,op->pollUs,op->endUs,prepare?op->startUs-MUL_PLAN_SERVICE_MARGIN_US:0};uint64_t targets[4];
 bool compensated=supplied&&mulClockDelayStatusNetwork(alrNetwork,&followerClock,&delay,now)==mulClockDelayValid;
 for(unsigned i=0;i<(prepare?4u:3u);i++) {
  uint64_t ref=mulTimestampMask(refTx()+((uint64_t)offsets[i]*MUL_DTU_PER_MS+999)/1000);
  if(compensated) {
   mulClockCorrectedMapping_t mapped;
   if(!mulClockMapRefToFollowerSimultaneousNetwork(alrNetwork,&followerClock,ref,&delay,now,&mapped))return false;
   targets[i]=mapped.timestamp;
  }else {
   // Same arrival-aligned convention as TDMA. No true offset/physical bound is
   // asserted; this permits AR-only plans without inventing an AA measurement.
   mulClockMapping_t mapped;
   if(!mulClockMapRefToFollower(&followerClock,ref,now,&mapped))return false;
   targets[i]=mapped.timestamp;
  }
 }
 if(prepare)*prepare=targets[3];
 *out=(mulLocalReservation_t){.start=targets[0],.poll=targets[1],.end=targets[2],.compensated=compensated};return true;
}
// One immutable mapped reservation. No radio IO or repeated full mapping is
// performed at the reservation-start wakeup. Packet/SPI setup retains its guards.
static bool prepareFollower(dwDevice_t *dev,const mulRangingOperation_t *op,unsigned index) {
 if(followerPrep.ready&&followerPrep.cycle==refSequence())return true;
 if(flags.tx||deferredClock.fit.active||ranging.active)return false;
 uint64_t began=mulLogLevel()==mulLogDebug?readHardwareTime(dev):0;(void)began;
 uint32_t context=mulGeometryContext(),now=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;
 mulDelaySource_t source=mulGeometrySource();mulLocalReservation_t local;
 bool mapped=mapReservation(op,now,&local,&followerPrep.prepare);
 uint64_t mappedAt=mulLogLevel()==mulLogDebug?readHardwareTime(dev):0;(void)mappedAt;
 if(!mapped||!mulTwrConfigurePlan(&ranging,activePlan,refSequence(),index)||
    (source==mulDelayExternal&&context!=mulGeometryContext())) {
  servedOperations|=UINT32_C(1)<<index;
  mulAlrTiming(5,refSequence(),op->initiator,op->responder,1,began,mappedAt,readHardwareTime(dev),0);
  return false;
 }
 followerPrep.local=local;followerPrep.epoch=followerClock.epoch;
 followerPrep.cycle=refSequence();followerPrep.context=context;followerPrep.source=source;followerPrep.ready=true;
 mulAlrTiming(5,refSequence(),op->initiator,op->responder,0,began,mappedAt,readHardwareTime(dev),followerPrep.prepare);
 return true;
}
static bool plannedFollowerOperation(dwDevice_t *dev,uwbEvent_t event,mulAlrRole_t *role) {
 uint64_t now=readHardwareTime(dev);uint32_t ms=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;
 unsigned count=mulRangingPlanCount(activePlan,refSequence());
 for(unsigned index=0;index<count;index++) {
  mulRangingOperation_t op;(void)mulRangingPlanOperation(activePlan,refSequence(),index,&op);
  if((op.kind==MUL_PLAN_AA||op.kind==MUL_PLAN_FF)&&op.responder==source[0]) {
   int64_t phase=mulTimestampDifference(now,refRx());
   if(phase>=(int64_t)((uint64_t)op.startUs*MUL_DTU_PER_MS/1000)&&phase<(int64_t)((uint64_t)op.endUs*MUL_DTU_PER_MS/1000)) {
    if(ranging.active&&(ranging.id!=refSequence()||ranging.operation!=index))mulTwrCancel(&ranging,dev,"reservation_turnaround",false);
    *role=mulAlrFollower;return mulTwrConfigurePlan(&ranging,activePlan,refSequence(),index);
   }
  }else if((op.kind==MUL_PLAN_AR||op.kind==MUL_PLAN_FF)&&op.initiator==source[0]) {
   *role=mulAlrReference;
   bool staged=activePlan->policy==MUL_PLAN_TDMA;mulLocalReservation_t local;
   if(staged) {
    if(servedOperations&(UINT32_C(1)<<index)) {
     return ranging.planned&&ranging.active&&ranging.id==refSequence()&&ranging.operation==index;
    }
    if(!prepareFollower(dev,&op,index))continue;
    local=followerPrep.local;now=readHardwareTime(dev);
   }else if(!mapReservation(&op,ms,&local,NULL))continue;
   if(mulTimestampDifference(now,local.end)>=0){servedOperations|=UINT32_C(1)<<index;followerPrep.ready=false;continue;}
   if(mulTimestampDifference(now,staged?followerPrep.prepare:local.start)<0)continue;
   *role=mulAlrReference;
   if(ranging.active&&(ranging.id!=refSequence()||ranging.operation!=index))mulTwrCancel(&ranging,dev,"reservation_turnaround",false);
   uint32_t bit=UINT32_C(1)<<index;
   if(event==eventTimeout&&!(servedOperations&bit)) {
    servedOperations|=bit;
    if(staged) {
     followerPrep.ready=false;
     if(followerPrep.epoch==followerClock.epoch&&mulClockMapStatus(&followerClock,ms)==mulClockMapValid&&
        followerPrep.source==mulGeometrySource()&&
        (followerPrep.source!=mulDelayExternal||(followerPrep.context==mulGeometryContext()&&mulGeometrySnapshot(ms,NULL,NULL)==mulPoseAvailable)))
      {ranging.epoch=followerClock.epoch;mulTwrStartTargets(&ranging,dev,refSequence(),local.poll,local.end);}
     else mulAlrTiming(5,refSequence(),op.initiator,op.responder,2,now,local.start,local.poll,0);
    }else if(mulTwrConfigurePlan(&ranging,activePlan,refSequence(),index))
      {ranging.epoch=followerClock.epoch;mulTwrStartTargets(&ranging,dev,refSequence(),local.poll,local.end);}
   }
   return ranging.planned&&ranging.active&&ranging.id==refSequence()&&ranging.operation==index;
  }
 }
 return false;
}
static uint32_t plannedFollowerWait(dwDevice_t *dev,uint32_t fallback) {
 if(!referenceAcquired)return fallback;
 uint64_t now=readHardwareTime(dev);uint32_t ms=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;
 unsigned count=mulRangingPlanCount(activePlan,refSequence());
 for(unsigned index=0;index<count;index++) {
  mulRangingOperation_t op;(void)mulRangingPlanOperation(activePlan,refSequence(),index,&op);uint64_t start;
  if((op.kind==MUL_PLAN_AA||op.kind==MUL_PLAN_FF)&&op.responder==source[0]) {
   if(ranging.planned&&ranging.attempted&&ranging.id==refSequence()&&ranging.operation>=index)continue;
   uint64_t end=mulTimestampMask(refRx()+(uint64_t)op.endUs*MUL_DTU_PER_MS/1000);
   if(mulTimestampDifference(now,end)>=0)continue;
   start=mulTimestampMask(refRx()+(uint64_t)op.startUs*MUL_DTU_PER_MS/1000);
  }else if((op.kind==MUL_PLAN_AR||op.kind==MUL_PLAN_FF)&&op.initiator==source[0]&&!(servedOperations&(UINT32_C(1)<<index))) {
   if(activePlan->policy==MUL_PLAN_TDMA) {
    // Pending Kalman gets its full admission window first; preparation follows.
    if(!followerPrep.ready&&deferredClock.fit.active)
     start=mulTimestampMask(refRx()+(uint64_t)op.startUs*MUL_DTU_PER_MS/1000);
    else {
     if(!prepareFollower(dev,&op,index))continue;
     start=followerPrep.prepare;
    }
    now=readHardwareTime(dev); // Mapping/encoding elapsed time is not part of sleep.
    int64_t left=mulTimestampDifference(start,now);
    uint32_t wake=left<=0?1:(uint32_t)(deferredClock.fit.active?((left+MUL_DTU_PER_MS-1)/MUL_DTU_PER_MS):(left/MUL_DTU_PER_MS));
    if(!wake)wake=1; // Service interval follows protected processing; RF stays at POLL.
    if(wake<fallback)fallback=wake;
    continue;
   }
   mulLocalReservation_t local;if(!mapReservation(&op,ms,&local,NULL)||mulTimestampDifference(now,local.end)>=0)continue;start=local.start;
  }else continue;
  int64_t remaining=mulTimestampDifference(start,now);
  uint32_t wait=remaining<=0?1:(uint32_t)(((uint64_t)remaining+MUL_DTU_PER_MS-1)/MUL_DTU_PER_MS);
  if(wait<fallback)fallback=wait;
 }
 return fallback;
}

static uint32_t followerWait(dwDevice_t *dev)
{
  mulTwrFlush(&ranging);
  uint32_t nowMs = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
  mulTwrSync(&ranging, &followerClock, nowMs, false);
  uint32_t wait = MUL_REFERENCE_LOSS_MS;
  if (referenceAcquired) {
    TickType_t elapsed = (TickType_t)(xTaskGetTickCount() - lastReferenceTick);
    uint32_t elapsedMs = elapsed * portTICK_PERIOD_MS;
    wait = elapsedMs >= MUL_REFERENCE_LOSS_MS ? 1 : MUL_REFERENCE_LOSS_MS - elapsedMs;
  }
  if (flags.tx) {
    // The watchdog is absolute in the local hardware clock, so unrelated events
    // cannot restart or extend it. A millisecond wakeup never defines the TX time.
    int64_t remaining = mulTimestampDifference(
      mulTimestampMask(pendingRawTx + MUL_TX_COMPLETION_MS * MUL_DTU_PER_MS),
      readHardwareTime(dev));
    uint32_t txWait = remaining <= 0 ? 1 :
      (uint32_t)(((uint64_t)remaining + MUL_DTU_PER_MS - 1) / MUL_DTU_PER_MS);
    if (txWait < wait) {
      wait = txWait;
    }
  }
  uint64_t waitBegan=deferredClock.fit.active&&!flags.tx?readHardwareTime(dev):0;
  if(activePlan&&!flags.tx)wait=plannedFollowerWait(dev,wait);
  wait = mulTwrDelayWait(&ranging, nowMs, wait);
  wait=mulTwrWait(&ranging, dev, refRx(), wait);
  if(deferredClock.fit.active&&!flags.tx) {
    // Full update measured ~4.4ms. Reserve 10ms plus 1ms service margin;
    // this is an admission budget, NOT a measured hardware WCET.
    uint64_t hw=readHardwareTime(dev);
    unsigned period=multiMode?schedule->periodMs:1000;
    bool tdma=activePlan&&activePlan->policy==MUL_PLAN_TDMA;
    unsigned budget=tdma?activePlan->kalmanBudgetMs:10;
    int64_t required=(int64_t)(budget+1)*MUL_DTU_PER_MS;
    int64_t next=mulTimestampDifference(mulTimestampMask(refRx()+(uint64_t)period*MUL_DTU_PER_MS),hw);
    if(multiMode&&!tdma)for(unsigned i=1;i<schedule->count;i++) {
      int64_t until=mulTimestampDifference(mulTimestampMask(refRx()+(uint64_t)i*schedule->slotMs*MUL_DTU_PER_MS),hw);
      if(until>0&&until<next)next=until;
    }
    if(tdma) {
      mulRangingOperation_t op;
      if(!mulRangingPlanOperation(activePlan,refSequence(),0,&op))return 1;
      int64_t start=mulTimestampDifference(mulTimestampMask(refRx()+(uint64_t)op.startUs*MUL_DTU_PER_MS/1000),hw);
      int64_t end=mulTimestampDifference(mulTimestampMask(refRx()+(uint64_t)op.endUs*MUL_DTU_PER_MS/1000),hw);
      int64_t preparation=start;
      if((op.kind==MUL_PLAN_AR||op.kind==MUL_PLAN_FF)&&op.initiator==source[0])preparation-=(int64_t)(MUL_PLAN_SERVICE_MARGIN_US*MUL_DTU_PER_MS/1000);
      if(preparation>0&&preparation<next)next=preparation;
      if(start<=0&&end>0) {
        next=0; // No estimator work inside the global TWR phase.
        // Resume at its end, not just the20ms tail: larger budgets may not fit there.
        uint32_t after=(uint32_t)((end+MUL_DTU_PER_MS-1)/MUL_DTU_PER_MS)+1;
        if(after<wait)wait=after;
      }
    }
    // The wait is rounded up and its planner may have consumed CPU time.
    // Remove that rounding and elapsed planning time before admitting work.
    int64_t slack=(int64_t)(wait?wait-1:0)*MUL_DTU_PER_MS-mulTimestampDifference(hw,waitBegan);
    if(!ranging.active&&slack>=required&&next>required) {
      finishClock(dev,hw);return 1; // Re-evaluate TWR/clock state after completion.
    }
    if(!deferredClock.postponed) {
      mulAlrTiming(3,deferredClock.packet.sequence,alrNetwork->addresses[0][0],source[0],1,
        hw,slack>0?slack:0,next>0?next:0,budget);deferredClock.postponed=true;
    }
    if(next>0&&next<=required) {
      uint32_t after=(uint32_t)((next+MUL_DTU_PER_MS-1)/MUL_DTU_PER_MS)+1;
      if(after<wait)wait=after;
    }
    // Generated plans leave 20ms for next-reference preparation. Wake there
    // even if intervening broadcasts/TWR frames were lost; never wait a cycle.
    int64_t tail=mulTimestampDifference(mulTimestampMask(refRx()+(uint64_t)(period-20)*MUL_DTU_PER_MS),hw);
    uint32_t retry=tail<=0?1:(uint32_t)((tail+MUL_DTU_PER_MS-1)/MUL_DTU_PER_MS);
    if(retry<wait)wait=retry;
  }
  return wait;
}

static bool scheduleFollowerTx(dwDevice_t *dev, uint64_t localRx)
{
  uint16_t antennaDelay = dwSpiRead16(dev, TX_ANTD, NO_SUB);
  // Aim for antenna-referenced TX at RX + the configured slot, in this follower's clock.
  // Subtract TX_ANTD before aligning the raw DX_TIME upwards to 512 ticks.
  unsigned offset=mulNetworkSlotMs(alrNetwork)*(multiMode?localSlot:1);
  uint64_t rawTarget = mulTimestampMask(localRx + offset*MUL_DTU_PER_MS - antennaDelay);
  dwTime_t rawTx = {.full = mulTimestampMask(
    (rawTarget + MUL_TX_ALIGNMENT - 1) & ~(MUL_TX_ALIGNMENT - 1))};
  uint64_t checked=readHardwareTime(dev);
  SLOT_TIME(3,checked);SLOT_TIME(6,rawTx.full);SLOT_TIME(7,MUL_TX_MIN_LEAD_DTU);
  int64_t lead = mulTimestampDifference(rawTx.full, checked);
  if (lead < (int64_t)MUL_TX_MIN_LEAD_DTU ||
      lead > (int64_t)(offset*MUL_DTU_PER_MS + MUL_TX_ALIGNMENT)) {
    // Never move a missed slot forward, retry its sequence, or extrapolate a frame.
    slotReason=lead<(int64_t)MUL_TX_MIN_LEAD_DTU?1:2;
    // Report after RX rearming, never block before the radio decision.
    return false;
  }
  return startDelayedTx(dev, rawTx, antennaDelay);
}

static uint32_t followerOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  // RTOS ticks are only the Stage 3A.1 liveness watchdog, not a UWB clock model.
  TickType_t now = xTaskGetTickCount();
  bool lostNow = referenceAcquired &&
    (TickType_t)(now - lastReferenceTick) >= pdMS_TO_TICKS(MUL_REFERENCE_LOSS_MS);
  if (lostNow) {
    referenceAcquired = false;followerPrep.ready=false;
    mulClockWorkspace_t *w=mulClockWorkspace();
    if(deferredClock.fit.active||!mulClockFitHoldover(&followerClock,&w->fit,&w->config,(uint32_t)now*portTICK_PERIOD_MS)){
      deferredClock.fit.active=false; // Epoch loss invalidates queued work.
      mulClockReset(&followerClock);mulTwrInvalidateAa(&ranging);
    }
    mulTwrCancel(&ranging, dev, "reference_lost", true);
    ranging.attempted = false;
    if (flags.tx) {
      abortFollowerTx(dev);
    }
  }

  if (flags.tx) {
    if (event == eventPacketReceived &&
        (dwGetDataLength(dev) == MUL_TWR_FRAME_LENGTH || dwGetDataLength(dev)==MUL_MULTI_TWR_BYTES || dwGetDataLength(dev) == 68)) {
      return followerWait(dev); // A stale ranging IRQ must not displace an armed TDMA slot.
    }
    if (event == eventTimeout &&
        mulTimestampDifference(readHardwareTime(dev),
          mulTimestampMask(pendingRawTx + MUL_TX_COMPLETION_MS * MUL_DTU_PER_MS)) < 0) {
      return followerWait(dev); // Early wake: leave the delayed transmission armed.
    }
    if (event == eventPacketSent) {
      dwTime_t actual = {.full = 0};
      dwGetTransmitTimestamp(dev, &actual);
      actual.full = mulTimestampMask(actual.full);
      flags.tx = false;
      if(actual.full!=pendingTimestamp)abortFollowerTx(dev); // Stale TX IRQ is an abort, not confirmation.
      mulRxRearm(dev);
      mulRxDiagBeacon(actual.full);
      mulAlrTiming(2,pendingSequence,source[0],0,actual.full!=pendingTimestamp,
        pendingTimestamp,actual.full,0,0);
    } else {
      // RX cannot legitimately deliver another frame while this delayed TX waits
      // in idle. An unexpected RX event/error cancels it; do not re-arm it.
      abortFollowerTx(dev);
      mulRxRearm(dev);
      mulAlrTiming(2,refSequence(),source[0],0,event==eventTimeout?2:3,pendingTimestamp,0,0,0);
    }
    return followerWait(dev);
  }

  // Broadcasts cannot be TWR messages. Do not walk the previous cycle's
  // generated reservations/map every AR window before arming the new slot.
  bool broadcast=activePlan&&event==eventPacketReceived&&
    (dwGetDataLength(dev)==40||dwGetDataLength(dev)==80);
#if MUL_SLOT_TRACE
  if(broadcast&&mulLogLevel()==mulLogDebug){memset(&slotTrace,0,sizeof(slotTrace));SLOT_TIME(1,readHardwareTime(dev));}
#endif
  slotReason=8;
  if(broadcast&&ranging.active)mulTwrCancel(&ranging,dev,NULL,false);
  bool selected=!broadcast;mulAlrRole_t exchangeRole=mulAlrFollower;
  if(multiMode&&referenceAcquired&&!broadcast) {
    selected=false;
    if(activePlan) {
      selected=plannedFollowerOperation(dev,event,&exchangeRole);
      if(!selected&&ranging.active)mulTwrCancel(&ranging,dev,"reservation_end",false);
    }else for(unsigned window=0;window<MUL_SYNC_WINDOWS;window++)if(mulScheduleSyncOwner(schedule,refSequence(),window)==localSlot) {
      selected=mulTwrConfigureSchedule(&ranging,schedule,localSlot,window);break;
    }
    if(!selected&&event==eventPacketReceived&&dwGetDataLength(dev)==MUL_MULTI_TWR_BYTES){mulRxRearm(dev);return followerWait(dev);}
  }
  if (referenceAcquired && selected && mulTwrOnEvent(&ranging, dev, event, exchangeRole,
      refSequence(), refRx(), refTx(),
      followerClock.epoch, (uint32_t)now * portTICK_PERIOD_MS)) {
    return followerWait(dev);
  }

  mulTestPacket_t packet;
  uint64_t arrival = 0;
  bool valid = event == eventPacketReceived && readBroadcast(dev,&packet,&arrival,NULL);
  if(valid&&!mulNetworkScheduleMatches(alrNetwork,packet.slotMs)) {

    valid=false;referenceAcquired=false;deferredClock.fit.active=false;mulClockReset(&followerClock);
    mulTwrInvalidateAa(&ranging);mulTwrCancel(&ranging,dev,"slot_mismatch",true);
  }
  SLOT_TIME(2,readHardwareTime(dev));
  bool acquiredNow = false;
  bool armed = false;
  mulClockResult_t clockResult=mulClockIgnored;
  if (valid && mulNetworkRole(alrNetwork,packet.source) == mulAlrReference) {
    bool hasSequence=multiMode?referenceState.multi.hasSequence:followerReference.hasSequence;
    uint32_t delta=packet.sequence-refSequence();
    bool restart=false;
    if(hasSequence&&(delta>=UINT32_C(0x80000000)||!delta)) {
      if(packet.sequence!=refSequence()||packet.txTimestamp!=refTx())
        restart=mulReferenceRestartObserve(&restartEvidence,&packet,arrival,
          (uint32_t)now*portTICK_PERIOD_MS,multiMode?schedule->periodMs:1000);
    }else restartEvidence.valid=false;
    if(restart) {
      deferredClock.fit.active=false;
      mulClockReset(&followerClock); // Begin below reseeds the mismatched fit epoch.
      if(multiMode)referenceState.multi.hasSequence=false;
      else followerReference.hasSequence=false;
    }
    mulSequenceResult_t result;
    if(multiMode) {
      result=!referenceState.multi.hasSequence?mulSequenceFirst:!delta?mulSequenceDuplicate:delta>=UINT32_C(0x80000000)?mulSequenceOld:mulSequenceForward;
      if(result!=mulSequenceDuplicate&&result!=mulSequenceOld) {
        referenceState.multi.hasSequence=true;referenceState.multi.sequence=packet.sequence;
        referenceState.multi.rxTimestamp=arrival;referenceState.multi.txTimestamp=packet.txTimestamp;
      }
    }else {
      result=mulObservePacket(&followerReference,&packet,arrival);
    }
    if (result != mulSequenceDuplicate && result != mulSequenceOld) {
      slotReason=0;servedOperations=0;followerPrep.ready=false;
      acquiredNow = !referenceAcquired;
      referenceAcquired = true;
      lastReferenceTick = now;
      // Anchor metadata to THIS observation; the bounded prior does not steer
      // the hardware slot. Full Kalman work waits for TX resolution and slack.
      mulClockWorkspace_t *workspace=mulClockWorkspace();mulClockDelay_t delay;
      bool supplied=selectedDelay((uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,&delay);
      if(deferredClock.fit.active) {
        // Budget overrun: reject this clock observation rather than overwrite
        // accepted work or run a full update before the imminent slot.
        slotReason=10;followerClock.status=mulClockWarmup;
        mulAlrTiming(3,packet.sequence,alrNetwork->addresses[0][0],source[0],2,
          arrival,deferredClock.rx,0,0);
      }else {
      deferredClock.postponed=false;
      deferredClock.packet=packet;deferredClock.rx=arrival;deferredClock.now=(uint32_t)now*portTICK_PERIOD_MS;
      clockResult=mulClockFitBegin(alrNetwork,&followerClock,&workspace->fit,&workspace->config,
        &packet,arrival,deferredClock.now,true,supplied?&delay:NULL,&deferredClock.fit);
      if(deferredClock.fit.active) {
        deferredClock.rawSkew=followerClock.skewPpb;deferredClock.rawStatus=followerClock.status;
        int32_t skew;
        bool prior=mulClockFitPrior(&followerClock,&workspace->fit,&deferredClock.fit,mulNetworkSlotMs(alrNetwork),&skew);
        followerClock.status=prior?mulClockValid:mulClockWarmup;
        if(prior)followerClock.skewPpb=skew;
      }
      if (restart || clockResult==mulClockDiscontinuity || (acquiredNow&&!workspace->fit.recovered)) {
        invalidateFollowerRanging(dev);
      }
      }
      armed = scheduleFollowerTx(dev, arrival);
      if(!armed)mulLogCount(logMiss);
    }else slotReason=9;
  }
  // Turn directly from the completed RX into delayed TX. Enabling RX briefly
  // before dwNewTransmit() could leave a new RX IRQ racing the pending slot.
  // A software wakeup did not consume a hardware frame. TRXOFF + RX status
  // clear here can destroy a frame completing after the semaphore timed out,
  // and periodically interrupts preamble search. RX/TX/error callbacks and
  // active TWR cancellation already own their required rearming.
  if (!armed && (event != eventTimeout || lostNow || !activePlan)) {
    mulRxRearm(dev);
  }
#if MUL_SLOT_TRACE
  if(broadcast)slotPublish();
#endif
  return followerWait(dev);
}

/* One radio, two disjoint maintenance windows. Reference reuses one TWR context;
 * each follower keeps its own committed propagation estimate. */
static uint32_t plannedReferenceService(dwDevice_t *dev,bool start) {
 uint32_t wait=waitForNextTx(dev);int64_t phase=mulTimestampDifference(readHardwareTime(dev),pendingTimestamp);
 unsigned count=mulRangingPlanCount(activePlan,pendingSequence);
 for(unsigned index=0;index<count;index++) {
  uint32_t bit=UINT32_C(1)<<index;if(servedOperations&bit)continue;
  mulRangingOperation_t op;(void)mulRangingPlanOperation(activePlan,pendingSequence,index,&op);
  int64_t begins=(int64_t)((uint64_t)op.startUs*MUL_DTU_PER_MS/1000);
  if(phase>=begins&&start) {
   servedOperations|=bit;
   if(ranging.active)mulTwrCancel(&ranging,dev,"reservation_turnaround",false);
   int follower=mulScheduleIndex(schedule,op.responder);
   if(op.initiator==source[0]&&(op.kind==MUL_PLAN_AR||(follower>0&&(followerSeen&(1u<<follower))))&&
      phase<(int64_t)((uint64_t)op.pollUs*MUL_DTU_PER_MS/1000)-(int64_t)minimumLeadDtu()&&
      mulTwrConfigurePlan(&ranging,activePlan,pendingSequence,index)){ranging.epoch=followerClock.epoch;mulTwrStart(&ranging,dev,pendingSequence,pendingTimestamp);}
  }else {
   int64_t remaining=begins-phase;
   uint32_t ms=remaining<=0?1:(uint32_t)((remaining+MUL_DTU_PER_MS-1)/MUL_DTU_PER_MS);
   if(ms<wait)wait=ms;
  }
 }
 return mulTwrWait(&ranging,dev,pendingTimestamp,wait);
}

static uint32_t multiReferenceService(dwDevice_t *dev,bool start) {
 if(activePlan)return plannedReferenceService(dev,start);
 uint32_t wait=waitForNextTx(dev);int64_t phase=mulTimestampDifference(readHardwareTime(dev),pendingTimestamp);
 for(unsigned w=0;w<MUL_SYNC_WINDOWS;w++) {
   unsigned owner=mulScheduleSyncOwner(schedule,pendingSequence,w);
   if(!owner||(servedWindows&(1u<<w)))continue;
   unsigned prep=mulScheduleSyncPollMs(w)-20;
   if(phase>=(int64_t)(prep*MUL_DTU_PER_MS)&&start) {
     servedWindows|=1u<<w;
     if(ranging.active)mulTwrCancel(&ranging,dev,"window_turnaround",false);
     if((followerSeen&(1u<<owner))&&phase<(int64_t)((mulScheduleSyncPollMs(w)-5)*MUL_DTU_PER_MS)) {
       mulTwrInit(&ranging);mulTwrConfigureSchedule(&ranging,schedule,owner,w);
       (void)mulSchedulePair(schedule,owner,(mulNetwork_t *)alrNetwork);
       mulTwrStart(&ranging,dev,pendingSequence,pendingTimestamp);
     }
   }else {
     int64_t remaining=(int64_t)(prep*MUL_DTU_PER_MS)-phase;
     uint32_t ms=remaining<=0?1:(uint32_t)((remaining+MUL_DTU_PER_MS-1)/MUL_DTU_PER_MS);
     if(ms<wait)wait=ms;
   }
 }
 return mulTwrWait(&ranging,dev,pendingTimestamp,wait);
}

static uint32_t referenceWait(dwDevice_t *dev)
{
  if(multiMode)return multiReferenceService(dev,false);
  uint32_t wait = waitForNextTx(dev);
  if (followerSeen && mulTwrDue(pendingSequence) &&
      !(ranging.attempted && ranging.id == pendingSequence)) {
    int64_t remaining = mulTimestampDifference(
      mulTimestampMask(pendingTimestamp + MUL_TWR_PREPARE_MS*MUL_DTU_PER_MS), readHardwareTime(dev));
    uint32_t twrWait = remaining <= 0 ? 1 :
      (uint32_t)(((uint64_t)remaining + MUL_DTU_PER_MS - 1) / MUL_DTU_PER_MS);
    if (twrWait < wait) wait = twrWait;
  }
  return mulTwrWait(&ranging, dev, pendingTimestamp, wait);
}

static uint32_t mulAlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  mulRxDiagEvent(dev,event);
  if(event==eventTimeout&&activePlan&&(alrRole==mulAlrReference||referenceAcquired))
    mulRxDiagService(dev,activePlan,alrRole==mulAlrReference?pendingSequence:refSequence(),source[0],
                     alrRole==mulAlrReference?pendingTimestamp:refRx());
  if (alrRole == mulAlrFollower) {
    return followerOnEvent(dev, event);
  }
  if (alrRole != mulAlrReference) {
    dwIdle(dev);
    return MAX_TIMEOUT;
  }
  if (!flags.tx) {
    // A delayed or stale IRQ cannot retain TWR ownership across the next mandatory
    // broadcast preparation deadline. Hardware time always wins over ranging.
    int64_t remaining = mulTimestampDifference(nextRawTx, readHardwareTime(dev));
    if (remaining <= (int64_t)(MUL_TX_LEAD_DTU + MUL_TX_ALIGNMENT) ||
        remaining > (int64_t)(MUL_TX_PERIOD_DTU + MUL_TX_LEAD_DTU)) {
      mulTwrCancel(&ranging, dev, NULL, false);
      if (event == eventTimeout) return scheduleTx(dev);
    } else {
      if(multiMode&&event==eventTimeout)multiReferenceService(dev,true);
      if (mulTwrOnEvent(&ranging, dev, event, mulAlrReference, pendingSequence,
          pendingTimestamp, pendingTimestamp, 0, (uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS)) {
        return referenceWait(dev);
      }
      if (event == eventPacketReceived) {
        mulTestPacket_t packet;
        uint64_t arrival;
        unsigned slot=1;
        if (readBroadcast(dev,&packet,&arrival,&slot) && (multiMode?slot!=0:mulNetworkRole(alrNetwork,packet.source)==mulAlrFollower)) {
          int64_t phase = mulTimestampDifference(arrival, pendingTimestamp);
          if(!mulNetworkScheduleMatches(alrNetwork,packet.slotMs)) { /* Reject mismatched schedule. */ }
          else if(multiMode) {
            int64_t expected=(int64_t)slot*schedule->slotMs*MUL_DTU_PER_MS;
            int64_t tolerance=(int64_t)mulSlotToleranceMs(schedule->slotMs)*MUL_DTU_PER_MS;
            if(packet.sequence==pendingSequence&&phase>=expected-tolerance&&phase<=expected+tolerance)followerSeen|=1u<<slot;
          }
          else if (phase >= (int64_t)(mulSlotMinDtu(mulNetworkSlotMs(alrNetwork))) && phase <= (int64_t)((mulNetworkSlotMs(alrNetwork)+2*mulSlotToleranceMs(mulNetworkSlotMs(alrNetwork)))*MUL_DTU_PER_MS))
            followerSeen = true;
        }
        mulRxRearm(dev);
        return referenceWait(dev);
      }
      if (event == eventTimeout) {
        int64_t phase = mulTimestampDifference(readHardwareTime(dev), pendingTimestamp);
        if (!multiMode && followerSeen && mulTwrDue(pendingSequence) && phase >= (int64_t)(MUL_TWR_PREPARE_MS*MUL_DTU_PER_MS) &&
            !(ranging.attempted && ranging.id == pendingSequence)) {
          mulTwrStart(&ranging, dev, pendingSequence, pendingTimestamp);
        }
        return referenceWait(dev);
      }
    }
  }
  switch (event) {
    case eventTimeout:
      if (!flags.tx) {
        return scheduleTx(dev);
      }
      dwIdle(dev);
      clearTxErrors(dev);
      flags.tx = false;
      mulAlrTiming(2,pendingSequence,source[0],0,2,pendingTimestamp,0,0,0);
      break;
    case eventPacketSent:
      if (flags.tx) {
        dwTime_t actual = {.full = 0};
        dwGetTransmitTimestamp(dev, &actual);
        actual.full = mulTimestampMask(actual.full);
        flags.tx = false;
        mulRxRearm(dev);
        mulRxDiagBeacon(actual.full);
        mulAlrTiming(2,pendingSequence,source[0],0,actual.full!=pendingTimestamp,
        pendingTimestamp,actual.full,0,0);
      }
      break;
    case eventReceiveTimeout:
    case eventReceiveFailed:
      dwIdle(dev);
      clearTxErrors(dev);
      flags.tx = false;
      mulRxRearm(dev);

      break;
    case eventPacketReceived:
      if (flags.tx) {
        return MUL_TX_COMPLETION_MS;
      }
      break;
  }
  return referenceWait(dev);
}

uwbAlgorithm_t mulAlrAlgorithm = {
  .init = mulAlrInit,
  .onEvent = mulAlrOnEvent,
};
