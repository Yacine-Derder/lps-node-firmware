/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_twr.h"
#include "mul_rx.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const uint8_t addresses[2][8] = {
  {0x84,0,0,0,0,0,0xcf,0xbc}, {0x85,0,0,0,0,0,0xcf,0xbc},
};

static mulAlrRole_t sender(mulTwrType_t type)
{
  return type == mulTwrResponse || type == mulTwrReport ? mulAlrFollower : mulAlrReference;
}

void mulTwrEncode(uint8_t frame[MUL_TWR_FRAME_LENGTH], const mulTwrPacket_t *packet)
{
  mulAlrRole_t role = sender(packet->type);
  memset(frame, 0, MUL_TWR_FRAME_LENGTH);
  mulEncodeTestPacket(frame, addresses[role], packet->id, packet->stamps[0]);
  memcpy(frame + 5, addresses[1-role], 8);
  frame[24] = packet->type;
  for (unsigned int i = 0; i < 6; i++) {
    mulEncodeTimestamp(frame + 29 + 5*i, packet->stamps[i]);
  }
  mulEncodeTimestamp(frame + 59, packet->txTimestamp);
  for (unsigned int i = 0; i < 4; i++) {
    frame[64+i] = packet->tof >> (8*i);
  }
}

bool mulTwrDecode(const uint8_t *frame, size_t length, mulTwrPacket_t *packet)
{
  if (frame == NULL || packet == NULL || length != MUL_TWR_FRAME_LENGTH ||
      frame[0] != 0x41 || frame[1] != 0xdc || frame[3] != 0xcf || frame[4] != 0xbc ||
      memcmp(frame+21, "MUL", 3) || frame[24] < mulTwrPoll || frame[24] > mulTwrResult ||
      frame[2] != frame[25]) {
    return false;
  }
  mulAlrRole_t role = sender(frame[24]);
  if (memcmp(frame+13, addresses[role], 8) || memcmp(frame+5, addresses[1-role], 8)) {
    return false;
  }
  mulTwrPacket_t result = {.type = frame[24]};
  for (unsigned int i = 0; i < 4; i++) {
    result.id |= (uint32_t)frame[25+i] << (8*i);
    result.tof |= (uint32_t)frame[64+i] << (8*i);
  }
  for (unsigned int i = 0; i < 6; i++) {
    result.stamps[i] = mulDecodeTimestamp(frame + 29 + 5*i);
  }
  result.txTimestamp = mulDecodeTimestamp(frame+59);
  unsigned int used = result.type == mulTwrPoll ? 1 : result.type == mulTwrResponse ? 3 :
                      result.type == mulTwrFinal ? 5 : 6;
  for (unsigned int i = used; i < 6; i++) {
    if (result.stamps[i] != 0) return false;
  }
  if ((used < 6 && result.txTimestamp != result.stamps[used-1]) ||
      (result.type != mulTwrResult && result.tof != 0) || result.tof > MUL_CLOCK_DELAY_MAX_DTU) {
    return false;
  }
  *packet = result;
  return true;
}

static bool interval(uint64_t a, uint64_t b)
{
  int64_t delta = mulTimestampDifference(a, b);
  return delta >= (int64_t)(15*MUL_TWR_DTU_PER_MS) && delta <= (int64_t)(30*MUL_TWR_DTU_PER_MS);
}

bool mulTwrCalculate(const uint64_t t[6], uint32_t *tof)
{
  if (t == NULL || tof == NULL || !interval(t[3],t[0]) || !interval(t[4],t[3]) ||
      !interval(t[2],t[1]) || !interval(t[5],t[2])) return false;
  uint64_t ra = (uint64_t)mulTimestampDifference(t[3],t[0]);
  uint64_t da = (uint64_t)mulTimestampDifference(t[4],t[3]);
  uint64_t db = (uint64_t)mulTimestampDifference(t[2],t[1]);
  uint64_t rb = (uint64_t)mulTimestampDifference(t[5],t[2]);
  int64_t rateDifference = (int64_t)(rb+db) - (int64_t)(ra+da);
  int64_t rateLimit = (int64_t)((ra+da)*MUL_CLOCK_MAX_PPB / UINT64_C(1000000000));
  if (rateDifference < -rateLimit || rateDifference > rateLimit) return false;
  // Asymmetric DS-TWR, expressed explicitly in REFERENCE units. With constant
  // reciprocal delay, (rb+db)/(ra+da) is follower/reference rate. Replacing the
  // nominal mixed-clock denominator with 2*(rb+db) yields reference DTU exactly.
  // Each interval <=30 ms: both products fit uint64 and subtraction is checked.
  uint64_t roundProduct = ra*rb;
  uint64_t replyProduct = da*db;
  if (roundProduct < replyProduct) return false;
  uint64_t denominator = 2*(rb+db);
  uint64_t numerator = roundProduct - replyProduct;
  uint64_t rounded = (numerator + denominator/2) / denominator;
  if (rounded > (uint64_t)MUL_CLOCK_DELAY_MAX_DTU) return false;
  *tof = (uint32_t)rounded;
  return true;
}

static uint64_t hardwareNow(dwDevice_t *dev)
{
  dwTime_t now = {.full=0};
  dwGetSystemTimestamp(dev, &now);
  return mulTimestampMask(now.full);
}

static void clearErrors(dwDevice_t *dev)
{
  const uint8_t errors[] = {0x08,0x04};
  dwSpiWrite(dev, SYS_STATUS, 3, errors, sizeof(errors));
}

void mulTwrInit(mulTwr_t *twr) { memset(twr, 0, sizeof(*twr)); }

bool mulTwrDue(uint32_t sequence) { return sequence != 0 && sequence % 5 == 0; }

void mulTwrCancel(mulTwr_t *twr, dwDevice_t *dev, const char *reason, bool invalidateDelay)
{
  bool wasActive = twr->active;
  twr->active = twr->txPending = false;
  if (invalidateDelay) {
    if (twr->delayValid) twr->syncDirty = true;
    twr->delayValid = false;
  }
  if (wasActive) {
    dwIdle(dev);
    clearErrors(dev);
    mulRxRearm(dev);
    if (reason) printf("MUL TWR: id=%" PRIu32 " result=FAILED reason=%s\r\n", twr->id, reason);
    else twr->deferredCancel = true;
  }
}

void mulTwrFlush(mulTwr_t *twr)
{
  if (twr->deferredCancel) {
    twr->deferredCancel = false;
    printf("MUL TWR: id=%" PRIu32 " result=FAILED reason=broadcast_priority\r\n", twr->id);
  }
}

static bool sendPacket(mulTwr_t *twr, dwDevice_t *dev, mulTwrType_t type, uint64_t target)
{
  if (!mulAntennaValid(dev)) {
    mulTwrCancel(twr, dev, "antenna_config", true);
    return false;
  }
  // Same raw-DX_TIME/live-TX_ANTD contract as Stage 2; never use immediate TX.
  uint16_t antenna = dwSpiRead16(dev, TX_ANTD, NO_SUB);
  dwTime_t raw = {.full=mulTimestampMask((mulTimestampMask(target-antenna)+511) & ~UINT64_C(511))};
  if (mulTimestampDifference(raw.full, hardwareNow(dev)) < (int64_t)(5*MUL_TWR_DTU_PER_MS) ||
      mulTimestampDifference(twr->windowEnd, target) < (int64_t)(20*MUL_TWR_DTU_PER_MS)) {
    mulTwrCancel(twr, dev, "late", false);
    return false;
  }
  dwNewTransmit(dev);
  dwSetDefaults(dev);
  clearErrors(dev);
  dwWaitForResponse(dev, false);
  dwSetTxRxTime(dev, raw);
  dwTime_t verified = {.full=0};
  dwSpiRead(dev, DX_TIME, NO_SUB, verified.raw, LEN_DX_TIME);
  if (verified.full != raw.full) {
    mulTwrCancel(twr, dev, "dx_time", false);
    return false;
  }
  twr->expectedTx = mulTimestampMask(verified.full + antenna);
  if (type == mulTwrPoll) twr->stamps[0] = twr->expectedTx;
  if (type == mulTwrResponse) twr->stamps[2] = twr->expectedTx;
  if (type == mulTwrFinal) twr->stamps[4] = twr->expectedTx;
  mulTwrPacket_t packet = {.type=type, .id=twr->id, .txTimestamp=twr->expectedTx,
                           .tof=type == mulTwrResult ? twr->tof : 0};
  memcpy(packet.stamps, twr->stamps, sizeof(packet.stamps));
  uint8_t frame[MUL_TWR_FRAME_LENGTH+2] = {0}; // libdw1000 reads two FCS reserve bytes.
  mulTwrEncode(frame, &packet);
  dwSetData(dev, frame, MUL_TWR_FRAME_LENGTH);
  if (mulTimestampDifference(raw.full, hardwareNow(dev)) < (int64_t)(5*MUL_TWR_DTU_PER_MS)) {
    mulTwrCancel(twr, dev, "late_setup", false);
    return false;
  }
  twr->stage = type;
  twr->txPending = true;
  twr->deadline = mulTimestampMask(twr->expectedTx + MUL_TWR_STEP_MS*MUL_TWR_DTU_PER_MS);
  dwStartTransmit(dev);
  if (dwSpiRead16(dev, SYS_STATUS, 3) & 0x0408) {
    mulTwrCancel(twr, dev, "delayed_start", false);
    return false;
  }
  return true;
}

bool mulTwrStart(mulTwr_t *twr, dwDevice_t *dev, uint32_t sequence, uint64_t origin)
{
  if (!mulTwrDue(sequence) || twr->active || (twr->attempted && twr->id == sequence)) return false;
  twr->id = sequence;
  twr->attempted = twr->active = true;
  twr->windowEnd = mulTimestampMask(origin+MUL_TWR_END_MS*MUL_TWR_DTU_PER_MS);
  memset(twr->stamps, 0, sizeof(twr->stamps));
  return sendPacket(twr, dev, mulTwrPoll, mulTimestampMask(origin+MUL_TWR_POLL_MS*MUL_TWR_DTU_PER_MS));
}

static void logSuccess(const mulTwr_t *twr, mulAlrRole_t role, dwDevice_t *dev)
{
  // Only after RESULT: no next TWR TX to schedule, >200 ms before broadcast prep.
  // Keep this and OK/SYNC below the 512-byte USB queue for one completion.
  printf("MUL TWR D: id=%" PRIu32 " tx_antd=%u rx_antd=%u source=%s P=",
    twr->id, (unsigned int)dwSpiRead16(dev, TX_ANTD, NO_SUB),
    (unsigned int)dwSpiRead16(dev, LDE_IF, LDE_RXANTD_SUB), MUL_ANTENNA_SOURCE);
  for (unsigned int i=0; i<6; i++) {
    printf("%02X%08" PRIX32 "%s", (unsigned int)(twr->stamps[i]>>32),
      (uint32_t)twr->stamps[i], i == 5 ? "" : ",");
  }
  printf(" I=%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\r\n",
    (uint32_t)mulTimestampDifference(twr->stamps[3],twr->stamps[0]),
    (uint32_t)mulTimestampDifference(twr->stamps[4],twr->stamps[3]),
    (uint32_t)mulTimestampDifference(twr->stamps[5],twr->stamps[2]),
    (uint32_t)mulTimestampDifference(twr->stamps[2],twr->stamps[1]));
  // Vacuum speed of light 299792458 m/s; display integer millimetres, not an accuracy claim.
  uint32_t mm = (uint32_t)(((uint64_t)twr->tof*UINT64_C(299792458000) + UINT64_C(31948800000)) /
                         UINT64_C(63897600000));
  printf("MUL TWR: id=%" PRIu32 " role=%s ref=0x84 fol=0x85 result=OK tof_dtu=%" PRIu32
         " distance_m=%" PRIu32 ".%03" PRIu32 " uncertainty=UNKNOWN\r\n",
         twr->id, role == mulAlrReference ? "INIT" : "RESP", twr->tof, mm/1000, mm%1000);
}

bool mulTwrOnEvent(mulTwr_t *twr, dwDevice_t *dev, uwbEvent_t event, mulAlrRole_t role,
  uint32_t sequence, uint64_t origin, uint64_t referenceOrigin, uint64_t epoch, uint32_t nowMs)
{
  if ((twr->active || (event == eventPacketReceived && dwGetDataLength(dev) == MUL_TWR_FRAME_LENGTH)) &&
      !mulAntennaValid(dev)) {
    if (!twr->active) printf("MUL TWR: result=FAILED reason=antenna_config\r\n");
    mulTwrCancel(twr, dev, "antenna_config", true);
    mulRxRearm(dev);
    return true;
  }
  int64_t phase = mulTimestampDifference(hardwareNow(dev), origin);
  if (twr->active && (phase < 0 || phase >= (int64_t)(MUL_TWR_END_MS*MUL_TWR_DTU_PER_MS) ||
      sequence != twr->id || (role == mulAlrFollower && epoch != twr->epoch))) {
    mulTwrCancel(twr, dev, "window_or_epoch", true);
  }
  if (event == eventPacketReceived && dwGetDataLength(dev) == MUL_TEST_FRAME_LENGTH) {
    mulTwrCancel(twr, dev, NULL, false); // Caller programs/re-arms broadcast radio before logging.
    return false;
  }
  if (!twr->active && (event != eventPacketReceived || dwGetDataLength(dev) != MUL_TWR_FRAME_LENGTH)) return false;
  if (twr->active && mulTimestampDifference(hardwareNow(dev), twr->deadline) >= 0) {
    mulTwrCancel(twr, dev, "timeout", false);
    return true;
  }
  if (event == eventTimeout) return true; // Absolute deadline; early wake cannot extend it.
  if (event == eventPacketSent && twr->txPending) {
    dwTime_t actual = {.full=0};
    dwGetTransmitTimestamp(dev, &actual);
    if (mulTimestampMask(actual.full) != twr->expectedTx) {
      mulTwrCancel(twr, dev, "tx_stamp", false);
      return true;
    }
    twr->txPending = false;
    if (twr->stage == mulTwrResult) {
      twr->active = false;
      mulRxRearm(dev);
      logSuccess(twr, role, dev);
    } else {
      twr->stage++;
      mulRxRearm(dev);
    }
    return true;
  }
  if (event != eventPacketReceived || twr->txPending) {
    mulTwrCancel(twr, dev, "radio_event", false);
    return true;
  }
  uint8_t frame[MUL_TWR_FRAME_LENGTH];
  mulTwrPacket_t packet;
  bool valid = dwGetDataLength(dev) == sizeof(frame);
  if (valid) {
    dwGetData(dev, frame, sizeof(frame));
    valid = mulTwrDecode(frame, sizeof(frame), &packet) && sender(packet.type) != role && packet.id == sequence;
  }
  if (!valid) {
    if (twr->active) mulTwrCancel(twr, dev, "packet", false); else mulRxRearm(dev);
    return true;
  }
  dwTime_t rx = {.full=0};
  // RX_STAMP already includes LDE_RXANTD. Do not apply the driver's optional
  // received-power bias table or the old TWR code's extra hardcoded antenna offset.
  dwGetRawReceiveTimestamp(dev, &rx);
  rx.full = mulTimestampMask(rx.full);
  int64_t captureAge = mulTimestampDifference(hardwareNow(dev), rx.full);
  if (captureAge < 0 || captureAge > (int64_t)(15*MUL_TWR_DTU_PER_MS)) {
    if (twr->active) mulTwrCancel(twr, dev, "rx_stamp", false); else mulRxRearm(dev);
    return true;
  }
  if (packet.type == mulTwrPoll && role == mulAlrFollower && !twr->active) {
    int64_t arrivalPhase = mulTimestampDifference(rx.full, origin);
    int64_t remotePhase = mulTimestampDifference(packet.stamps[0], referenceOrigin);
    if (!mulTwrDue(sequence) || (twr->attempted && twr->id == sequence) ||
        arrivalPhase < (int64_t)(600*MUL_TWR_DTU_PER_MS) || arrivalPhase > (int64_t)(700*MUL_TWR_DTU_PER_MS) ||
        remotePhase < (int64_t)(600*MUL_TWR_DTU_PER_MS) || remotePhase > (int64_t)(700*MUL_TWR_DTU_PER_MS)) {
      mulRxRearm(dev);
      return true;
    }
    memset(twr->stamps, 0, sizeof(twr->stamps));
    twr->id = sequence;
    twr->attempted = twr->active = true;
    twr->delayValid = false; // Never re-date an earlier estimate while a new exchange runs.
    twr->windowEnd = mulTimestampMask(origin+MUL_TWR_END_MS*MUL_TWR_DTU_PER_MS);
    twr->epoch = epoch;
    // Backdate the coarse acquisition time by hardware RX-to-callback latency,
    // rounded upwards. Neither callback delay nor RESULT delivery can re-date it.
    twr->measuredMs = nowMs - (uint32_t)(((uint64_t)captureAge + MUL_TWR_DTU_PER_MS - 1) /
                                        MUL_TWR_DTU_PER_MS);
    twr->stamps[0] = packet.stamps[0];
    twr->stamps[1] = rx.full;
    sendPacket(twr, dev, mulTwrResponse, mulTimestampMask(rx.full+MUL_TWR_REPLY_MS*MUL_TWR_DTU_PER_MS));
    return true;
  }
  unsigned int prefix = packet.type == mulTwrResponse ? 1 : packet.type == mulTwrFinal ? 3 :
                        packet.type == mulTwrReport ? 5 : 6;
  if (!twr->active || packet.type != twr->stage || packet.id != twr->id ||
      memcmp(packet.stamps, twr->stamps, prefix*sizeof(uint64_t))) {
    if (twr->active) mulTwrCancel(twr, dev, "exchange", false); else mulRxRearm(dev);
    return true;
  }
  if (packet.type == mulTwrResponse) {
    if (!interval(packet.stamps[2],packet.stamps[1])) {
      mulTwrCancel(twr, dev, "response_interval", false);
      return true;
    }
    twr->stamps[1] = packet.stamps[1]; twr->stamps[2] = packet.stamps[2]; twr->stamps[3] = rx.full;
    sendPacket(twr, dev, mulTwrFinal, mulTimestampMask(rx.full+MUL_TWR_REPLY_MS*MUL_TWR_DTU_PER_MS));
  } else if (packet.type == mulTwrFinal) {
    twr->stamps[3] = packet.stamps[3]; twr->stamps[4] = packet.stamps[4]; twr->stamps[5] = rx.full;
    uint32_t tof;
    if (!mulTwrCalculate(twr->stamps, &tof)) mulTwrCancel(twr, dev, "intervals", false);
    else sendPacket(twr, dev, mulTwrReport, mulTimestampMask(rx.full+MUL_TWR_REPLY_MS*MUL_TWR_DTU_PER_MS));
  } else if (packet.type == mulTwrReport) {
    twr->stamps[5] = packet.stamps[5];
    if (!interval(packet.txTimestamp,packet.stamps[5]) || !mulTwrCalculate(twr->stamps, &twr->tof))
      mulTwrCancel(twr, dev, "range", false);
    else sendPacket(twr, dev, mulTwrResult, mulTimestampMask(rx.full+MUL_TWR_REPLY_MS*MUL_TWR_DTU_PER_MS));
  } else if (packet.type == mulTwrResult) {
    uint32_t tof;
    int64_t resultInterval = mulTimestampDifference(packet.txTimestamp, twr->stamps[4]);
    if (resultInterval < (int64_t)(30*MUL_TWR_DTU_PER_MS) ||
        resultInterval > (int64_t)(60*MUL_TWR_DTU_PER_MS) ||
        !mulTwrCalculate(twr->stamps, &tof) || tof != packet.tof) {
      mulTwrCancel(twr, dev, "result", false);
    } else {
      twr->tof = tof;
      twr->delayValid = true;
      twr->syncDirty = true;
      twr->active = false;
      mulRxRearm(dev);
      logSuccess(twr, role, dev);
    }
  }
  return true;
}

uint32_t mulTwrWait(const mulTwr_t *twr, dwDevice_t *dev, uint64_t origin, uint32_t fallback)
{
  if (!twr->active) return fallback;
  uint64_t now = hardwareNow(dev);
  int64_t step = mulTimestampDifference(twr->deadline, now);
  int64_t end = mulTimestampDifference(mulTimestampMask(origin+MUL_TWR_END_MS*MUL_TWR_DTU_PER_MS), now);
  if (end < step) step = end;
  uint32_t wait = step <= 0 ? 1 : (uint32_t)(((uint64_t)step+MUL_TWR_DTU_PER_MS-1)/MUL_TWR_DTU_PER_MS);
  return wait < fallback ? wait : fallback;
}

bool mulTwrDelay(const mulTwr_t *twr, const mulClock_t *clock, mulClockDelay_t *delay)
{
  if (twr == NULL || clock == NULL || delay == NULL || !twr->delayValid) return false;
  *delay = (mulClockDelay_t){.epoch=twr->epoch, .oneWayDtu=twr->tof, .units=mulClockDelayReferenceDtu,
    .measuredMs=twr->measuredMs, .referenceSequence=twr->id, .maxAgeMs=MUL_CLOCK_DELAY_DEFAULT_MAX_AGE_MS,
    .valid=true, .measurementBoundDtu=MUL_CLOCK_BOUND_UNKNOWN, .motionBoundDtuPerMs=MUL_CLOCK_BOUND_UNKNOWN};
  memcpy(delay->reference, addresses[0], 8); memcpy(delay->follower, addresses[1], 8);
  return delay->epoch == clock->epoch;
}

void mulTwrSync(mulTwr_t *twr, const mulClock_t *clock, uint32_t nowMs, bool force)
{
  mulClockDelay_t delay;
  bool supplied = mulTwrDelay(twr, clock, &delay);
  mulClockDelayStatus_t status = mulClockDelayStatus(clock, supplied ? &delay : NULL, nowMs);
  mulClockCorrectedMapping_t mapped;
  bool available = mulClockMapRefToFollowerSimultaneous(clock, clock->referenceTx,
    supplied ? &delay : NULL, nowMs, &mapped);
  mulClockMapStatus_t mapStatus = mulClockMapStatus(clock, nowMs);
  if (force || twr->syncDirty || available != twr->syncAvailable) {
    printf("MUL ALR: SYNC src=0x84 map=%s delay=%s correction=%s delay_age_ms=%" PRIu32
           " physical_bound=UNKNOWN\r\n",
      mapStatus == mulClockMapValid ? "VALID" : mapStatus == mulClockMapWarmup ? "WARMUP" :
      mapStatus == mulClockMapStale ? "STALE" : "INVALID",
      status == mulClockDelayValid ? "VALID" : status == mulClockDelayStale ? "STALE" : "INVALID",
      available ? "AVAILABLE" : "UNAVAILABLE", supplied ? nowMs-delay.measuredMs : 0);
  }
  twr->syncAvailable = available;
  twr->syncDirty = false;
}

uint32_t mulTwrDelayWait(const mulTwr_t *twr, uint32_t nowMs, uint32_t fallback)
{
  uint32_t age = nowMs - twr->measuredMs;
  if (twr->delayValid && age <= MUL_CLOCK_DELAY_DEFAULT_MAX_AGE_MS) {
    uint32_t remaining = MUL_CLOCK_DELAY_DEFAULT_MAX_AGE_MS + 1 - age;
    if (remaining < fallback) return remaining;
  }
  return fallback;
}
