/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_PROPAGATION_H
#define MUL_PROPAGATION_H
#include <stdint.h>
// Strict v1 compatibility by default. Opt into continuous v2 on all nodes.
#ifndef MUL_PROP_MAX_AGE_MS
#define MUL_PROP_MAX_AGE_MS 1000
#endif
#if MUL_PROP_MAX_AGE_MS < 1000 || MUL_PROP_MAX_AGE_MS > 6000
#error "MUL_PROP_MAX_AGE_MS must be between 1000 and 6000"
#endif
#ifndef MUL_RELATIVE_SPEED_MM_S
#define MUL_RELATIVE_SPEED_MM_S (-1) // No physical motion guarantee.
#endif
#if MUL_RELATIVE_SPEED_MM_S < -1 || MUL_RELATIVE_SPEED_MM_S > 1000000
#error "MUL_RELATIVE_SPEED_MM_S must be -1 (UNKNOWN) or 0..1000000"
#endif
// Opt-in budget on UNMODELED path change only, not total range accuracy.
// The speed must bound BOTH endpoints' relative motion. Unknown fails closed.
#ifndef MUL_PROP_MOTION_BUDGET_MM
#define MUL_PROP_MOTION_BUDGET_MM (-1)
#endif
#if MUL_PROP_MOTION_BUDGET_MM < -1 || MUL_PROP_MOTION_BUDGET_MM > 1000000
#error "MUL_PROP_MOTION_BUDGET_MM must be -1 (disabled) or 0..1000000"
#endif
static inline uint32_t mulPropagationMotionHorizon(uint32_t lifetimeMs) {
#if MUL_PROP_MOTION_BUDGET_MM >= 0
#if MUL_RELATIVE_SPEED_MM_S < 0
  (void)lifetimeMs; return 0;
#elif MUL_RELATIVE_SPEED_MM_S > 0
  uint32_t horizon = (uint32_t)((UINT64_C(1000) * MUL_PROP_MOTION_BUDGET_MM) /
                               MUL_RELATIVE_SPEED_MM_S);
  return horizon < lifetimeMs ? horizon : lifetimeMs;
#endif
#endif
  return lifetimeMs;
}
static inline int mulPropagationMotionValid(uint32_t spanMs) {
#if MUL_PROP_MOTION_BUDGET_MM < 0
  (void)spanMs; return 1; // Existing caller-owned TTL remains authoritative.
#elif MUL_RELATIVE_SPEED_MM_S < 0
  (void)spanMs; return 0;
#else
  return spanMs <= mulPropagationMotionHorizon(MUL_PROP_MAX_AGE_MS);
#endif
}
// Outward-rounded reference nominal DTU bound; not a statistical variance.
static inline uint64_t mulPropagationMotionDtu(uint32_t spanMs) {
#if MUL_RELATIVE_SPEED_MM_S >= 0
  if (spanMs > 6000) return UINT64_MAX;
  return ((uint64_t)MUL_RELATIVE_SPEED_MM_S * spanMs * UINT64_C(63897600) +
          UINT64_C(299792457999)) / UINT64_C(299792458000);
#else
  (void)spanMs; return UINT64_MAX;
#endif
}
// A named stationary build must never silently emit strict v1 relations.
#if defined(MUL_STATIONARY_CH5) && (MUL_PROP_MAX_AGE_MS != 6000 || MUL_RELATIVE_SPEED_MM_S != 0 || !MUL_HYBRID_CH5)
#error "stationary-ch5 requires CH5, propagation6000 and explicit zero relative speed"
#endif
typedef enum {
  mulPropagationNone,
  mulPropagationFresh, // Measurement cycle or first reference after it.
  mulPropagationReused,
  mulPropagationExpired,
  mulPropagationInvalidated,
} mulPropagationState_t;
#endif
