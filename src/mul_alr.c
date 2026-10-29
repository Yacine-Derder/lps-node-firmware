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

static TickType_t lastReferenceTick;
#define followerClock mulRoleIo.alr.clock
#define ranging mulRoleScratch.twr
static uint8_t followerSeen;

static uint64_t transmitPeriodDtu(void){return (uint64_t)(multiMode?schedule->periodMs:1000)*MUL_DTU_PER_MS;}
static uint64_t minimumLeadDtu(void){return ((uint64_t)(multiMode?mulScheduleLeadUs(schedule):5000)*MUL_DTU_PER_MS+999)/1000;}

#define MUL_REFERENCE_LOSS_MS MUL_SOURCE_TIMEOUT_MS

static void logCompletedTx(uint64_t actual)
{
  // Compare against the bytes passed to dwSetData(), not just the scheduling
  // calculation. Retain both values in the log to expose any encoding mismatch.
  uint64_t packetTimestamp = mulDecodeTimestamp(&txFrame[MUL_TX_TIMESTAMP_OFFSET]);
  int64_t difference = mulTimestampDifference(actual, packetTimestamp);
  uint64_t magnitude = (uint64_t)(difference < 0 ? -difference : difference);
  // newlib-nano here has no 64-bit printf formats. Preserve the full signed
  // 40-bit difference as decimal rather than truncating it to a 32-bit long.
  char delta[14];
  char *end = delta + sizeof(delta) - 1;
  *end = '\0';
  do {
    *--end = '0' + magnitude % 10;
    magnitude /= 10;
  } while (magnitude != 0);
  if (difference < 0) {
    *--end = '-';
  }
  printf("MUL ALR: TX role=%s src=0x%02X seq=%" PRIu32 " scheduled=0x%02X%08" PRIX32
         " packet=0x%02X%08" PRIX32
         " actual=0x%02X%08" PRIX32 " delta=%s\r\n",
         alrRole == mulAlrReference ? "REF" : "FOLLOWER",
         (unsigned int)source[0], pendingSequence,
         (unsigned int)(pendingTimestamp >> 32), (uint32_t)pendingTimestamp,
         (unsigned int)(packetTimestamp >> 32), (uint32_t)packetTimestamp,
         (unsigned int)(actual >> 32), (uint32_t)actual, end);
}

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
 if(age<0||age>(int64_t)(15*MUL_DTU_PER_MS))return false;
 *arrival=mulTimestampMask(rx.full);*packet=decoded.packet;packet->slotMs=schedule->slotMs;
 if(slot)*slot=decoded.slot;
 return true;
}

static void mulAlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  activePlan=config->mulPlan;servedOperations=0;
  dwIdle(dev);
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
  printf("MUL ALR: Mode initialized role=%s src=0x%02X slot_ms=%u\r\n",
         alrRole == mulAlrReference ? "REF" : alrRole == mulAlrFollower ? "FOLLOWER" : "UNSUPPORTED",
         (unsigned int)source[0], (unsigned int)mulNetworkSlotMs(alrNetwork));
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
    printf("MUL ALR: DX_TIME mismatch seq=%" PRIu32 "\r\n", pendingSequence);
    return false;
  }
  pendingTimestamp = mulTimestampMask(programmed.full + txAntennaDelay);
  (void)mulEncodeScheduledPacket(txFrame,source,pendingSequence,pendingTimestamp,mulNetworkSlotMs(alrNetwork));
  unsigned int length=MUL_TEST_FRAME_LENGTH;
  if(multiMode)length=activePlan?mulPlanBroadcastEncode(txFrame,sizeof(txFrame),activePlan,0,pendingSequence,pendingTimestamp,NULL):mulMultiBroadcastEncode(txFrame,sizeof(txFrame),schedule,0,pendingSequence,pendingTimestamp,NULL);
  if (alrRole==mulAlrFollower) {
    mulClockDelay_t delay;
    bool supplied=selectedDelay((uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,&delay);
    mulRelation_t relation=mulRelationPublishSourceNetwork(alrNetwork,&followerClock,supplied ? &delay : NULL,
      pendingTimestamp,readHardwareTime(dev),(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,mulGeometrySource()==mulDelayTwr&&mulTwrDelayPowerCorrected(&ranging),mulGeometrySource());
    if (relation.version==2 && !supplied&&mulGeometrySource()==mulDelayTwr)
      relation.delayState=mulTwrPropagationState(&ranging,&followerClock,(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS);
    mulTestPacket_t packet={.sequence=pendingSequence,.txTimestamp=pendingTimestamp,.slotMs=mulNetworkSlotMs(alrNetwork)}; memcpy(packet.source,source,8);
    if(multiMode)length=activePlan?mulPlanBroadcastEncode(txFrame,sizeof(txFrame),activePlan,localSlot,pendingSequence,pendingTimestamp,&relation):mulMultiBroadcastEncode(txFrame,sizeof(txFrame),schedule,localSlot,pendingSequence,pendingTimestamp,&relation);
    else {mulRelationEncode(txFrame,&packet,&relation);length=MUL_RELATION_FRAME_LENGTH;}
  }
  if(!length){dwIdle(dev);printf("MUL ALR: invalid TX payload; skipped\r\n");return false;}
  dwSetData(dev, txFrame, length);

  // Check again after SPI setup, before arming. Leave room for preamble/power-up.
  if (mulTimestampDifference(rawTx.full, readHardwareTime(dev)) <
      (int64_t)MUL_TX_MIN_LEAD_DTU) {
    dwIdle(dev);
    printf("MUL ALR: Skipped late TX seq=%" PRIu32 "\r\n", pendingSequence);
    return false;
  }
  pendingRawTx = rawTx.full;
  flags.tx = true;
  dwStartTransmit(dev);
  if (dwSpiRead16(dev, SYS_STATUS, 3) & MUL_TX_ERROR_STATUS) {
    dwIdle(dev);
    clearTxErrors(dev);
    flags.tx = false;
    printf("MUL ALR: Delayed TX failed seq=%" PRIu32 "\r\n", pendingSequence);
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
    printf("MUL ALR: Skipped late TX\r\n");
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

typedef struct {uint64_t start,poll,end;bool compensated;} mulLocalReservation_t;
static bool mapReservation(const mulRangingOperation_t *op,uint32_t now,mulLocalReservation_t *out) {
 mulClockDelay_t delay;bool supplied=selectedDelay((uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,&delay);
 uint32_t offsets[3]={op->startUs,op->pollUs,op->endUs};uint64_t targets[3];
 bool compensated=supplied&&mulClockDelayStatusNetwork(alrNetwork,&followerClock,&delay,now)==mulClockDelayValid;
 for(unsigned i=0;i<3;i++) {
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
 *out=(mulLocalReservation_t){.start=targets[0],.poll=targets[1],.end=targets[2],.compensated=compensated};return true;
}
static bool plannedFollowerOperation(dwDevice_t *dev,uwbEvent_t event,mulAlrRole_t *role) {
 uint64_t now=readHardwareTime(dev);uint32_t ms=(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;
 unsigned count=mulRangingPlanCount(activePlan,refSequence());
 for(unsigned index=0;index<count;index++) {
  mulRangingOperation_t op;(void)mulRangingPlanOperation(activePlan,refSequence(),index,&op);
  if(op.kind==MUL_PLAN_AA&&op.responder==source[0]) {
   int64_t phase=mulTimestampDifference(now,refRx());
   if(phase>=(int64_t)((uint64_t)op.startUs*MUL_DTU_PER_MS/1000)&&phase<(int64_t)((uint64_t)op.endUs*MUL_DTU_PER_MS/1000)) {
    if(ranging.active&&(ranging.id!=refSequence()||ranging.operation!=index))mulTwrCancel(&ranging,dev,"reservation_turnaround",false);
    *role=mulAlrFollower;return mulTwrConfigurePlan(&ranging,activePlan,refSequence(),index);
   }
  }else if(op.kind==MUL_PLAN_AR&&op.initiator==source[0]) {
   mulLocalReservation_t local;if(!mapReservation(&op,ms,&local))continue;
   if(mulTimestampDifference(now,local.end)>=0){servedOperations|=UINT32_C(1)<<index;continue;}
   if(mulTimestampDifference(now,local.start)<0)continue;
   *role=mulAlrReference;
   if(ranging.active&&(ranging.id!=refSequence()||ranging.operation!=index))mulTwrCancel(&ranging,dev,"reservation_turnaround",false);
   uint32_t bit=UINT32_C(1)<<index;
   if(event==eventTimeout&&!(servedOperations&bit)) {
    servedOperations|=bit;
    if(mulTwrConfigurePlan(&ranging,activePlan,refSequence(),index))
      mulTwrStartTargets(&ranging,dev,refSequence(),local.poll,local.end);
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
  if(op.kind==MUL_PLAN_AA&&op.responder==source[0]) {
   if(ranging.planned&&ranging.attempted&&ranging.id==refSequence()&&ranging.operation>=index)continue;
   uint64_t end=mulTimestampMask(refRx()+(uint64_t)op.endUs*MUL_DTU_PER_MS/1000);
   if(mulTimestampDifference(now,end)>=0)continue;
   start=mulTimestampMask(refRx()+(uint64_t)op.startUs*MUL_DTU_PER_MS/1000);
  }else if(op.kind==MUL_PLAN_AR&&op.initiator==source[0]&&!(servedOperations&(UINT32_C(1)<<index))) {
   mulLocalReservation_t local;if(!mapReservation(&op,ms,&local)||mulTimestampDifference(now,local.end)>=0)continue;start=local.start;
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
  if(activePlan&&!flags.tx)wait=plannedFollowerWait(dev,wait);
  wait = mulTwrDelayWait(&ranging, nowMs, wait);
  return mulTwrWait(&ranging, dev, refRx(), wait);
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
  int64_t lead = mulTimestampDifference(rawTx.full, readHardwareTime(dev));
  if (lead < (int64_t)MUL_TX_MIN_LEAD_DTU ||
      lead > (int64_t)(offset*MUL_DTU_PER_MS + MUL_TX_ALIGNMENT)) {
    // Never move a missed slot forward, retry its sequence, or extrapolate a frame.
    printf("MUL ALR: SLOT skipped ref_seq=%" PRIu32 " reason=late_or_stale\r\n",
           refSequence());
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
    referenceAcquired = false;
    mulClockReset(&followerClock);
    mulTwrInvalidateAa(&ranging);mulTwrCancel(&ranging, dev, "reference_lost", true);
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
      mulRxRearm(dev);
      mulRxDiagBeacon(actual.full);
      logCompletedTx(actual.full);
    } else {
      // RX cannot legitimately deliver another frame while this delayed TX waits
      // in idle. An unexpected RX event/error cancels it; do not re-arm it.
      abortFollowerTx(dev);
      mulRxRearm(dev);
      printf("MUL ALR: SLOT aborted ref_seq=%" PRIu32 " reason=%s\r\n",
             refSequence(), event == eventTimeout ? "tx_timeout" : "radio_event");
    }
    return followerWait(dev);
  }

  bool selected=true;mulAlrRole_t exchangeRole=mulAlrFollower;
  if(multiMode&&referenceAcquired) {
    selected=false;
    if(activePlan) {
      if(ranging.active&&event==eventPacketReceived&&(dwGetDataLength(dev)==40||dwGetDataLength(dev)==80))mulTwrCancel(&ranging,dev,NULL,false);
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
    printf("MUL ALR: slot_mismatch expected=%u advertised=%u; slot skipped\r\n",mulNetworkSlotMs(alrNetwork),packet.slotMs);
    valid=false;referenceAcquired=false;mulClockReset(&followerClock);
    mulTwrInvalidateAa(&ranging);mulTwrCancel(&ranging,dev,"slot_mismatch",true);
  }
  bool fresh = false;
  bool acquiredNow = false;
  bool armed = false;
  mulClockResult_t clockResult=mulClockIgnored;
  if (valid && mulNetworkRole(alrNetwork,packet.source) == mulAlrReference) {
    // Preserve Stage 3A.1's restart policy after loss, without accepting replays
    // of the last sequence. Epoch identification is deliberately not added here.
    mulSequenceResult_t result;
    if(multiMode) {
      if(!referenceAcquired&&referenceState.multi.hasSequence&&packet.sequence!=refSequence())referenceState.multi.hasSequence=false;
      uint32_t delta=packet.sequence-refSequence();
      result=!referenceState.multi.hasSequence?mulSequenceFirst:!delta?mulSequenceDuplicate:delta>=UINT32_C(0x80000000)?mulSequenceOld:mulSequenceForward;
      if(result!=mulSequenceDuplicate&&result!=mulSequenceOld) {
        referenceState.multi.hasSequence=true;referenceState.multi.sequence=packet.sequence;
        referenceState.multi.rxTimestamp=arrival;referenceState.multi.txTimestamp=packet.txTimestamp;
      }
    }else {
      if(!referenceAcquired&&followerReference.hasSequence&&packet.sequence!=followerReference.sequence)followerReference.hasSequence=false;
      result=mulObservePacket(&followerReference,&packet,arrival);
    }
    if (result != mulSequenceDuplicate && result != mulSequenceOld) {
      fresh = true;servedOperations=0;
      acquiredNow = !referenceAcquired;
      referenceAcquired = true;
      lastReferenceTick = now;
      // Build metadata from THIS reference observation before encoding TX.
      // Bounded integer estimation does not steer the hardware slot.
      mulClockWorkspace_t *workspace=mulClockWorkspace();mulClockDelay_t delay;
      bool supplied=selectedDelay((uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,&delay);
      clockResult=mulClockObserveFitted(alrNetwork,&followerClock,&workspace->fit,&workspace->config,&packet,arrival,(uint32_t)now*portTICK_PERIOD_MS,true,supplied?&delay:NULL);
      if (clockResult==mulClockDiscontinuity || acquiredNow) {
        mulTwrInvalidateAa(&ranging);mulTwrCancel(&ranging,dev,"clock_epoch",true); ranging.attempted=false;
      }
      armed = scheduleFollowerTx(dev, arrival);
    }
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
  if (lostNow) {
    printf("MUL ALR: REF_LOST src=0x%02X state=SEARCHING\r\n",alrNetwork->addresses[0][0]);
    printf("MUL ALR: CLOCK src=0x%02X samples=0 state=SEARCHING\r\n",alrNetwork->addresses[0][0]);
    printf("MUL ALR: MAP src=0x%02X state=INVALID sync=UNCALIBRATED bound_ns=UNKNOWN\r\n",alrNetwork->addresses[0][0]);
  }
  if (fresh) {
    // All logging stays after the hardware slot is armed/re-armed.
    if (acquiredNow) {
      printf("MUL ALR: REF_ACQUIRED src=0x%02X state=ACQUIRED\r\n",alrNetwork->addresses[0][0]);
    }
    printf("MUL ALR: REF_RX src=0x%02X seq=%" PRIu32
           " tx=0x%02X%08" PRIX32 " rx=0x%02X%08" PRIX32
           " state=%s slot_ms=%u\r\n",
           alrNetwork->addresses[0][0],packet.sequence, (unsigned int)(packet.txTimestamp >> 32),
           (uint32_t)packet.txTimestamp, (unsigned int)(arrival >> 32), (uint32_t)arrival,
           armed ? "TX_PENDING" : "ACQUIRED_SLOT_SKIPPED", (unsigned int)mulNetworkSlotMs(alrNetwork));
    const mulClockWorkspace_t *workspace=mulClockWorkspace();
    if(workspace->config.estimator)printf("MUL KF: h=%u n=%u f=%ld b=%ld v=%lu rej=%lu s=%lu c=%u r=%u use=%u fb=%u m=%u a=%lu rst=%lu rr=%u e=%08lX%08lX bound=UNKNOWN\r\n",
      workspace->config.window,workspace->fit.count,(long)workspace->fit.skewPpb,(long)followerClock.skewPpb,
      (unsigned long)workspace->fit.rmsDtu,(unsigned long)workspace->fit.rejected,(unsigned long)workspace->fit.uncertaintyPpb,workspace->fit.compensated,workspace->fit.ready,followerClock.status==mulClockValid?(workspace->fit.applied?2:0):255,workspace->fit.fallback,
      mulClockMapStatus(&followerClock,(uint32_t)now*portTICK_PERIOD_MS),(unsigned long)((uint32_t)now*portTICK_PERIOD_MS-followerClock.observedMs),(unsigned long)workspace->fit.resets,workspace->fit.resetReason,(unsigned long)(followerClock.epoch>>32),(unsigned long)followerClock.epoch);
    if(!workspace->config.estimator) {
    int32_t ppb = followerClock.skewPpb;
    uint32_t magnitude = (uint32_t)(ppb < 0 ? -ppb : ppb);
    printf("MUL ALR: CLOCK src=0x%02X seq=%" PRIu32 " skew_ppm=%s%" PRIu32 ".%03" PRIu32
           " samples=%" PRIu32 " state=%s result=%s\r\n",alrNetwork->addresses[0][0],packet.sequence,
           ppb < 0 ? "-" : "+", magnitude / 1000, magnitude % 1000, followerClock.samples,
           followerClock.status == mulClockValid ? "VALID" : "WARMUP",
           clockResult == mulClockDiscontinuity ? "RESET" :
           clockResult == mulClockSample ? "ACCEPTED" : "BASELINE");
    mulClockMapping_t mapping;
    uint32_t nowMs = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool mapValid = mulClockMapRefToFollower(&followerClock, packet.txTimestamp, nowMs, &mapping);
    mulClockMapStatus_t mapState = mulClockMapStatus(&followerClock, nowMs);
    printf("MUL ALR: MAP src=0x%02X seq=%" PRIu32 " slope_ppm=%s%" PRIu32 ".%03" PRIu32
           " state=%s sync=UNCALIBRATED age_ms=%" PRIu32
           " horizon_ms=%u bound_ns=UNKNOWN\r\n",alrNetwork->addresses[0][0],packet.sequence,
           ppb < 0 ? "-" : "+", magnitude / 1000, magnitude % 1000,
           mapValid ? "VALID" : mapState == mulClockMapStale ? "STALE" :
           mapState == mulClockMapWarmup ? "WARMUP" : "INVALID",
           nowMs - followerClock.observedMs, MUL_CLOCK_MAP_HORIZON_MS);
  }
  }
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
      mulTwrConfigurePlan(&ranging,activePlan,pendingSequence,index))mulTwrStart(&ranging,dev,pendingSequence,pendingTimestamp);
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
          if(!mulNetworkScheduleMatches(alrNetwork,packet.slotMs))
            printf("MUL ALR: slot_mismatch expected=%u advertised=%u\r\n",mulNetworkSlotMs(alrNetwork),packet.slotMs);
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
      printf("MUL ALR: TX timeout seq=%" PRIu32 "\r\n", pendingSequence);
      break;
    case eventPacketSent:
      if (flags.tx) {
        dwTime_t actual = {.full = 0};
        dwGetTransmitTimestamp(dev, &actual);
        actual.full = mulTimestampMask(actual.full);
        flags.tx = false;
        mulRxRearm(dev);
        mulRxDiagBeacon(actual.full);
        logCompletedTx(actual.full);
      }
      break;
    case eventReceiveTimeout:
    case eventReceiveFailed:
      dwIdle(dev);
      clearTxErrors(dev);
      flags.tx = false;
      mulRxRearm(dev);
      printf("MUL ALR: RX %s outside_TWR recovery=rearm\r\n",
             event == eventReceiveTimeout ? "timeout" : "failure");
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
