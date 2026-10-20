/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_localization.h"
#include "mul_downlink.h"
#include "mul_relation.h"
#include "mul_observation.h"
#include "mul_clock.h"
#include "mul_common.h"
#include <string.h>
#define MS UINT64_C(63897600)
static mulPair_t pair;
static mulClock_t rate;
static mulRelation_t relation;
static mulLocalization_t observation;
static uint32_t now;
void setUp(void) {
  memset(&pair,0,sizeof(pair));
  const uint8_t ref[8]={0x84,0,0,0,0,0,0xcf,0xbc},fol[8]={0x85,0,0,0,0,0,0xcf,0xbc};
  memcpy(pair.reference.sourceClock,ref,8); memcpy(pair.follower.sourceClock,fol,8);
  pair.reference.receiverClock[0]=pair.follower.receiverClock[0]=0x86;
  pair.reference.valid=pair.follower.valid=true;
  pair.reference.generation=2; pair.follower.generation=3;
  pair.reference.source.sequence=6; pair.follower.source.sequence=987;
  pair.reference.source.txTimestamp=MUL_TIMESTAMP_MASK-100*MS;
  pair.follower.source.txTimestamp=MUL_FOLLOWER_SLOT_DTU;
  pair.reference.source.rxTimestamp=MUL_TIMESTAMP_MASK-100*MS;
  pair.follower.source.rxTimestamp=mulTimestampMask(pair.reference.source.rxTimestamp+MUL_FOLLOWER_SLOT_DTU+100);
  pair.rawArrivalDifferenceDtu=MUL_FOLLOWER_SLOT_DTU+100;
  pair.reference.acceptedMs=6000; now=6000+MUL_FOLLOWER_SLOT_MS;
  pair.follower.acceptedMs=now;
  relation=(mulRelation_t){.available=true,.epoch=7,.referenceSequence=6,.twrSequence=5,
    .referenceOrigin=pair.reference.source.txTimestamp,.originAgeMs=MUL_FOLLOWER_SLOT_MS,
    .delayAgeMs=350+MUL_FOLLOWER_SLOT_MS,.remainingMs=150,.delayDtu=426,.maxAgeMs=MUL_PROP_MAX_AGE_MS,
    .version=2,.delayState=mulPropagationFresh};
  rate=(mulClock_t){.hasBaseline=true,.status=mulClockValid,.sequence=6,
    .referenceTx=pair.reference.source.txTimestamp,.localRx=pair.reference.source.rxTimestamp,.observedMs=6000};
}
void tearDown(void) {}
static void observe(bool stationary) {
  mulLocalizationObserve(&observation,&pair,&relation,&rate,now,1,stationary);
}
void test_mulLocalizationRecordOwnsCompactIdentityTimeGenerationAndSignedMeasurement(void) {
  observe(true);
  TEST_ASSERT_TRUE(sizeof(observation)<=96);
  TEST_ASSERT_EQUAL_INT(mulDownlinkAvailable,observation.status);
  TEST_ASSERT_EQUAL_INT32(100,observation.tdoaDtu); TEST_ASSERT_INT32_WITHIN(1,469,observation.rangeDifferenceMm);
  TEST_ASSERT_EQUAL_UINT32(987,observation.followerSequence); TEST_ASSERT_EQUAL_UINT32(6,observation.referenceSequence);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pair.reference.sourceClock,observation.reference,8);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pair.follower.sourceClock,observation.follower,8);
  TEST_ASSERT_EQUAL_UINT32(2,observation.referenceGeneration); TEST_ASSERT_EQUAL_UINT32(3,observation.followerGeneration);
  TEST_ASSERT_EQUAL_UINT64(pair.follower.source.rxTimestamp,observation.receiverTimestamp);
  TEST_ASSERT_EQUAL_UINT32(now,observation.observedMs); TEST_ASSERT_EQUAL_UINT64(7,observation.followerEpoch);
  TEST_ASSERT_EQUAL_UINT16(426,observation.delayDtu); TEST_ASSERT_EQUAL_UINT32(351+MUL_FOLLOWER_SLOT_MS,observation.delayAgeMs);
  TEST_ASSERT_EQUAL_UINT16(MUL_FOLLOWER_SLOT_MS,observation.slotMs); TEST_ASSERT_TRUE(observation.associationValid);
  TEST_ASSERT_TRUE(observation.clockValid); TEST_ASSERT_EQUAL_INT(mulRateStationary,observation.rateState);
  TEST_ASSERT_EQUAL_UINT16(MUL_FOLLOWER_SLOT_MS+1,observation.clockAgeMs);
  TEST_ASSERT_TRUE(observation.unknownComponents & mulQualityTwrUnknown);
  TEST_ASSERT_TRUE(observation.unknownComponents & mulQualityOtherUnknown);
}
void test_mulLocalizationUnavailableAlwaysMasksNumericsAndPreservesReason(void) {
  for (unsigned int i=0;i<5;i++) {
    setUp();
    if (i==0) relation.available=false;
    if (i==1) rate.status=mulClockWarmup;
    if (i==2) rate.sequence++;
    if (i==3) { pair.rawArrivalDifferenceDtu+=100000; pair.follower.source.rxTimestamp=mulTimestampMask(pair.follower.source.rxTimestamp+100000); }
    if (i==4) pair.follower.generation=0;
    observe(true);
    TEST_ASSERT_TRUE(observation.status!=mulDownlinkAvailable);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN,observation.tdoaDtu); TEST_ASSERT_EQUAL_INT32(INT32_MIN,observation.rangeDifferenceMm);
  }
  setUp(); observe(false);
  TEST_ASSERT_EQUAL_INT(mulDownlinkNoRate,observation.status);
  TEST_ASSERT_EQUAL_INT(mulRateUnavailable,observation.rateState);
  TEST_ASSERT_EQUAL_INT32(INT32_MIN,observation.ratePpb);
}
void test_mulLocalizationMotionBoundRoundsOutwardsAndNeverImpliesTotalCalibration(void) {
  for (unsigned int age=0;age<=6000;age+=1000) {
    uint32_t mm,dtu; mulPropagationMotionBound(age,&mm,&dtu);
#if MUL_RELATIVE_SPEED_MM_S >= 0
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(((uint64_t)MUL_RELATIVE_SPEED_MM_S*age+999)/1000),mm);
    // Independently compare physical seconds/metres to nominal UWB ticks.
    long double ideal=(long double)MUL_RELATIVE_SPEED_MM_S*age/1000.0L*63897600000.0L/299792458000.0L;
    TEST_ASSERT_TRUE((long double)dtu>=ideal);
    TEST_ASSERT_TRUE((long double)dtu-ideal<2);
#else
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,mm); TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,dtu);
#endif
  }
  observe(true);
#if MUL_RELATIVE_SPEED_MM_S >= 0
  TEST_ASSERT_FALSE(observation.unknownComponents & mulQualityMotionUnknown);
#else
  TEST_ASSERT_TRUE(observation.unknownComponents & mulQualityMotionUnknown);
#endif
  TEST_ASSERT_TRUE(observation.unknownComponents & mulQualityClockUnknown);
  TEST_ASSERT_TRUE(observation.unknownComponents & mulQualityRateUnknown);
  TEST_ASSERT_TRUE(observation.unknownComponents & mulQualityPowerUnknown);
}
void test_mulLocalizationUnavailableStateAndOriginalAgeSurviveTransportAndProcessing(void) {
  relation.available=false; relation.delayState=mulPropagationReused;
  observe(true); TEST_ASSERT_EQUAL_INT(mulPropagationReused,observation.delayState);
  TEST_ASSERT_EQUAL_UINT32(351+MUL_FOLLOWER_SLOT_MS,observation.delayAgeMs);
  relation.delayAgeMs=MUL_PROP_MAX_AGE_MS; observe(true);
  TEST_ASSERT_EQUAL_INT(mulPropagationExpired,observation.delayState);
  TEST_ASSERT_EQUAL_INT(mulDownlinkNoRelation,observation.status);
  relation.delayState=mulPropagationInvalidated; observe(true);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,observation.delayAgeMs);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX,observation.delayDtu);
  relation.delayState=mulPropagationNone; observe(true);
  TEST_ASSERT_EQUAL_INT(mulPropagationNone,observation.delayState);
}
void test_mulLocalizationLegacyMetadataDoesNotInventPropagationEstimate(void) {
  relation.version=1; observe(true);
  TEST_ASSERT_EQUAL_INT(mulDownlinkAvailable,observation.status);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX,observation.delayDtu);
  TEST_ASSERT_EQUAL_UINT32(351+MUL_FOLLOWER_SLOT_MS,observation.delayAgeMs);
}
void test_mulLocalizationPrintWorstCaseHasStableFieldsAndUnknownPhysicalQuality(void) {
  observe(true); observation.referenceSequence=observation.followerSequence=UINT32_MAX;
  observation.referenceGeneration=observation.followerGeneration=UINT32_MAX;
  observation.observedMs=UINT32_MAX; observation.followerEpoch=UINT64_MAX;
  observation.tdoaDtu=-63898; observation.rangeDifferenceMm=-299999;
  observation.ratePpb=-200000; observation.delayAgeMs=65535; observation.delayDtu=63898;
  observation.motionBoundMm=65535000; observation.motionBoundDtu=13967774;
  mulLocalizationPrint(&observation);
}
