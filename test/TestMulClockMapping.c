/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_clock.h"
#include "mul_common.h"

#include <math.h>
#include <string.h>

#define SECOND UINT64_C(63897600000)
static mulClock_t clockState;
static mulTestPacket_t reference;
static uint32_t nowMs;

void setUp(void) {
  mulClockReset(&clockState);
  memset(&reference, 0, sizeof(reference));
  const uint8_t address[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  memcpy(reference.source, address, sizeof(address));
  nowMs = 0;
}
void tearDown(void) {}

static void converge(int32_t ppb, uint64_t firstTx, uint64_t firstRx) {
  for (uint32_t i = 0; i <= 5; i++) {
    reference.sequence = i;
    reference.txTimestamp = mulTimestampMask(firstTx + i*SECOND);
    uint64_t rx = mulTimestampMask(firstRx + (uint64_t)llroundl(
      (long double)i * SECOND * (1.0L + ppb/1e9L)));
    nowMs += 1000;
    mulClockObserve(&clockState, &reference, rx, nowMs, true);
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid, clockState.status);
  TEST_ASSERT_EQUAL_INT32(ppb, clockState.skewPpb);
}

static void checkRoundTrip(int64_t delta, int32_t ppb) {
  mulClockMapping_t forward, inverse;
  uint64_t input = mulTimestampMask(clockState.referenceTx + delta);
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, input, nowMs, &forward));
  uint64_t expected = mulTimestampMask(clockState.localRx +
    llroundl((long double)delta * (1.0L + ppb/1e9L)));
  TEST_ASSERT_EQUAL_UINT64(expected, forward.timestamp);
  TEST_ASSERT_EQUAL_INT(mulAlrFollower, forward.destination);
  TEST_ASSERT_TRUE(mulClockMapFollowerToRef(&clockState, forward.timestamp, nowMs, &inverse));
  TEST_ASSERT_INT64_WITHIN(1, 0, mulTimestampDifference(inverse.timestamp, input));
  TEST_ASSERT_EQUAL_INT(mulAlrReference, inverse.destination);
}

void test_mulMappingIdentityAndBothConversionDirections(void) {
  converge(0, UINT64_C(0x123456789a), UINT64_C(0x123456789a));
  checkRoundTrip(0, 0);
  checkRoundTrip(SECOND/2 + 1, 0);
  checkRoundTrip(-(int64_t)SECOND, 0);
}

void test_mulMappingKnownCombinedInterceptWithPositiveSkew(void) {
  const uint64_t start = UINT64_C(0xb112345678);
  const int64_t combinedIntercept = INT64_C(987654321);
  uint64_t firstRx = (uint64_t)llroundl((long double)start * 1.000005L + combinedIntercept);
  converge(5000, start, firstRx);
  mulClockMapping_t mapping;
  uint64_t query = start + 6*SECOND;
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, mulTimestampMask(query), nowMs, &mapping));
  uint64_t expected = mulTimestampMask((uint64_t)llroundl((long double)query * 1.000005L + combinedIntercept));
  TEST_ASSERT_INT64_WITHIN(1, 0, mulTimestampDifference(mapping.timestamp, expected));
  checkRoundTrip(SECOND, 5000);
  checkRoundTrip(-(int64_t)SECOND/2, 5000);
}

void test_mulMappingNegativeSkewLargeUnrelatedClockOrigins(void) {
  converge(-10000, MUL_TIMESTAMP_MASK - SECOND, UINT64_C(0x8001234567));
  checkRoundTrip(SECOND/3 + 123, -10000);
  checkRoundTrip(-(int64_t)SECOND - 999, -10000);
}

void test_mulMappingInverseMatchesIndependentAffineOracle(void) {
  const int32_t slopes[] = {200000, -200000, 5000, -10000};
  for (unsigned int i = 0; i < sizeof(slopes)/sizeof(slopes[0]); i++) {
    setUp();
    int32_t ppb = slopes[i];
    converge(ppb, MUL_TIMESTAMP_MASK - 10, MUL_TIMESTAMP_MASK - SECOND/2);
    for (int64_t delta = -(int64_t)SECOND; delta <= (int64_t)SECOND; delta += 1234567890) {
      mulClockMapping_t inverse;
      TEST_ASSERT_TRUE(mulClockMapFollowerToRef(&clockState,
        mulTimestampMask(clockState.localRx + delta), nowMs, &inverse));
      uint64_t expected = mulTimestampMask(clockState.referenceTx +
        llroundl((long double)delta / (1.0L + ppb/1e9L)));
      TEST_ASSERT_INT64_WITHIN(1, 0, mulTimestampDifference(inverse.timestamp, expected));
      TEST_ASSERT_FALSE(inverse.syncCalibrated);
      TEST_ASSERT_EQUAL_UINT32(mulClockUnknownAll, inverse.unknownComponents);
    }
  }
}

void test_mulMappingNearWrapOriginsAndBothHorizonEndpoints(void) {
  const int32_t slopes[] = {200000, -200000, -1612, 0, 5000};
  for (unsigned int i = 0; i < sizeof(slopes)/sizeof(slopes[0]); i++) {
    setUp();
    int32_t ppb = slopes[i];
    uint64_t localSpan = (uint64_t)llroundl(5.0L*SECOND*(1.0L + ppb/1e9L));
    converge(ppb, mulTimestampMask(MUL_TIMESTAMP_MASK - 5*SECOND - 4),
             mulTimestampMask(MUL_TIMESTAMP_MASK - localSpan - 2));
    TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK - 4, clockState.referenceTx);
    TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK - 2, clockState.localRx);
    checkRoundTrip(1001, ppb);
    checkRoundTrip(-1001, ppb);
    checkRoundTrip(MUL_CLOCK_MAP_HORIZON_DTU, ppb);
    checkRoundTrip(-(int64_t)MUL_CLOCK_MAP_HORIZON_DTU, ppb);
  }
}

void test_mulMappingQuantizedSubTickCorrectionsAndInverseConsistency(void) {
  converge(5000, UINT64_C(0xead1234567), UINT64_C(0xfedcba9876));
  for (int64_t delta = -300001; delta <= 300001; delta += 997) {
    checkRoundTrip(delta, 5000);
  }
  mulClockMapping_t mapped;
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, clockState.referenceTx + 1, nowMs, &mapped));
  TEST_ASSERT_EQUAL_INT64(1, mulTimestampDifference(mapped.timestamp, clockState.localRx));
}

void test_mulMappingRejectsOutOfHorizonAndAmbiguousIntervalsWithoutOutput(void) {
  converge(5000, 0, MUL_TIMESTAMP_MASK);
  mulClockMapping_t mapping;
  memset(&mapping, 0xa5, sizeof(mapping));
  mulClockMapping_t original = mapping;
  const int64_t deltas[] = {MUL_CLOCK_MAP_HORIZON_DTU + 1,
    -(int64_t)MUL_CLOCK_MAP_HORIZON_DTU - 1, INT64_C(1) << 39, -(INT64_C(1) << 39),
    INT64_C(1) << 38};
  for (unsigned int i = 0; i < sizeof(deltas)/sizeof(deltas[0]); i++) {
    TEST_ASSERT_FALSE(mulClockMapRefToFollower(&clockState,
      mulTimestampMask(clockState.referenceTx + deltas[i]), nowMs, &mapping));
    TEST_ASSERT_EQUAL_MEMORY(&original, &mapping, sizeof(mapping));
  }
  TEST_ASSERT_FALSE(mulClockMapFollowerToRef(&clockState,
    mulTimestampMask(clockState.localRx + MUL_CLOCK_MAP_HORIZON_DTU + SECOND/100), nowMs, &mapping));
  TEST_ASSERT_FALSE(mulClockMapFollowerToRef(&clockState,
    mulTimestampMask(clockState.localRx + (INT64_C(1) << 39)), nowMs, &mapping));
  TEST_ASSERT_EQUAL_MEMORY(&original, &mapping, sizeof(mapping));
}

void test_mulMappingStaleAgeAndUptimeWrapAreIndependentOfRequestedCoordinate(void) {
  nowMs = UINT32_MAX - 8000;
  converge(-1612, 0, UINT64_C(0x923456789a));
  mulClockMapping_t mapping;
  TEST_ASSERT_EQUAL_INT(mulClockMapValid, mulClockMapStatus(&clockState, nowMs+2000));
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, clockState.referenceTx, nowMs+2000, &mapping));
  TEST_ASSERT_EQUAL_UINT32(2000, mapping.ageMs);
  TEST_ASSERT_EQUAL_UINT64(0, mapping.extrapolationDtu);
  TEST_ASSERT_EQUAL_INT(mulClockMapStale, mulClockMapStatus(&clockState, nowMs+2001));
  TEST_ASSERT_FALSE(mulClockMapRefToFollower(&clockState, clockState.referenceTx, nowMs+2001, &mapping));
  TEST_ASSERT_FALSE(mulClockMapFollowerToRef(&clockState, clockState.localRx, nowMs+18000, &mapping));
}

void test_mulMappingInvalidWarmupAndInvalidSlopeCannotConvert(void) {
  mulClockMapping_t mapping;
  TEST_ASSERT_EQUAL_INT(mulClockMapInvalid, mulClockMapStatus(&clockState, nowMs));
  TEST_ASSERT_FALSE(mulClockMapRefToFollower(&clockState, 0, nowMs, &mapping));
  TEST_ASSERT_EQUAL_INT(mulClockBaseline, mulClockObserve(&clockState, &reference, 0, nowMs, true));
  TEST_ASSERT_EQUAL_INT(mulClockMapWarmup, mulClockMapStatus(&clockState, nowMs));
  TEST_ASSERT_FALSE(mulClockMapFollowerToRef(&clockState, 0, nowMs, &mapping));
  setUp();
  converge(-10000, 0, 0);
  const int32_t invalid[] = {MUL_CLOCK_MAX_PPB+1, -MUL_CLOCK_MAX_PPB-1, INT32_MAX, INT32_MIN};
  for (unsigned int i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
    clockState.skewPpb = invalid[i];
    TEST_ASSERT_EQUAL_INT(mulClockMapInvalid, mulClockMapStatus(&clockState, nowMs));
    TEST_ASSERT_FALSE(mulClockMapRefToFollower(&clockState, 0, nowMs, &mapping));
    TEST_ASSERT_FALSE(mulClockMapFollowerToRef(&clockState, 0, nowMs, &mapping));
  }
  TEST_ASSERT_EQUAL_INT(mulClockMapInvalid, mulClockMapStatus(NULL, nowMs));
  TEST_ASSERT_FALSE(mulClockMapRefToFollower(NULL, 0, nowMs, &mapping));
  TEST_ASSERT_FALSE(mulClockMapFollowerToRef(NULL, 0, nowMs, &mapping));
  TEST_ASSERT_FALSE(mulClockMapRefToFollower(&clockState, 0, nowMs, NULL));
}

void test_mulMappingReanchorsAfterEachAcceptedObservationOnly(void) {
  converge(5000, UINT64_C(0xa123456789), UINT64_C(0xffba987654));
  mulClockMapping_t before, after;
  uint64_t nextTx = mulTimestampMask(clockState.referenceTx + SECOND);
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, nextTx, nowMs+1000, &before));
  nowMs += 1000;
  reference.sequence++;
  reference.txTimestamp = nextTx;
  TEST_ASSERT_EQUAL_INT(mulClockSample, mulClockObserve(&clockState, &reference,
    mulTimestampMask(before.timestamp + 1000), nowMs, true));
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, nextTx, nowMs, &after));
  TEST_ASSERT_EQUAL_INT64(1000, mulTimestampDifference(after.timestamp, before.timestamp));
  TEST_ASSERT_EQUAL_UINT32(0, after.ageMs);
  mulClock_t original = clockState;
  TEST_ASSERT_EQUAL_INT(mulClockIgnored, mulClockObserve(&clockState, &reference, 0, nowMs+100, true));
  reference.sequence--;
  TEST_ASSERT_EQUAL_INT(mulClockIgnored, mulClockObserve(&clockState, &reference, 0, nowMs+200, true));
  TEST_ASSERT_EQUAL_MEMORY(&original, &clockState, sizeof(original));
}

void test_mulMappingMissingPacketsRetainMultiPeriodRateAndReanchor(void) {
  converge(-10000, 0, UINT64_C(0xab12345678));
  reference.sequence += 2;
  reference.txTimestamp = mulTimestampMask(clockState.referenceTx + 2*SECOND);
  uint64_t nextRx = mulTimestampMask(clockState.localRx + 2*SECOND - 2*SECOND/100000);
  nowMs += 2000;
  TEST_ASSERT_EQUAL_INT(mulClockSample, mulClockObserve(&clockState, &reference, nextRx, nowMs, true));
  TEST_ASSERT_EQUAL_INT(mulClockMapValid, mulClockMapStatus(&clockState, nowMs));
  checkRoundTrip(SECOND/2, -10000);
}

void test_mulMappingSkewInvalidationLossResetAndRestartNeedNewConvergence(void) {
  converge(5000, 0, 0);
  const int32_t scattered[] = {-100000, -50000, 0, 50000, 100000};
  for (unsigned int i = 0; i < sizeof(scattered)/sizeof(scattered[0]); i++) {
    reference.sequence++;
    reference.txTimestamp = mulTimestampMask(clockState.referenceTx + SECOND);
    nowMs += 1000;
    uint64_t nextRx = mulTimestampMask(clockState.localRx +
      llroundl((long double)SECOND * (1.0L + scattered[i]/1e9L)));
    TEST_ASSERT_EQUAL_INT(mulClockSample, mulClockObserve(&clockState, &reference, nextRx, nowMs, true));
  }
  TEST_ASSERT_EQUAL_INT(mulClockWarmup, clockState.status);
  mulClockMapping_t mapping;
  TEST_ASSERT_FALSE(mulClockMapRefToFollower(&clockState, clockState.referenceTx, nowMs, &mapping));
  mulClockReset(&clockState);
  TEST_ASSERT_EQUAL_INT(mulClockMapInvalid, mulClockMapStatus(&clockState, nowMs));
  converge(-10000, 0, UINT64_C(0xa123456789));
  TEST_ASSERT_EQUAL_INT(mulClockMapValid, mulClockMapStatus(&clockState, nowMs));
  reference.sequence++;
  reference.txTimestamp = 0; // Discontinuity while sequence moves forward.
  nowMs += 1000;
  TEST_ASSERT_EQUAL_INT(mulClockDiscontinuity, mulClockObserve(&clockState, &reference, 123, nowMs, true));
  TEST_ASSERT_FALSE(mulClockMapRefToFollower(&clockState, 0, nowMs, &mapping));
}

void test_mulMappingUncertaintyIsUnknownPhysicalBoundWithGrowingNumericalAllowance(void) {
  converge(5000, 0, 0);
  const uint64_t deltas[] = {0, SECOND/2, SECOND, 2*SECOND};
  uint64_t previous = 0;
  for (unsigned int i = 0; i < sizeof(deltas)/sizeof(deltas[0]); i++) {
    mulClockMapping_t mapping;
    TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState,
      mulTimestampMask(clockState.referenceTx + deltas[i]), nowMs+100, &mapping));
    TEST_ASSERT_EQUAL_UINT32(mulClockUnknownAll, mapping.unknownComponents);
    TEST_ASSERT_BITS(mulClockUnknownRate | mulClockUnknownReferenceMeasurement |
      mulClockUnknownFollowerMeasurement | mulClockUnknownOrigin | mulClockUnknownPropagation |
      mulClockUnknownPropagationChange | mulClockUnknownExtrapolation | mulClockUnknownTotal,
      mulClockUnknownAll, mapping.unknownComponents);
    TEST_ASSERT_FALSE(mapping.syncCalibrated);
    TEST_ASSERT_EQUAL_UINT64(deltas[i], mapping.extrapolationDtu);
    TEST_ASSERT_TRUE(mapping.numericalBoundDtu > previous);
    previous = mapping.numericalBoundDtu;
    TEST_ASSERT_EQUAL_UINT32(0, mapping.observedRateSpreadPpb); // Zero spread is still UNKNOWN.
  }
}

void test_mulMappingCannotDistinguishOffsetFromPropagationDelay(void) {
  // a=1.000005; (b=12345,tau=200000) and (b=112345.5,tau=100000)
  // both give c=b+a*tau=212346. These worlds produce identical one-way data.
  long double a = 1.000005L;
  long double c1 = 12345.0L + a*200000.0L;
  long double c2 = 112345.5L + a*100000.0L;
  TEST_ASSERT_TRUE(fabsl(c1-c2) < 0.000001L);
  uint64_t start = UINT64_C(0x123456789a);
  converge(5000, start, (uint64_t)llroundl(a*start + c1));
  mulClockMapping_t first;
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, reference.txTimestamp, nowMs, &first));
  setUp();
  converge(5000, start, (uint64_t)llroundl(a*start + c2));
  mulClockMapping_t second;
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&clockState, reference.txTimestamp, nowMs, &second));
  TEST_ASSERT_EQUAL_UINT64(first.timestamp, second.timestamp);
  TEST_ASSERT_BITS_HIGH(mulClockUnknownPropagation, second.unknownComponents);
  TEST_ASSERT_FALSE(first.syncCalibrated);
  TEST_ASSERT_FALSE(second.syncCalibrated);
}

void test_mulMappingLongRunReanchorsWithoutNumericalDrift(void) {
  converge(-10000, MUL_TIMESTAMP_MASK - SECOND, UINT64_C(0xfedcba9876));
  for (uint32_t i = 0; i < 100000; i++) {
    checkRoundTrip(SECOND/2 + (i % 1000), -10000);
    uint64_t nextRx = mulTimestampMask(clockState.localRx + SECOND - SECOND/100000);
    reference.sequence++;
    reference.txTimestamp = mulTimestampMask(clockState.referenceTx + SECOND);
    nowMs += 1000;
    TEST_ASSERT_EQUAL_INT(mulClockSample, mulClockObserve(&clockState, &reference, nextRx, nowMs, true));
  }
}
