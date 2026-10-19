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

// Arrival-aligned mapping ONLY: full reference identity 84:00:00:00:00:00:cf:bc
// to follower 85:00:00:00:00:00:cf:bc. Existing observation/slope fields are the
// mapping origin; no absolute intercept or independently duplicated state.
#define MUL_CLOCK_MAP_HORIZON_MS 2000
#define MUL_CLOCK_MAP_HORIZON_DTU (UINT64_C(63897600) * MUL_CLOCK_MAP_HORIZON_MS)
typedef enum {
  mulClockMapInvalid,
  mulClockMapWarmup,
  mulClockMapValid,
  mulClockMapStale,
} mulClockMapStatus_t;

// These physical uncertainties have no established finite bounds. They must
// not be inferred from convergence, timestamp resolution, or sample scatter.
enum {
  mulClockUnknownRate = 1u << 0,
  mulClockUnknownReferenceMeasurement = 1u << 1,
  mulClockUnknownFollowerMeasurement = 1u << 2,
  mulClockUnknownOrigin = 1u << 3,
  mulClockUnknownPropagation = 1u << 4,
  mulClockUnknownPropagationChange = 1u << 5,
  mulClockUnknownExtrapolation = 1u << 6,
  mulClockUnknownTotal = 1u << 7,
  mulClockUnknownAll = 0xff,
};

typedef struct {
  uint64_t timestamp; // 40-bit arrival-aligned coordinate, in destination clock.
  mulAlrRole_t destination;
  uint32_t ageMs; // Coarse age of origin at query time, not extrapolation distance.
  uint64_t extrapolationDtu; // Absolute displacement from origin, reference units.
  uint32_t unknownComponents; // Physical error bounds: flags above, currently ALL.
  uint32_t observedRateSpreadPpb; // Max deviation in five samples; NOT a bound.
  uint64_t numericalBoundDtu; // Rounding/rate-quantization ONLY, destination units.
  bool syncCalibrated; // Always false: neither true offset nor delay is identified.
} mulClockMapping_t;

mulClockMapStatus_t mulClockMapStatus(const mulClock_t *clock, uint32_t nowMs);
// Nearest modular displacement must be within +/- horizon in reference units;
// origin age must also be <= horizon. Caller must know the intended counter epoch:
// no 40-bit API can distinguish a full-wrap alias from a nearby coordinate.
// On failure output is unchanged. Inverse input must be an arrival-aligned local
// coordinate, NOT an arbitrary follower TX timestamp. Both inherit delay ambiguity.
bool mulClockMapRefToFollower(const mulClock_t *clock, uint64_t referenceTx,
                             uint32_t nowMs, mulClockMapping_t *mapping);
bool mulClockMapFollowerToRef(const mulClock_t *clock, uint64_t localArrival,
                             uint32_t nowMs, mulClockMapping_t *mapping);

#endif // MUL_CLOCK_H
