/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_relation.h"
#include "mul_common.h"
#include "mul_clock.h"
#include <string.h>
static const uint8_t follower[8]={0x85,0,0,0,0,0,0xcf,0xbc};
static mulTestPacket_t packet;
static mulRelation_t relation;
void setUp(void) {
  packet=(mulTestPacket_t){.sequence=900,.txTimestamp=UINT64_C(63897600)*500}; memcpy(packet.source,follower,8);
  relation=(mulRelation_t){.epoch=9,.referenceSequence=6,.twrSequence=5,
    .originAgeMs=500,.delayAgeMs=850,.remainingMs=150,.available=true,.powerCorrectedTwr=true};
}
void tearDown(void) {}
void test_mulRelationRoundTripPreservesBaseAndFixedDirection(void) {
  uint8_t frame[MUL_RELATION_FRAME_LENGTH]; mulRelationEncode(frame,&packet,&relation);
  mulRelation_t r; mulTestPacket_t p;
  TEST_ASSERT_TRUE(mulBroadcastDecode(frame,sizeof(frame),&p,&r));
  TEST_ASSERT_EQUAL_UINT32(900,p.sequence); TEST_ASSERT_EQUAL_UINT64(packet.txTimestamp,p.txTimestamp);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(follower,p.source,8); TEST_ASSERT_EQUAL_UINT64(9,r.epoch);
  TEST_ASSERT_EQUAL_UINT32(6,r.referenceSequence); TEST_ASSERT_EQUAL_UINT32(5,r.twrSequence);
  TEST_ASSERT_FALSE(mulDecodeTestPacket(frame,sizeof(frame),&p));
  frame[13]=0x84; TEST_ASSERT_FALSE(mulBroadcastDecode(frame,sizeof(frame),&p,&r));
}
void test_mulRelationRejectsLengthsVersionsFlagsAndStaleContracts(void) {
  uint8_t frame[MUL_RELATION_FRAME_LENGTH]; mulRelationEncode(frame,&packet,&relation);
  mulRelation_t r; mulTestPacket_t p;
  for (unsigned int i=0;i<100;i++) if (i!=sizeof(frame)) { TEST_ASSERT_FALSE(mulBroadcastDecode(frame,i,&p,&r)); }
  const unsigned int corrupt[]={0,1,2,3,5,20,24,34,35,70,73};
  for (unsigned int i=0;i<sizeof(corrupt)/sizeof(corrupt[0]);i++) {
    frame[corrupt[i]]^=0x80; TEST_ASSERT_FALSE(mulBroadcastDecode(frame,sizeof(frame),&p,&r)); frame[corrupt[i]]^=0x80;
  }
  relation.delayAgeMs=1001; mulRelationEncode(frame,&packet,&relation);
  TEST_ASSERT_FALSE(mulBroadcastDecode(frame,sizeof(frame),&p,&r));
  relation.delayAgeMs=850; relation.epoch=0; mulRelationEncode(frame,&packet,&relation);
  TEST_ASSERT_FALSE(mulBroadcastDecode(frame,sizeof(frame),&p,&r));
}
void test_mulRelationUnavailableAndLegacyCannotReuseMapping(void) {
  relation=(mulRelation_t){.epoch=10,.referenceSequence=7};
  uint8_t frame[MUL_RELATION_FRAME_LENGTH]; mulRelationEncode(frame,&packet,&relation);
  mulRelation_t r; mulTestPacket_t p;
  TEST_ASSERT_TRUE(mulBroadcastDecode(frame,sizeof(frame),&p,&r)); TEST_ASSERT_FALSE(r.available);
  mulEncodeTestPacket(frame,follower,901,1);
  TEST_ASSERT_TRUE(mulBroadcastDecode(frame,34,&p,&r)); TEST_ASSERT_FALSE(r.available); TEST_ASSERT_EQUAL_UINT64(0,r.epoch);
  TEST_ASSERT_FALSE(mulBroadcastDecode(NULL,34,&p,&r));
}
void test_mulRelationInverseSignedRatesWrapAndExpiry(void) {
  for (int32_t rate=-150000;rate<=150000;rate+=150000) {
    relation.skewPpb=rate; relation.referenceOrigin=MUL_TIMESTAMP_MASK-10;
    relation.followerOrigin=MUL_TIMESTAMP_MASK-100;
    int64_t d=INT64_C(63897600)*500;
    uint64_t tx=mulTimestampMask(relation.followerOrigin+d+d*rate/1000000000);
    uint64_t ref=77;
    TEST_ASSERT_TRUE(mulRelationInverse(&relation,tx,0,&ref));
    TEST_ASSERT_UINT64_WITHIN(1,mulTimestampMask(relation.referenceOrigin+d),ref);
    TEST_ASSERT_FALSE(mulRelationInverse(&relation,tx,151,&ref));
    TEST_ASSERT_FALSE(mulRelationInverse(&relation,mulTimestampMask(relation.followerOrigin+(UINT64_C(1)<<39)),0,&ref));
  }
}

void test_mulRelationPublicationUsesSimultaneousOriginAndTxTimeExpiry(void) {
  mulClock_t clock={.hasBaseline=true,.sequence=6,.epoch=9,.referenceTx=100,
    .localRx=1000000,.observedMs=6000,.status=mulClockValid,.skewPpb=5000};
  mulClockDelay_t delay={.epoch=9,.oneWayDtu=2131,.units=mulClockDelayReferenceDtu,
    .measuredMs=5650,.referenceSequence=5,.maxAgeMs=1000,.valid=true,
    .measurementBoundDtu=UINT64_MAX,.motionBoundDtuPerMs=UINT64_MAX};
  const uint8_t ref[8]={0x84,0,0,0,0,0,0xcf,0xbc};
  memcpy(delay.reference,ref,8); memcpy(delay.follower,follower,8);
  uint64_t tx=mulTimestampMask(clock.localRx+500*UINT64_C(63897600));
  mulRelation_t r=mulRelationPublish(&clock,&delay,tx,clock.localRx,6000,true);
  TEST_ASSERT_TRUE(r.available); TEST_ASSERT_EQUAL_UINT64(clock.localRx-2131,r.followerOrigin);
  TEST_ASSERT_TRUE(r.delayAgeMs>=850 && r.remainingMs<=150);
  TEST_ASSERT_EQUAL_UINT32(6,r.referenceSequence); TEST_ASSERT_EQUAL_UINT32(5,r.twrSequence);
  uint64_t converted; TEST_ASSERT_TRUE(mulRelationInverse(&r,tx,0,&converted));
  // Available at RX, stale by scheduled follower TX: must publish UNAVAILABLE.
  delay.measuredMs=5200;
  TEST_ASSERT_FALSE(mulRelationPublish(&clock,&delay,tx,clock.localRx,6000,true).available);
  delay.measuredMs=5650; delay.epoch++;
  TEST_ASSERT_FALSE(mulRelationPublish(&clock,&delay,tx,clock.localRx,6000,true).available);
  TEST_ASSERT_FALSE(mulRelationPublish(&clock,NULL,tx,clock.localRx,6000,true).available);
}
