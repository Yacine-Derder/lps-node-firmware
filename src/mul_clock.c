/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_clock.h"

#include <string.h>

#define DTU_PER_MS UINT64_C(63897600)
#define PERIOD_DTU (1000 * DTU_PER_MS)
#define PERIOD_TOLERANCE (100 * DTU_PER_MS)

void mulClockReset(mulClock_t *clock)
{
  memset(clock, 0, sizeof(*clock));
}

static void baseline(mulClock_t *clock, const mulTestPacket_t *packet,
                     uint64_t localRx, uint32_t observedMs)
{
  clock->hasBaseline = true;
  clock->sequence = packet->sequence;
  clock->referenceTx = mulTimestampMask(packet->txTimestamp);
  clock->localRx = mulTimestampMask(localRx);
  clock->observedMs = observedMs;
  if (clock->count == 0) {
    clock->status = mulClockWarmup;
  }
}

mulClockResult_t mulClockObserve(mulClock_t *clock, const mulTestPacket_t *packet,
                                uint64_t localRx, uint32_t observedMs, bool valid)
{
  if (!valid || packet == NULL || mulAlrRoleForAddress(packet->source) != mulAlrReference) {
    return mulClockIgnored;
  }
  if (!clock->hasBaseline) {
    baseline(clock, packet, localRx, observedMs);
    return mulClockBaseline;
  }
  uint32_t advance = packet->sequence - clock->sequence;
  if (advance == 0 || advance >= UINT32_C(0x80000000)) {
    return mulClockIgnored; // No baseline/history/age refresh from replay or reordering.
  }

  // Subtract integers in their own clock domains before computing the rate.
  int64_t remote = mulTimestampDifference(packet->txTimestamp, clock->referenceTx);
  int64_t local = mulTimestampDifference(localRx, clock->localRx);
  uint32_t elapsed = observedMs - clock->observedMs;
  // Bound the elapsed *real* time as well as modular differences, to reject an
  // apparently small interval after one or more complete hardware-counter wraps.
  // The existing 1 Hz reference advances sequence once per TX attempt. Missing
  // packets use their measured multi-period intervals, never an assumed second.
  bool plausible = elapsed > 0 && elapsed < MUL_SOURCE_TIMEOUT_MS &&
    advance <= 3 && remote > 0 && local > 0 &&
    remote <= (int64_t)(3 * PERIOD_DTU + PERIOD_TOLERANCE) &&
    local <= (int64_t)(3 * PERIOD_DTU + PERIOD_TOLERANCE);
  if (plausible) {
    int64_t expected = (int64_t)advance * PERIOD_DTU;
    plausible = remote >= expected - (int64_t)PERIOD_TOLERANCE &&
                remote <= expected + (int64_t)PERIOD_TOLERANCE;
  }
  int32_t skew = 0;
  if (plausible) {
    int64_t difference = local - remote;
    // Gate before multiplication: bounds ensure difference * 1e9 fits int64.
    int64_t limit = remote * MUL_CLOCK_MAX_PPB / INT64_C(1000000000);
    plausible = difference >= -limit && difference <= limit;
    if (plausible) {
      // Round to nearest ppb; avoid subtracting nearly equal floats near ratio=1.
      int64_t scaled = difference * INT64_C(1000000000);
      skew = (int32_t)((scaled + (scaled < 0 ? -remote/2 : remote/2)) / remote);
    }
  }
  if (!plausible) {
    mulClockReset(clock);
    baseline(clock, packet, localRx, observedMs);
    return mulClockDiscontinuity;
  }

  clock->history[clock->next] = skew;
  clock->next = (clock->next + 1) % MUL_CLOCK_HISTORY;
  if (clock->count < MUL_CLOCK_HISTORY) {
    clock->count++;
  }
  if (clock->samples != UINT32_MAX) {
    clock->samples++;
  }
  // Five bounded values: median rejects up to two isolated plausible outliers
  // without an accumulating sum or a persistent bias from an erroneous interval.
  int32_t sorted[MUL_CLOCK_HISTORY];
  memcpy(sorted, clock->history, clock->count * sizeof(sorted[0]));
  for (unsigned int i = 1; i < clock->count; i++) {
    int32_t value = sorted[i];
    unsigned int j = i;
    while (j > 0 && sorted[j - 1] > value) {
      sorted[j] = sorted[j - 1];
      j--;
    }
    sorted[j] = value;
  }
  clock->skewPpb = sorted[clock->count / 2];
  unsigned int inliers = 0;
  for (unsigned int i = 0; i < clock->count; i++) {
    int32_t distance = sorted[i] - clock->skewPpb;
    if (distance >= -MUL_CLOCK_STABILITY_PPB && distance <= MUL_CLOCK_STABILITY_PPB) {
      inliers++;
    }
  }
  clock->status = clock->count == MUL_CLOCK_HISTORY && inliers >= 3 ?
    mulClockValid : mulClockWarmup;
  baseline(clock, packet, localRx, observedMs);
  return mulClockSample;
}
