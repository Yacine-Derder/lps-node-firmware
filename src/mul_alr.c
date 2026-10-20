/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_alr.h"
#include "mul_common.h"
#include "mul_clock.h"
#include "mul_rx.h"
#include "mul_twr.h"
#include "mul_relation.h"
#include "mul_role_state.h"
#include "mul_timing.h"
#include "task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// DW1000 time units: 499.2 MHz * 128. These are not STM32 milliseconds.
#define MUL_DTU_PER_MS UINT64_C(63897600)
#define MUL_TX_PERIOD_DTU (1000 * MUL_DTU_PER_MS)
#define MUL_TX_LEAD_DTU (20 * MUL_DTU_PER_MS)
#define MUL_TX_MIN_LEAD_DTU (5 * MUL_DTU_PER_MS)
#define MUL_TX_COMPLETION_MS 100
#define MUL_TX_ALIGNMENT UINT64_C(512)

// SYS_STATUS bits 27 (HPDWARN) and 34 (TXPUTE), read at byte offset 3.
// Match Decawave dwt_starttx()'s delayed-start check. libdw1000 returns void.
#define MUL_TX_ERROR_STATUS UINT16_C(0x0408)

static uint8_t source[MUL_ADDRESS_LENGTH];
// dwSetData() reads length + 2 bytes when hardware FCS is enabled.
static uint8_t txFrame[MUL_RELATION_FRAME_LENGTH + 2];
static uint32_t nextSequence;
static uint32_t pendingSequence;
static uint64_t nextRawTx;
static uint64_t pendingTimestamp;
static uint64_t pendingRawTx;
static bool txPending;
static mulAlrRole_t alrRole = mulAlrUnsupported;
static mulSourceState_t followerReference;
static bool referenceAcquired;
static TickType_t lastReferenceTick;
static mulClock_t followerClock;
#define ranging mulRoleScratch.twr
static bool followerSeen;

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

static void mulAlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  dwIdle(dev);
  dwReceivePermanently(dev, false);
  memcpy(source, config->address, sizeof(source));
  nextSequence = 0;
  txPending = false;
  alrRole = mulAlrRoleForAddress(config->address);
  memset(&followerReference, 0, sizeof(followerReference));
  referenceAcquired = false;
  mulClockReset(&followerClock);
  mulTwrInit(&ranging);
  followerSeen = false;
  printf("MUL ALR: Mode initialized role=%s src=0x%02X slot_ms=%u\r\n",
         alrRole == mulAlrReference ? "REF" : alrRole == mulAlrFollower ? "FOLLOWER" : "UNSUPPORTED",
         (unsigned int)source[0], (unsigned int)MUL_FOLLOWER_SLOT_MS);
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

// Shared, tested Stage 2 TX setup. The caller supplies a raw local-clock target
// and a live TX_ANTD reading; no remote timestamp is used for hardware scheduling.
static bool startDelayedTx(dwDevice_t *dev, dwTime_t rawTx, uint16_t txAntennaDelay)
{
  pendingSequence = nextSequence++;
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
  mulEncodeTestPacket(txFrame, source, pendingSequence, pendingTimestamp);
  unsigned int length=MUL_TEST_FRAME_LENGTH;
  if (alrRole==mulAlrFollower) {
    mulClockDelay_t delay;
    bool supplied=mulTwrDelay(&ranging,&followerClock,&delay);
    mulRelation_t relation=mulRelationPublish(&followerClock,supplied ? &delay : NULL,
      pendingTimestamp,readHardwareTime(dev),(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS,ranging.powerCorrected);
    if (relation.version==2 && !supplied)
      relation.delayState=mulTwrPropagationState(&ranging,&followerClock,(uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS);
    mulTestPacket_t packet={.sequence=pendingSequence,.txTimestamp=pendingTimestamp}; memcpy(packet.source,source,8);
    mulRelationEncode(txFrame,&packet,&relation); length=MUL_RELATION_FRAME_LENGTH;
  }
  dwSetData(dev, txFrame, length);

  // Check again after SPI setup, before arming. Leave room for preamble/power-up.
  if (mulTimestampDifference(rawTx.full, readHardwareTime(dev)) <
      (int64_t)MUL_TX_MIN_LEAD_DTU) {
    dwIdle(dev);
    printf("MUL ALR: Skipped late TX seq=%" PRIu32 "\r\n", pendingSequence);
    return false;
  }
  pendingRawTx = rawTx.full;
  txPending = true;
  dwStartTransmit(dev);
  if (dwSpiRead16(dev, SYS_STATUS, 3) & MUL_TX_ERROR_STATUS) {
    dwIdle(dev);
    clearTxErrors(dev);
    txPending = false;
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
  txPending = false;
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
  if (txPending) {
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
  wait = mulTwrDelayWait(&ranging, nowMs, wait);
  return mulTwrWait(&ranging, dev, followerReference.rxTimestamp, wait);
}

static bool scheduleFollowerTx(dwDevice_t *dev, uint64_t localRx)
{
  uint16_t antennaDelay = dwSpiRead16(dev, TX_ANTD, NO_SUB);
  // Aim for antenna-referenced TX at RX + the configured slot, in this follower's clock.
  // Subtract TX_ANTD before aligning the raw DX_TIME upwards to 512 ticks.
  uint64_t rawTarget = mulTimestampMask(localRx + MUL_FOLLOWER_SLOT_DTU - antennaDelay);
  dwTime_t rawTx = {.full = mulTimestampMask(
    (rawTarget + MUL_TX_ALIGNMENT - 1) & ~(MUL_TX_ALIGNMENT - 1))};
  int64_t lead = mulTimestampDifference(rawTx.full, readHardwareTime(dev));
  if (lead < (int64_t)MUL_TX_MIN_LEAD_DTU ||
      lead > (int64_t)(MUL_FOLLOWER_SLOT_DTU + MUL_TX_ALIGNMENT)) {
    // Never move a missed slot forward, retry its sequence, or extrapolate a frame.
    printf("MUL ALR: SLOT skipped ref_seq=%" PRIu32 " reason=late_or_stale\r\n",
           followerReference.sequence);
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
    mulTwrCancel(&ranging, dev, "reference_lost", true);
    ranging.attempted = false;
    if (txPending) {
      abortFollowerTx(dev);
    }
  }

  if (txPending) {
    if (event == eventPacketReceived &&
        (dwGetDataLength(dev) == MUL_TWR_FRAME_LENGTH || dwGetDataLength(dev) == 68)) {
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
      txPending = false;
      mulRxRearm(dev);
      logCompletedTx(actual.full);
    } else {
      // RX cannot legitimately deliver another frame while this delayed TX waits
      // in idle. An unexpected RX event/error cancels it; do not re-arm it.
      abortFollowerTx(dev);
      mulRxRearm(dev);
      printf("MUL ALR: SLOT aborted ref_seq=%" PRIu32 " reason=%s\r\n",
             followerReference.sequence, event == eventTimeout ? "tx_timeout" : "radio_event");
    }
    return followerWait(dev);
  }

  if (referenceAcquired && mulTwrOnEvent(&ranging, dev, event, mulAlrFollower,
      followerReference.sequence, followerReference.rxTimestamp, followerReference.txTimestamp,
      followerClock.epoch, (uint32_t)now * portTICK_PERIOD_MS)) {
    return followerWait(dev);
  }

  mulTestPacket_t packet;
  uint64_t arrival = 0;
  bool valid = event == eventPacketReceived && mulRxRead(dev, &packet, &arrival);
  bool fresh = false;
  bool acquiredNow = false;
  bool armed = false;
  mulClockResult_t clockResult=mulClockIgnored;
  if (valid && mulAlrRoleForAddress(packet.source) == mulAlrReference) {
    // Preserve Stage 3A.1's restart policy after loss, without accepting replays
    // of the last sequence. Epoch identification is deliberately not added here.
    if (!referenceAcquired && followerReference.hasSequence &&
        packet.sequence != followerReference.sequence) {
      followerReference.hasSequence = false;
    }
    mulSequenceResult_t result = mulObservePacket(&followerReference, &packet, arrival);
    if (result != mulSequenceDuplicate && result != mulSequenceOld) {
      fresh = true;
      acquiredNow = !referenceAcquired;
      referenceAcquired = true;
      lastReferenceTick = now;
      // Build metadata from THIS reference observation before encoding TX.
      // Bounded integer estimation does not steer the hardware slot.
      clockResult=mulClockObserve(&followerClock,&packet,arrival,(uint32_t)now*portTICK_PERIOD_MS,true);
      if (clockResult==mulClockDiscontinuity || acquiredNow) {
        mulTwrCancel(&ranging,dev,"clock_epoch",true); ranging.attempted=false;
      }
      armed = scheduleFollowerTx(dev, arrival);
    }
  }
  // Turn directly from the completed RX into delayed TX. Enabling RX briefly
  // before dwNewTransmit() could leave a new RX IRQ racing the pending slot.
  if (!armed) {
    mulRxRearm(dev);
  }
  if (lostNow) {
    printf("MUL ALR: REF_LOST src=0x84 state=SEARCHING\r\n");
    printf("MUL ALR: CLOCK src=0x84 samples=0 state=SEARCHING\r\n");
    printf("MUL ALR: MAP src=0x84 state=INVALID sync=UNCALIBRATED bound_ns=UNKNOWN\r\n");
  }
  if (fresh) {
    // All logging stays after the hardware slot is armed/re-armed.
    if (acquiredNow) {
      printf("MUL ALR: REF_ACQUIRED src=0x84 state=ACQUIRED\r\n");
    }
    printf("MUL ALR: REF_RX src=0x84 seq=%" PRIu32
           " tx=0x%02X%08" PRIX32 " rx=0x%02X%08" PRIX32
           " state=%s slot_ms=%u\r\n",
           packet.sequence, (unsigned int)(packet.txTimestamp >> 32),
           (uint32_t)packet.txTimestamp, (unsigned int)(arrival >> 32), (uint32_t)arrival,
           armed ? "TX_PENDING" : "ACQUIRED_SLOT_SKIPPED", (unsigned int)MUL_FOLLOWER_SLOT_MS);
    int32_t ppb = followerClock.skewPpb;
    uint32_t magnitude = (uint32_t)(ppb < 0 ? -ppb : ppb);
    printf("MUL ALR: CLOCK src=0x84 seq=%" PRIu32 " skew_ppm=%s%" PRIu32 ".%03" PRIu32
           " samples=%" PRIu32 " state=%s result=%s\r\n", packet.sequence,
           ppb < 0 ? "-" : "+", magnitude / 1000, magnitude % 1000, followerClock.samples,
           followerClock.status == mulClockValid ? "VALID" : "WARMUP",
           clockResult == mulClockDiscontinuity ? "RESET" :
           clockResult == mulClockSample ? "ACCEPTED" : "BASELINE");
    mulClockMapping_t mapping;
    uint32_t nowMs = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool mapValid = mulClockMapRefToFollower(&followerClock, packet.txTimestamp, nowMs, &mapping);
    mulClockMapStatus_t mapState = mulClockMapStatus(&followerClock, nowMs);
    printf("MUL ALR: MAP src=0x84 seq=%" PRIu32 " slope_ppm=%s%" PRIu32 ".%03" PRIu32
           " state=%s sync=UNCALIBRATED age_ms=%" PRIu32
           " horizon_ms=%u bound_ns=UNKNOWN\r\n", packet.sequence,
           ppb < 0 ? "-" : "+", magnitude / 1000, magnitude % 1000,
           mapValid ? "VALID" : mapState == mulClockMapStale ? "STALE" :
           mapState == mulClockMapWarmup ? "WARMUP" : "INVALID",
           nowMs - followerClock.observedMs, MUL_CLOCK_MAP_HORIZON_MS);
  }
  return followerWait(dev);
}

static uint32_t referenceWait(dwDevice_t *dev)
{
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
  if (alrRole == mulAlrFollower) {
    return followerOnEvent(dev, event);
  }
  if (alrRole != mulAlrReference) {
    dwIdle(dev);
    return MAX_TIMEOUT;
  }
  if (!txPending) {
    // A delayed or stale IRQ cannot retain TWR ownership across the next mandatory
    // broadcast preparation deadline. Hardware time always wins over ranging.
    int64_t remaining = mulTimestampDifference(nextRawTx, readHardwareTime(dev));
    if (remaining <= (int64_t)(MUL_TX_LEAD_DTU + MUL_TX_ALIGNMENT) ||
        remaining > (int64_t)(MUL_TX_PERIOD_DTU + MUL_TX_LEAD_DTU)) {
      mulTwrCancel(&ranging, dev, NULL, false);
      if (event == eventTimeout) return scheduleTx(dev);
    } else {
      if (mulTwrOnEvent(&ranging, dev, event, mulAlrReference, pendingSequence,
          pendingTimestamp, pendingTimestamp, 0, (uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS)) {
        return referenceWait(dev);
      }
      if (event == eventPacketReceived) {
        mulTestPacket_t packet;
        uint64_t arrival;
        if (mulRxRead(dev, &packet, &arrival) && mulAlrRoleForAddress(packet.source) == mulAlrFollower) {
          int64_t phase = mulTimestampDifference(arrival, pendingTimestamp);
          if (phase >= (int64_t)(MUL_SLOT_MIN_DTU) && phase <= (int64_t)(MUL_SLOT_PRESENCE_MAX_MS*MUL_DTU_PER_MS))
            followerSeen = true;
        }
        mulRxRearm(dev);
        return referenceWait(dev);
      }
      if (event == eventTimeout) {
        int64_t phase = mulTimestampDifference(readHardwareTime(dev), pendingTimestamp);
        if (followerSeen && mulTwrDue(pendingSequence) && phase >= (int64_t)(MUL_TWR_PREPARE_MS*MUL_DTU_PER_MS) &&
            !(ranging.attempted && ranging.id == pendingSequence)) {
          mulTwrStart(&ranging, dev, pendingSequence, pendingTimestamp);
        }
        return referenceWait(dev);
      }
    }
  }
  switch (event) {
    case eventTimeout:
      if (!txPending) {
        return scheduleTx(dev);
      }
      dwIdle(dev);
      clearTxErrors(dev);
      txPending = false;
      printf("MUL ALR: TX timeout seq=%" PRIu32 "\r\n", pendingSequence);
      break;
    case eventPacketSent:
      if (txPending) {
        dwTime_t actual = {.full = 0};
        dwGetTransmitTimestamp(dev, &actual);
        actual.full = mulTimestampMask(actual.full);
        txPending = false;
        mulRxRearm(dev);
        logCompletedTx(actual.full);
      }
      break;
    case eventReceiveTimeout:
    case eventReceiveFailed:
      dwIdle(dev);
      clearTxErrors(dev);
      txPending = false;
      mulRxRearm(dev);
      printf("MUL ALR: RX %s outside_TWR recovery=rearm\r\n",
             event == eventReceiveTimeout ? "timeout" : "failure");
      break;
    case eventPacketReceived:
      if (txPending) {
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
