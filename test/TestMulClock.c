/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_clock.h"
#include "mul_common.h"

#include <string.h>

#define SECOND UINT64_C(63897600000)
static mulClock_t clockState;
static mulTestPacket_t reference;
static uint64_t rx;
static uint32_t ms;

void setUp(void) {
  mulClockReset(&clockState);
  memset(&reference, 0, sizeof(reference));
  const uint8_t address[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  memcpy(reference.source, address, 8);
  reference.txTimestamp = UINT64_C(0x735926831a);
  rx = UINT64_C(0xe731953342); // Deliberately unrelated origins.
  ms = 0;
}
void tearDown(void) {}

static mulClockResult_t observe(void) {
  return mulClockObserve(&clockState, &reference, rx, ms, true);
}

static mulClockResult_t step(unsigned int periods, int32_t ppb) {
  uint64_t remoteDelta = SECOND * periods;
  int64_t adjustment = ((int64_t)remoteDelta * ppb) / INT64_C(1000000000);
  reference.sequence += periods;
  reference.txTimestamp = mulTimestampMask(reference.txTimestamp + remoteDelta);
  rx = mulTimestampMask(rx + remoteDelta + adjustment);
  ms += 1000 * periods;
  return observe();
}

static void checkRate(int32_t ppb) {
  TEST_ASSERT_EQUAL_INT(mulClockBaseline, observe());
  for (unsigned int i = 0; i < 20; i++) {
    TEST_ASSERT_EQUAL_INT(mulClockSample, step(1, ppb));
    TEST_ASSERT_EQUAL_INT32(ppb, clockState.skewPpb);
    TEST_ASSERT_EQUAL_INT(i < 4 ? mulClockWarmup : mulClockValid, clockState.status);
  }
  TEST_ASSERT_EQUAL_UINT32(20, clockState.samples);
}

void test_mulClockZeroSkewConvergesWithoutOffsetDependence(void) { checkRate(0); }
void test_mulClockPositiveFivePpmMeansFasterLocalClock(void) { checkRate(5000); }
void test_mulClockNegativeTenPpmMeansSlowerLocalClock(void) { checkRate(-10000); }
void test_mulClockLargerPlausiblePositiveSkew(void) { checkRate(150000); }
void test_mulClockLargerPlausibleNegativeSkew(void) { checkRate(-150000); }
void test_mulClockSubPpmPrecisionNearUnity(void) { checkRate(1); }

void test_mulClockCounterAndSequenceAndUptimeWrap(void) {
  reference.sequence = UINT32_MAX - 2;
  reference.txTimestamp = MUL_TIMESTAMP_MASK - SECOND/2;
  rx = MUL_TIMESTAMP_MASK - SECOND/4;
  ms = UINT32_MAX - 100;
  checkRate(5000);
  TEST_ASSERT_EQUAL_UINT32(17, reference.sequence);
}

void test_mulClockMissingPacketUsesActualMultiSecondIntervals(void) {
  observe();
  for (unsigned int i = 0; i < 8; i++) {
    TEST_ASSERT_EQUAL_INT(mulClockSample, step(2, -10000));
  }
  TEST_ASSERT_EQUAL_INT32(-10000, clockState.skewPpb);
  TEST_ASSERT_EQUAL_UINT32(8, clockState.samples);
  TEST_ASSERT_EQUAL_INT(mulClockValid, clockState.status);
}

void test_mulClockDuplicateAndOldPacketsCannotChangeAnyEstimatorState(void) {
  checkRate(5000);
  mulClock_t before = clockState;
  rx += 123;
  ms += 300;
  TEST_ASSERT_EQUAL_INT(mulClockIgnored, observe());
  TEST_ASSERT_EQUAL_MEMORY(&before, &clockState, sizeof(before));
  reference.sequence--;
  TEST_ASSERT_EQUAL_INT(mulClockIgnored, observe());
  TEST_ASSERT_EQUAL_MEMORY(&before, &clockState, sizeof(before));
  reference.sequence = before.sequence + UINT32_C(0x80000000);
  TEST_ASSERT_EQUAL_INT(mulClockIgnored, observe());
  TEST_ASSERT_EQUAL_MEMORY(&before, &clockState, sizeof(before));
}

void test_mulClockInvalidOrWrongFullIdentityCannotAlterState(void) {
  checkRate(5000);
  mulClock_t before = clockState;
  TEST_ASSERT_EQUAL_INT(mulClockIgnored,
    mulClockObserve(&clockState, &reference, rx, ms, false));
  TEST_ASSERT_EQUAL_INT(mulClockIgnored,
    mulClockObserve(&clockState, NULL, rx, ms, true));
  reference.source[1] = 1;
  TEST_ASSERT_EQUAL_INT(mulClockIgnored, observe());
  reference.source[1] = 0;
  reference.source[0] = 0x85;
  TEST_ASSERT_EQUAL_INT(mulClockIgnored, observe());
  TEST_ASSERT_EQUAL_MEMORY(&before, &clockState, sizeof(before));
}

static void checkInvalidInterval(int64_t remoteDelta, int64_t localDelta, uint32_t age) {
  setUp();
  checkRate(5000);
  reference.sequence++;
  reference.txTimestamp = mulTimestampMask(reference.txTimestamp + remoteDelta);
  rx = mulTimestampMask(rx + localDelta);
  ms += age;
  TEST_ASSERT_EQUAL_INT(mulClockDiscontinuity, observe());
  TEST_ASSERT_EQUAL_INT(mulClockWarmup, clockState.status);
  TEST_ASSERT_EQUAL_UINT32(0, clockState.samples);
  TEST_ASSERT_EQUAL_INT32(0, clockState.skewPpb);
  for (int i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL_INT(mulClockSample, step(1, -10000));
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid, clockState.status);
  TEST_ASSERT_EQUAL_INT32(-10000, clockState.skewPpb);
}

void test_mulClockRejectsZeroNegativeHalfPeriodAndTooLongIntervals(void) {
  checkInvalidInterval(0, SECOND, 1000);
  checkInvalidInterval(SECOND, 0, 1000);
  checkInvalidInterval(-1, SECOND, 1000);
  checkInvalidInterval(SECOND, -1, 1000);
  checkInvalidInterval(INT64_C(1) << 39, INT64_C(1) << 39, 1000);
  checkInvalidInterval(4*SECOND, 4*SECOND, 1000);
  checkInvalidInterval(SECOND, SECOND, MUL_SOURCE_TIMEOUT_MS);
  checkInvalidInterval(SECOND, SECOND, 18000); // Full-wrap alias despite small deltas.
  checkInvalidInterval(SECOND, SECOND, 0);
}

void test_mulClockRejectsImplausibleRatiosAndReferenceTimestampRestart(void) {
  checkInvalidInterval(SECOND, SECOND + SECOND/1000, 1000); // +1000 ppm.
  checkInvalidInterval(SECOND, SECOND - SECOND/1000, 1000); // -1000 ppm.
  checkInvalidInterval(SECOND/2, SECOND/2, 1000); // Equal ratio, wrong ref cadence.
  checkInvalidInterval(SECOND, 3*SECOND, 1000);
}

void test_mulClockSequenceGapCannotAliasToOneSecond(void) {
  observe();
  reference.sequence += 18;
  reference.txTimestamp += SECOND;
  rx += SECOND;
  ms += 1000;
  TEST_ASSERT_EQUAL_INT(mulClockDiscontinuity, observe());
  TEST_ASSERT_EQUAL_UINT32(0, clockState.samples);
}

void test_mulClockMedianRejectsTwoPlausibleOutliersAndAdaptsToPersistentChange(void) {
  checkRate(5000);
  step(1, 150000);
  TEST_ASSERT_EQUAL_INT32(5000, clockState.skewPpb);
  step(1, -150000);
  TEST_ASSERT_EQUAL_INT32(5000, clockState.skewPpb);
  for (unsigned int i = 0; i < 5; i++) { step(1, -10000); }
  TEST_ASSERT_EQUAL_INT32(-10000, clockState.skewPpb);
  TEST_ASSERT_EQUAL_INT(mulClockValid, clockState.status);
}

void test_mulClockNeedsStableMajorityAndInvalidatesScatteredHistory(void) {
  checkRate(5000);
  const int32_t scattered[] = {-100000, -50000, 0, 50000, 100000};
  for (unsigned int i = 0; i < 5; i++) { step(1, scattered[i]); }
  TEST_ASSERT_EQUAL_INT(mulClockWarmup, clockState.status);
  for (unsigned int i = 0; i < 5; i++) { step(1, 5000 + (int)i*100); }
  TEST_ASSERT_EQUAL_INT(mulClockValid, clockState.status);
  TEST_ASSERT_INT32_WITHIN(500, 5000, clockState.skewPpb);
}

void test_mulClockExplicitLossResetAndReferenceRestartReconverge(void) {
  checkRate(5000);
  mulClockReset(&clockState);
  TEST_ASSERT_EQUAL_INT(mulClockSearching, clockState.status);
  TEST_ASSERT_FALSE(clockState.hasBaseline);
  TEST_ASSERT_EQUAL_UINT32(0, clockState.samples);
  reference.sequence = 0;
  reference.txTimestamp = 0;
  ms += 10000;
  checkRate(-10000);
}

void test_mulClockLongRunHasNoNumericalDriftOrCountOverflow(void) {
  observe();
  for (uint32_t i = 0; i < 100000; i++) {
    TEST_ASSERT_EQUAL_INT(mulClockSample, step(i % 9 == 0 ? 2 : 1, 5000));
    TEST_ASSERT_EQUAL_INT32(5000, clockState.skewPpb);
  }
  clockState.samples = UINT32_MAX;
  TEST_ASSERT_EQUAL_INT(mulClockSample, step(1, 5000));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, clockState.samples);
}
