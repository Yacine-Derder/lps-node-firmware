/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_clock.h"
#include "mul_common.h"

#include <math.h>
#include <string.h>

#define SECOND UINT64_C(63897600000)
static mulClock_t clockState;
static mulTestPacket_t reference;
static mulClockDelay_t delay;
static uint32_t nowMs;
static uint64_t txStart;
static uint64_t simultaneousStart;

void setUp(void) {
  mulClockReset(&clockState);
  memset(&reference, 0, sizeof(reference));
  memset(&delay, 0, sizeof(delay));
  const uint8_t ref[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  const uint8_t follower[8] = {0x85,0,0,0,0,0,0xcf,0xbc};
  memcpy(reference.source, ref, sizeof(ref));
  memcpy(delay.reference, ref, sizeof(ref));
  memcpy(delay.follower, follower, sizeof(follower));
  delay.maxAgeMs = MUL_CLOCK_DELAY_DEFAULT_MAX_AGE_MS;
  delay.measurementBoundDtu = MUL_CLOCK_BOUND_UNKNOWN;
  delay.motionBoundDtuPerMs = MUL_CLOCK_BOUND_UNKNOWN;
  nowMs = 0;
  txStart = MUL_TIMESTAMP_MASK - 3*SECOND + 17;
  simultaneousStart = MUL_TIMESTAMP_MASK - 2*SECOND - 123;
}
void tearDown(void) {}

// Test-only injection: metres / (299792458 metres/second) * DTU/second,
// rounded to nearest integer reference DTU. No distances enter production code.
static int64_t distanceDtu(uint32_t metres) {
  return (metres * SECOND + UINT64_C(299792458)/2) / UINT64_C(299792458);
}

static uint64_t truth(int32_t ppb, int64_t referenceDisplacement) {
  return mulTimestampMask(simultaneousStart + llroundl(
    (5.0L*SECOND + referenceDisplacement) * (1.0L + ppb/1e9L)));
}

static void acquire(int32_t ppb, int64_t trueDelay) {
  for (uint32_t i = 0; i <= 5; i++) {
    reference.sequence = i;
    reference.txTimestamp = mulTimestampMask(txStart + i*SECOND);
    uint64_t rx = mulTimestampMask(simultaneousStart + llroundl(
      ((long double)i*SECOND + trueDelay) * (1.0L + ppb/1e9L)));
    nowMs += 1000;
    mulClockObserve(&clockState, &reference, rx, nowMs, true);
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid, clockState.status);
  TEST_ASSERT_EQUAL_INT32(ppb, clockState.skewPpb);
  delay.epoch = clockState.epoch;
  delay.oneWayDtu = trueDelay;
  delay.measuredMs = nowMs;
  delay.referenceSequence = clockState.sequence;
  delay.valid = true;
}

static void assertRejected(const mulClockDelay_t *measurement) {
  mulClockCorrectedMapping_t result;
  memset(&result, 0xa5, sizeof(result));
  mulClockCorrectedMapping_t original = result;
  TEST_ASSERT_FALSE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, measurement, nowMs, &result));
  TEST_ASSERT_EQUAL_MEMORY(&original, &result, sizeof(result));
  TEST_ASSERT_FALSE(mulClockMapFollowerSimultaneousToRef(&clockState,
    clockState.localRx, measurement, nowMs, &result));
  TEST_ASSERT_EQUAL_MEMORY(&original, &result, sizeof(result));
}

void test_mulDelaySyntheticDistancesUnitsPrecisionAndBothSkewSigns(void) {
  const uint32_t metres[] = {0,1,5,10,30};
  const int32_t rates[] = {0,5000,-10000,200000,-200000};
  for (unsigned int s = 0; s < sizeof(rates)/sizeof(rates[0]); s++) {
    for (unsigned int m = 0; m < sizeof(metres)/sizeof(metres[0]); m++) {
      setUp();
      int64_t tau = distanceDtu(metres[m]);
      long double exact = (long double)metres[m] * SECOND / 299792458.0L;
      TEST_ASSERT_TRUE(fabsl(exact - tau) <= 0.5L);
      acquire(rates[s], tau);
      for (int direction = -1; direction <= 1; direction++) {
        int64_t delta = direction*(int64_t)(SECOND/2);
        uint64_t tx = mulTimestampMask(clockState.referenceTx + delta);
        mulClockMapping_t arrival;
        mulClockCorrectedMapping_t corrected, inverse;
        TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, tx, nowMs, &arrival));
        TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState, tx, &delay, nowMs, &corrected));
        TEST_ASSERT_INT64_WITHIN(2, 0, mulTimestampDifference(corrected.timestamp, truth(rates[s], delta)));
        int64_t expectedCorrection = llroundl(tau*(1.0L + rates[s]/1e9L));
        TEST_ASSERT_EQUAL_INT64(expectedCorrection, mulTimestampDifference(arrival.timestamp, corrected.timestamp));
        TEST_ASSERT_TRUE(mulClockMapFollowerSimultaneousToRef(&clockState, corrected.timestamp,
          &delay, nowMs, &inverse));
        TEST_ASSERT_INT64_WITHIN(1, 0, mulTimestampDifference(inverse.timestamp, tx));
        TEST_ASSERT_EQUAL_INT(mulAlrFollower, corrected.affine.destination);
        TEST_ASSERT_EQUAL_INT(mulAlrReference, inverse.affine.destination);
        // Inverse independently supplied simultaneous ground truth, not only round trip.
        TEST_ASSERT_TRUE(mulClockMapFollowerSimultaneousToRef(&clockState, truth(rates[s], delta),
          &delay, nowMs, &inverse));
        TEST_ASSERT_INT64_WITHIN(2, 0, mulTimestampDifference(inverse.timestamp, tx));
      }
    }
  }
}

void test_mulDelayIncorrectEstimateProducesExpectedSignedCorrectionError(void) {
  const int32_t rates[] = {0,5000,-10000,200000};
  for (unsigned int i = 0; i < sizeof(rates)/sizeof(rates[0]); i++) {
    setUp();
    acquire(rates[i], distanceDtu(5));
    mulClockCorrectedMapping_t correct, wrong;
    TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
      clockState.referenceTx, &delay, nowMs, &correct));
    int64_t error = distanceDtu(10) - delay.oneWayDtu;
    delay.oneWayDtu += error;
    TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
      clockState.referenceTx, &delay, nowMs, &wrong));
    int64_t expectedError = llroundl(-error*(1.0L + rates[i]/1e9L));
    TEST_ASSERT_INT64_WITHIN(1, expectedError, mulTimestampDifference(wrong.timestamp, correct.timestamp));
    TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, wrong.totalPhysicalBoundDtu);
    mulClockCorrectedMapping_t wrongInverse;
    TEST_ASSERT_TRUE(mulClockMapFollowerSimultaneousToRef(&clockState,
      truth(rates[i], 0), &delay, nowMs, &wrongInverse));
    TEST_ASSERT_INT64_WITHIN(2, error,
      mulTimestampDifference(wrongInverse.timestamp, clockState.referenceTx));
    delay.oneWayDtu = 0; // Underestimate: simultaneous result remains too late.
    TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
      clockState.referenceTx, &delay, nowMs, &wrong));
    TEST_ASSERT_TRUE(mulTimestampDifference(wrong.timestamp, correct.timestamp) > 0);
  }
}

void test_mulDelayMissingInvalidAndWrongLinkDirectionAreRejected(void) {
  acquire(5000, distanceDtu(10));
  TEST_ASSERT_EQUAL_INT(mulClockDelayMissing, mulClockDelayStatus(&clockState, NULL, nowMs));
  assertRejected(NULL);
  mulClockDelay_t saved = delay;
  delay.valid = false;
  assertRejected(&delay);
  delay = saved;
  delay.reference[1] = 1; // Full identity, not merely node ID.
  assertRejected(&delay);
  delay = saved;
  delay.follower[7] ^= 1;
  assertRejected(&delay);
  delay = saved;
  memcpy(delay.reference, saved.follower, MUL_ADDRESS_LENGTH);
  memcpy(delay.follower, saved.reference, MUL_ADDRESS_LENGTH); // Opposite link.
  assertRejected(&delay);
  delay = saved;
  delay.units = (mulClockDelayUnits_t)1; // Follower DTU, ns or RTT are not this contract.
  assertRejected(&delay);
}

void test_mulDelayNegativeImplausibleAndInvalidAgePoliciesAreRejected(void) {
  acquire(-10000, 0);
  const int64_t invalid[] = {-1, INT64_MIN, INT64_MAX, MUL_CLOCK_DELAY_MAX_DTU+1};
  for (unsigned int i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
    delay.oneWayDtu = invalid[i];
    TEST_ASSERT_EQUAL_INT(mulClockDelayInvalid, mulClockDelayStatus(&clockState, &delay, nowMs));
    assertRejected(&delay);
  }
  delay.oneWayDtu = 0;
  delay.maxAgeMs = 0;
  assertRejected(&delay);
  delay.maxAgeMs = MUL_CLOCK_DELAY_MAX_AGE_MS + 1;
  assertRejected(&delay);
  delay.maxAgeMs = MUL_CLOCK_DELAY_DEFAULT_MAX_AGE_MS;
  delay.measuredMs = nowMs + 1;
  assertRejected(&delay);
}

void test_mulDelayFreshnessOriginAndExtrapolationAllLimitApplicability(void) {
  acquire(5000, distanceDtu(30));
  mulClockCorrectedMapping_t result;
  uint32_t measured = nowMs;
  nowMs += 1000;
  TEST_ASSERT_EQUAL_INT(mulClockDelayValid, mulClockDelayStatus(&clockState, &delay, nowMs));
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &result));
  TEST_ASSERT_EQUAL_UINT32(1000, result.delayAgeMs);
  nowMs++;
  TEST_ASSERT_EQUAL_INT(mulClockDelayStale, mulClockDelayStatus(&clockState, &delay, nowMs));
  assertRejected(&delay);
  nowMs = measured;
  TEST_ASSERT_FALSE(mulClockMapRefToFollowerSimultaneous(&clockState,
    mulTimestampMask(clockState.referenceTx + 2*SECOND), &delay, nowMs, &result));
  delay.maxAgeMs = MUL_CLOCK_DELAY_MAX_AGE_MS;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    mulTimestampMask(clockState.referenceTx + 2*SECOND), &delay, nowMs, &result));
  // A new measurement must also cover the old mapping origin, not just query time.
  nowMs += 1001;
  delay.measuredMs = nowMs;
  delay.maxAgeMs = 1000;
  assertRejected(&delay);
}

void test_mulDelayEpochLossRestartAndOtherClockContextCannotReuseMeasurements(void) {
  acquire(5000, distanceDtu(1));
  uint64_t oldEpoch = delay.epoch;
  mulClock_t other;
  mulClockReset(&other);
  TEST_ASSERT_EQUAL_INT(mulClockDelayEpochMismatch, mulClockDelayStatus(&other, &delay, nowMs));
  mulClockReset(&clockState); // Same path as follower REF_LOST.
  TEST_ASSERT_TRUE(clockState.epoch != oldEpoch);
  assertRejected(&delay);
  acquire(-10000, distanceDtu(5)); // Fresh model/new reference TX sequence.
  delay.epoch = oldEpoch; // Even refreshing age must not resurrect the old record.
  TEST_ASSERT_EQUAL_INT(mulClockDelayEpochMismatch, mulClockDelayStatus(&clockState, &delay, nowMs));
  assertRejected(&delay);
  delay.epoch = clockState.epoch;
  mulClockCorrectedMapping_t result;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &result));
  reference.sequence++;
  reference.txTimestamp = 0; // Detected discontinuity replaces epoch.
  nowMs += 1000;
  uint64_t epoch = clockState.epoch;
  TEST_ASSERT_EQUAL_INT(mulClockDiscontinuity, mulClockObserve(&clockState, &reference, 0, nowMs, true));
  TEST_ASSERT_TRUE(clockState.epoch != epoch);
  assertRejected(&delay);
}

void test_mulDelaySequenceWrapGapsAndUptimeFullWrapAlias(void) {
  acquire(5000, distanceDtu(5));
  clockState.sequence = UINT32_MAX - 1;
  delay.referenceSequence = clockState.sequence;
  reference.sequence = 0; // Two intervals, one missing packet across sequence wrap.
  reference.txTimestamp = mulTimestampMask(clockState.referenceTx + 2*SECOND);
  uint64_t nextRx = mulTimestampMask(clockState.localRx + 2*SECOND + 2*SECOND/200000);
  nowMs += 2000;
  delay.maxAgeMs = MUL_CLOCK_DELAY_MAX_AGE_MS;
  TEST_ASSERT_EQUAL_INT(mulClockSample, mulClockObserve(&clockState, &reference, nextRx, nowMs, true));
  TEST_ASSERT_EQUAL_INT(mulClockDelayValid, mulClockDelayStatus(&clockState, &delay, nowMs));
  mulClockCorrectedMapping_t result;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &result));
  // A stale record from a complete uint32 uptime wrap ago may look young in ms,
  // but cannot have a nearby reference sequence at the established 1 Hz cadence.
  delay.measuredMs = nowMs;
  delay.referenceSequence = clockState.sequence - 4294967;
  TEST_ASSERT_EQUAL_INT(mulClockDelayEpochMismatch, mulClockDelayStatus(&clockState, &delay, nowMs));
  assertRejected(&delay);
  delay.referenceSequence = clockState.sequence + 1;
  assertRejected(&delay);
}

void test_mulDelayValidMeasurementDoesNotImplyAvailableOrBoundedMapping(void) {
  acquire(5000, distanceDtu(5));
  TEST_ASSERT_EQUAL_INT(mulClockDelayValid, mulClockDelayStatus(&clockState, &delay, nowMs));
  clockState.status = mulClockWarmup;
  assertRejected(&delay);
  clockState.status = mulClockValid;
  clockState.skewPpb = MUL_CLOCK_MAX_PPB + 1;
  assertRejected(&delay);
  clockState.skewPpb = 5000;
  nowMs += MUL_CLOCK_MAP_HORIZON_MS + 1;
  delay.measuredMs = nowMs; // Delay fresh, affine model stale.
  TEST_ASSERT_EQUAL_INT(mulClockDelayValid, mulClockDelayStatus(&clockState, &delay, nowMs));
  assertRejected(&delay);
}

void test_mulDelayKnownMeasurementUncertaintyDoesNotMakeUnknownTotalFinite(void) {
  acquire(5000, distanceDtu(10));
  delay.measurementBoundDtu = 20;
  delay.motionBoundDtuPerMs = 2;
  mulClockCorrectedMapping_t first, later, inverse;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &first));
  TEST_ASSERT_EQUAL_UINT64(20, first.measurementBoundReferenceDtu);
  TEST_ASSERT_EQUAL_UINT64(0, first.motionBoundReferenceDtu);
  TEST_ASSERT_EQUAL_UINT64(21, first.conditionalDelayBoundDtu); // ceil(a*20).
  nowMs += 100;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &later));
  TEST_ASSERT_EQUAL_UINT64(200, later.motionBoundReferenceDtu);
  TEST_ASSERT_EQUAL_UINT64(221, later.conditionalDelayBoundDtu);
  TEST_ASSERT_TRUE(mulClockMapFollowerSimultaneousToRef(&clockState,
    later.timestamp, &delay, nowMs, &inverse));
  TEST_ASSERT_EQUAL_UINT64(220, inverse.conditionalDelayBoundDtu); // Reference units.
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, later.totalPhysicalBoundDtu);
  TEST_ASSERT_EQUAL_UINT32(mulClockUnknownAll, later.affine.unknownComponents);
  TEST_ASSERT_FALSE(later.affine.syncCalibrated);
  TEST_ASSERT_TRUE(later.numericalBoundDtu >= later.affine.numericalBoundDtu + 2);
}

void test_mulDelayUnknownMeasurementMotionAndOverflowRemainUnknown(void) {
  acquire(-10000, distanceDtu(30));
  mulClockCorrectedMapping_t result;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &result));
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, result.measurementBoundReferenceDtu);
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, result.motionBoundReferenceDtu);
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, result.conditionalDelayBoundDtu);
  delay.measurementBoundDtu = 0; // Explicit ideal synthetic measurement, not a default.
  delay.motionBoundDtuPerMs = 0; // Explicit constant synthetic path.
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &result));
  TEST_ASSERT_EQUAL_UINT64(0, result.conditionalDelayBoundDtu);
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, result.totalPhysicalBoundDtu);
  delay.motionBoundDtuPerMs = UINT64_MAX/2;
  nowMs += 100;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &result));
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, result.motionBoundReferenceDtu);
  delay.motionBoundDtuPerMs = 1;
  delay.measurementBoundDtu = UINT64_MAX - 50;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &result));
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, result.conditionalDelayBoundDtu);
}

void test_mulDelayWrapsLargeOriginsAndZeroDelayAreHandled(void) {
  nowMs = UINT32_MAX - 6500;
  acquire(-10000, 0);
  nowMs += 501; // Uptime wraps, measurement age remains well-defined.
  mulClockMapping_t arrival;
  mulClockCorrectedMapping_t corrected;
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, clockState.referenceTx, nowMs, &arrival));
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &corrected));
  TEST_ASSERT_EQUAL_UINT64(arrival.timestamp, corrected.timestamp);
  TEST_ASSERT_EQUAL_UINT32(501, corrected.delayAgeMs);
}

void test_mulDelayCorrectionAndInverseShiftAcrossCounterZero(void) {
  const int32_t rates[] = {-200000, 200000, -10000, 5000};
  for (unsigned int i = 0; i < sizeof(rates)/sizeof(rates[0]); i++) {
    setUp();
    int64_t tau = distanceDtu(30);
    txStart = MUL_TIMESTAMP_MASK - 5*SECOND - 10;
    simultaneousStart = mulTimestampMask(3 - llroundl(
      (5.0L*SECOND + tau)*(1.0L + rates[i]/1e9L)));
    acquire(rates[i], tau);
    TEST_ASSERT_EQUAL_UINT64(3, clockState.localRx);
    TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK - 10, clockState.referenceTx);
    mulClockCorrectedMapping_t forward, inverse;
    TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
      clockState.referenceTx, &delay, nowMs, &forward));
    TEST_ASSERT_TRUE(forward.timestamp > MUL_TIMESTAMP_MASK - 10000);
    TEST_ASSERT_TRUE(mulClockMapFollowerSimultaneousToRef(&clockState,
      forward.timestamp, &delay, nowMs, &inverse));
    TEST_ASSERT_INT64_WITHIN(1, 0, mulTimestampDifference(inverse.timestamp, clockState.referenceTx));
  }
}

void test_mulDelayMotionBoundCoversOriginAndForwardOrBackwardQuery(void) {
  acquire(-10000, distanceDtu(5));
  delay.measurementBoundDtu = 7;
  delay.motionBoundDtuPerMs = 3;
  mulClockCorrectedMapping_t forward, backward;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    mulTimestampMask(clockState.referenceTx + SECOND/2), &delay, nowMs, &forward));
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    mulTimestampMask(clockState.referenceTx - SECOND/2), &delay, nowMs, &backward));
  TEST_ASSERT_EQUAL_UINT32(500, forward.applicationAgeMs);
  TEST_ASSERT_EQUAL_UINT64(1500, forward.motionBoundReferenceDtu);
  TEST_ASSERT_EQUAL_UINT64(forward.motionBoundReferenceDtu, backward.motionBoundReferenceDtu);
  TEST_ASSERT_EQUAL_UINT64(1507, forward.conditionalDelayBoundDtu); // ceil(1507*0.999990).
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, forward.totalPhysicalBoundDtu);
  delay.motionBoundDtuPerMs = MUL_CLOCK_BOUND_UNKNOWN;
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &forward));
  TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN, forward.conditionalDelayBoundDtu);
}

void test_mulDelayDoesNotMutateArrivalMappingOrDoubleApplyCorrection(void) {
  acquire(200000, MUL_CLOCK_DELAY_MAX_DTU);
  mulClockMapping_t before, after;
  mulClockCorrectedMapping_t first, second;
  mulClock_t model = clockState;
  mulClockDelay_t measurement = delay;
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, clockState.referenceTx, nowMs, &before));
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &first));
  TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState,
    clockState.referenceTx, &delay, nowMs, &second));
  TEST_ASSERT_EQUAL_UINT64(first.timestamp, second.timestamp);
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, clockState.referenceTx, nowMs, &after));
  TEST_ASSERT_EQUAL_UINT64(before.timestamp, after.timestamp);
  TEST_ASSERT_EQUAL_MEMORY(&model, &clockState, sizeof(model));
  TEST_ASSERT_EQUAL_MEMORY(&measurement, &delay, sizeof(delay));
  TEST_ASSERT_FALSE(mulClockMapRefToFollowerSimultaneous(NULL, 0, &delay, nowMs, &first));
  TEST_ASSERT_FALSE(mulClockMapFollowerSimultaneousToRef(NULL, 0, &delay, nowMs, &first));
  TEST_ASSERT_FALSE(mulClockMapRefToFollowerSimultaneous(&clockState, 0, &delay, nowMs, NULL));
}

void test_mulDelayLongRunReanchorsWhileFreshExternalMeasurementsAreRequired(void) {
  acquire(-10000, distanceDtu(30));
  uint64_t epoch = clockState.epoch;
  for (uint32_t i = 0; i < 100000; i++) {
    mulClockCorrectedMapping_t forward, inverse;
    uint64_t query = mulTimestampMask(clockState.referenceTx + SECOND/2);
    TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&clockState, query, &delay, nowMs, &forward));
    TEST_ASSERT_TRUE(mulClockMapFollowerSimultaneousToRef(&clockState,
      forward.timestamp, &delay, nowMs, &inverse));
    TEST_ASSERT_INT64_WITHIN(1, 0, mulTimestampDifference(inverse.timestamp, query));
    reference.sequence++;
    reference.txTimestamp = mulTimestampMask(clockState.referenceTx + SECOND);
    uint64_t nextRx = mulTimestampMask(clockState.localRx + SECOND - SECOND/100000);
    nowMs += 1000;
    TEST_ASSERT_EQUAL_INT(mulClockSample, mulClockObserve(&clockState, &reference, nextRx, nowMs, true));
    TEST_ASSERT_EQUAL_UINT64(epoch, clockState.epoch); // Re-anchor is not a restart.
    delay.measuredMs = nowMs; // Explicit new synthetic measurement, never automatic in firmware.
    delay.referenceSequence = clockState.sequence;
  }
  nowMs++;
  TEST_ASSERT_EQUAL_INT(mulClockMapValid, mulClockMapStatus(&clockState, nowMs));
}
