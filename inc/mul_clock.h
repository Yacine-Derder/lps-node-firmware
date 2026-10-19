/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_CLOCK_H
#define MUL_CLOCK_H

#include "mul_common.h"

#define MUL_CLOCK_HISTORY 5
#define MUL_CLOCK_MAX_PPB 200000 // +/-200 ppm plausibility gate, not calibration.
#define MUL_CLOCK_STABILITY_PPB 1000 // At least three samples within 1 ppm of median.

typedef enum {
  mulClockSearching,
  mulClockWarmup,
  mulClockValid,
} mulClockStatus_t;

typedef enum {
  mulClockIgnored,
  mulClockBaseline,
  mulClockSample,
  mulClockDiscontinuity,
} mulClockResult_t;

typedef struct {
  bool hasBaseline;
  uint32_t sequence;
  uint64_t referenceTx;
  uint64_t localRx;
  uint32_t observedMs;
  int32_t history[MUL_CLOCK_HISTORY];
  unsigned int count;
  unsigned int next;
  uint32_t samples; // Accepted intervals since baseline/reset, saturating.
  int32_t skewPpb; // ratio = 1 + skewPpb/1e9; positive means faster local clock.
  mulClockStatus_t status;
} mulClock_t;

void mulClockReset(mulClock_t *clock);
// observedMs is wrap-safe coarse monotonic uptime, used only to reject long gaps.
// valid means the packet passed wire validation and local RX capture succeeded.
// Caller must reset on reference loss/reacquisition; no epoch exists on wire.
mulClockResult_t mulClockObserve(mulClock_t *clock, const mulTestPacket_t *packet,
                                uint64_t localRx, uint32_t observedMs, bool valid);

#endif // MUL_CLOCK_H
