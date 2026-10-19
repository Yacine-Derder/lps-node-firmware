/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_downlink.h"
#include "mul_relation.h"
#include "mul_common.h"
#include "mul_clock.h"
#include "mul_observation.h"
#include <string.h>
static mulPair_t pair;
static mulRelation_t relation;
#define MS UINT64_C(63897600)
void setUp(void) {
  memset(&pair,0,sizeof(pair));
  const uint8_t ref[8]={0x84,0,0,0,0,0,0xcf,0xbc},fol[8]={0x85,0,0,0,0,0,0xcf,0xbc};
  const uint8_t receiver[8]={0x86,0,0,0,0,0,0xcf,0xbc};
  memcpy(pair.reference.sourceClock,ref,8); memcpy(pair.follower.sourceClock,fol,8);
  memcpy(pair.reference.receiverClock,receiver,8); memcpy(pair.follower.receiverClock,receiver,8);
  pair.reference.valid=pair.follower.valid=true;
  pair.reference.source.sequence=6; pair.follower.source.sequence=900;
  pair.reference.source.txTimestamp=MUL_TIMESTAMP_MASK-200*MS;
  pair.follower.source.txTimestamp=500*MS;
  pair.reference.acceptedMs=1000; pair.follower.acceptedMs=1500;
  relation=(mulRelation_t){.available=true,.epoch=10,.referenceSequence=6,.twrSequence=5,
    .referenceOrigin=pair.reference.source.txTimestamp,.originAgeMs=500,.delayAgeMs=850,.remainingMs=150};
}
void tearDown(void) {}
void test_mulDownlinkBindsCurrentPairWithoutCrossDomainSubtraction(void) {
  int64_t dt;
  TEST_ASSERT_TRUE(mulDownlinkRelation(&relation,&pair,1500,1,&dt));
  TEST_ASSERT_EQUAL_INT64(500*MS,dt); TEST_ASSERT_TRUE(pair.clockRelationAvailable);
  TEST_ASSERT_FALSE(pair.geometricTdoaAvailable);
}
void test_mulDownlinkRejectsStaleMissingWrongReferenceDomainsAndHardwareAge(void) {
  for (unsigned int i=0;i<6;i++) {
    setUp(); int64_t dt=123;
    if (i==0) relation.available=false;
    if (i==1) relation.referenceSequence++;
    if (i==2) relation.referenceOrigin++;
    if (i==3) pair.reference.receiverClock[1]++;
    if (i==4) relation.remainingMs=1;
    if (i==5) pair.follower.sourceClock[7]++;
    TEST_ASSERT_FALSE(mulDownlinkRelation(&relation,&pair,1500,2,&dt));
    TEST_ASSERT_EQUAL_INT64(123,dt); TEST_ASSERT_FALSE(pair.clockRelationAvailable);
  }
  setUp(); int64_t dt;
  TEST_ASSERT_FALSE(mulDownlinkRelation(&relation,&pair,1500,UINT32_MAX,&dt));
  TEST_ASSERT_FALSE(mulDownlinkRelation(&relation,&pair,1651,0,&dt));
}

static mulClock_t rlrRate;
// Physical reference units. Independent follower and RLR clocks, offsets near
// wrap, nonzero ref->fol propagation, known ref/follower->RLR delays.
static int64_t scaled(int64_t v,int32_t ppb) {
  int64_t p=v*ppb;
  return v+(p>=0 ? (p+500000000)/1000000000 : -((-p+500000000)/1000000000));
}
static void physicalCase(int32_t followerPpb,int32_t receiverPpb,int64_t rangeDifference) {
  setUp();
  int64_t slot=500*MS, interAlr=4263, refToRlr=4000, folToRlr=4000+rangeDifference;
  uint64_t refTx=pair.reference.source.txTimestamp;
  uint64_t offset=MUL_TIMESTAMP_MASK-200*MS;
  // Arrival aligned origin would be offset + a*interAlr; remove propagation.
  uint64_t arrival=mulTimestampMask(offset+scaled(interAlr,followerPpb));
  relation.followerOrigin=mulTimestampMask(arrival-scaled(interAlr,followerPpb));
  relation.skewPpb=followerPpb;
  pair.follower.source.txTimestamp=mulTimestampMask(relation.followerOrigin+scaled(slot,followerPpb));
  pair.reference.source.txTimestamp=refTx;
  pair.reference.source.rxTimestamp=mulTimestampMask(offset+scaled(refToRlr,receiverPpb));
  pair.follower.source.rxTimestamp=mulTimestampMask(offset+scaled(slot+folToRlr,receiverPpb));
  pair.rawArrivalDifferenceDtu=mulTimestampDifference(pair.follower.source.rxTimestamp,pair.reference.source.rxTimestamp);
  pair.reference.correctedRx=pair.reference.source.rxTimestamp;
  pair.follower.correctedRx=pair.follower.source.rxTimestamp;
  pair.reference.power.status=pair.follower.power.status=mulPowerApplied;
  pair.correctedAvailable=true; pair.correctedArrivalDifferenceDtu=pair.rawArrivalDifferenceDtu;
  rlrRate=(mulClock_t){.hasBaseline=true,.status=mulClockValid,.sequence=6,.referenceTx=refTx,
    .localRx=pair.reference.source.rxTimestamp,.observedMs=1000,.skewPpb=receiverPpb};
}

void test_mulDownlinkIndependentRatesSignedGeometryZeroAndNonzeroPropagationAtWrap(void) {
  const int32_t rates[]={-150000,0,150000}; const int64_t differences[]={-1000,0,1000};
  for (unsigned int a=0;a<3;a++) for (unsigned int b=0;b<3;b++) for (unsigned int c=0;c<3;c++) {
    physicalCase(rates[a],rates[b],differences[c]);
    mulDownlink_t d=mulDownlinkCompute(&relation,&pair,&rlrRate,1500,1,true);
    TEST_ASSERT_EQUAL_INT(mulDownlinkAvailable,d.status);
    TEST_ASSERT_TRUE(pair.geometricTdoaAvailable); TEST_ASSERT_EQUAL_UINT64(UINT64_MAX,d.physicalBoundDtu);
    TEST_ASSERT_INT64_WITHIN(2,differences[c],d.correctedTdoaRef);
    if (c==0) { TEST_ASSERT_TRUE(d.rangeDifferenceMm<0); }
    if (c==2) { TEST_ASSERT_TRUE(d.rangeDifferenceMm>0); }
  }
}
void test_mulDownlinkNeverAssumesEqualRatesOrUsesUnboundOrStaleModels(void) {
  for (unsigned int i=0;i<7;i++) {
    physicalCase(5000,-10000,1000);
    if (i==0) relation.available=false;
    if (i==1) rlrRate.status=mulClockWarmup;
    if (i==2) rlrRate.skewPpb=300000;
    if (i==3) rlrRate.sequence++;
    if (i==4) rlrRate.observedMs=0;
    if (i==5) relation.delayAgeMs=1001;
    mulDownlink_t d=mulDownlinkCompute(&relation,&pair,&rlrRate,1500,1,i!=6);
    TEST_ASSERT_TRUE(d.status!=mulDownlinkAvailable); TEST_ASSERT_FALSE(pair.geometricTdoaAvailable);
  }
}
void test_mulDownlinkRetainsRawAndCorrectedAndMissingPowerIsExplicit(void) {
  physicalCase(5000,-10000,-1000);
  pair.follower.source.rxTimestamp=mulTimestampMask(pair.follower.source.rxTimestamp+10);
  pair.rawArrivalDifferenceDtu+=10;
  mulDownlink_t d=mulDownlinkCompute(&relation,&pair,&rlrRate,1500,1,true);
  TEST_ASSERT_EQUAL_INT(mulDownlinkAvailable,d.status);
  TEST_ASSERT_INT64_WITHIN(2,-1000,d.correctedTdoaRef);
  TEST_ASSERT_INT64_WITHIN(2,-990,d.rawTdoaRef);
  pair.correctedAvailable=false;
  d=mulDownlinkCompute(&relation,&pair,&rlrRate,1500,1,true);
  TEST_ASSERT_EQUAL_INT(mulDownlinkAvailable,d.status); TEST_ASSERT_FALSE(d.correctedAvailable);
  TEST_ASSERT_INT64_WITHIN(2,-990,d.rawTdoaRef);
}
void test_mulDownlinkCadenceRateConvergesButMotionCannotBeIndependentlyIdentified(void) {
  mulClock_t estimate; mulClockReset(&estimate);
  mulTestPacket_t packet={.source={0x84,0,0,0,0,0,0xcf,0xbc}};
  for (unsigned int i=0;i<6;i++) {
    packet.sequence=i; packet.txTimestamp=i*1000*MS;
    // Constant radial path-delay change looks EXACTLY like +5 ppm clock skew.
    mulClockObserve(&estimate,&packet,mulTimestampMask(i*1000*MS+scaled(i*1000*MS,5000)-i*1000*MS),i*1000,true);
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid,estimate.status); TEST_ASSERT_EQUAL_INT32(5000,estimate.skewPpb);
  // Therefore a stationary declaration is mandatory; default policy rejects it.
  physicalCase(0,5000,0);
  TEST_ASSERT_EQUAL_INT(mulDownlinkNoRate,mulDownlinkCompute(&relation,&pair,&rlrRate,1500,1,false).status);
  // An observed abrupt path jump invalidates the reused cadence estimator.
  packet.sequence=6; packet.txTimestamp=6000*MS;
  mulClockObserve(&estimate,&packet,packet.txTimestamp+100*MS,6000,true);
  TEST_ASSERT_TRUE(estimate.status!=mulClockValid);
}
