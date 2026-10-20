/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_clock.h"
#include "mul_common.h"
#include "mul_relation.h"
#include "mul_timing.h"
#include <string.h>
#define MS UINT64_C(63897600)
static mulClock_t clockState;
static mulClockDelay_t delay;
void setUp(void) {
  clockState=(mulClock_t){.hasBaseline=true,.sequence=6,.epoch=7,.status=mulClockValid,
    .referenceTx=MUL_TIMESTAMP_MASK-100*MS,.localRx=MUL_TIMESTAMP_MASK-200*MS,.observedMs=6000};
  delay=(mulClockDelay_t){.reference={0x84,0,0,0,0,0,0xcf,0xbc},
    .follower={0x85,0,0,0,0,0,0xcf,0xbc},.epoch=7,.oneWayDtu=426,
    .measuredMs=5650,.referenceSequence=5,.maxAgeMs=MUL_PROP_MAX_AGE_MS,.valid=true,
    .measurementBoundDtu=UINT64_MAX,.motionBoundDtuPerMs=UINT64_MAX};
}
void tearDown(void) {}
static mulRelation_t publish(unsigned int seconds) {
  clockState.sequence=6+seconds; clockState.observedMs=6000+seconds*1000;
  clockState.referenceTx=mulTimestampMask(MUL_TIMESTAMP_MASK-100*MS+seconds*1000*MS);
  clockState.localRx=mulTimestampMask(MUL_TIMESTAMP_MASK-200*MS+seconds*1000*MS);
  uint64_t tx=mulTimestampMask(clockState.localRx+MUL_FOLLOWER_SLOT_MS*MS);
  mulRelation_t r=mulRelationPublish(&clockState,&delay,tx,clockState.localRx,clockState.observedMs,true);
  mulTestPacket_t p={.source={0x85,0,0,0,0,0,0xcf,0xbc},.sequence=900+seconds,.txTimestamp=tx},decoded;
  mulRelation_t received; uint8_t frame[74]; mulRelationEncode(frame,&p,&r);
  TEST_ASSERT_TRUE(mulBroadcastDecode(frame,sizeof(frame),&decoded,&received));
  TEST_ASSERT_EQUAL_UINT32(900+seconds,decoded.sequence);
  if (r.available) {
    uint64_t converted; TEST_ASSERT_TRUE(mulRelationInverse(&received,tx,0,&converted));
    TEST_ASSERT_INT64_WITHIN(1,MUL_FOLLOWER_SLOT_MS*MS+delay.oneWayDtu,mulTimestampDifference(converted,clockState.referenceTx));
  }
  return received;
}
void test_mulPropagationOriginalAgeSurvivesEachBroadcastAndExpiresWithoutRefresh(void) {
  for (unsigned int i=0;i<8;i++) {
    mulRelation_t r=publish(i);
    unsigned int age=351+MUL_FOLLOWER_SLOT_MS+i*1000;
    TEST_ASSERT_EQUAL_INT(age<MUL_PROP_MAX_AGE_MS,r.available);
    TEST_ASSERT_EQUAL_UINT32(5650,delay.measuredMs);
    if (r.version==2) {
      TEST_ASSERT_EQUAL_UINT16(age,r.delayAgeMs); TEST_ASSERT_EQUAL_UINT16(426,r.delayDtu);
      TEST_ASSERT_EQUAL_INT(age>MUL_PROP_MAX_AGE_MS ? mulPropagationExpired : i==0 ? mulPropagationFresh : mulPropagationReused,r.delayState);
    }
  }
}
void test_mulPropagationResetAndFutureSequenceCannotReuseOldEstimate(void) {
  delay.epoch++; TEST_ASSERT_FALSE(publish(0).available);
  setUp(); delay.referenceSequence=7; TEST_ASSERT_FALSE(publish(0).available);
  setUp(); delay.valid=false; TEST_ASSERT_FALSE(publish(0).available);
}
void test_mulPropagationRefreshChangesMeasurementAgeOnlyAfterNewMeasurement(void) {
  if (MUL_PROP_MAX_AGE_MS==1000) return;
  TEST_ASSERT_TRUE(publish(4).available);
  delay.measuredMs=10650; delay.referenceSequence=10; delay.oneWayDtu=500;
  mulRelation_t r=publish(5);
  TEST_ASSERT_TRUE(r.available); TEST_ASSERT_EQUAL_UINT16(351+MUL_FOLLOWER_SLOT_MS,r.delayAgeMs);
  TEST_ASSERT_EQUAL_UINT16(500,r.delayDtu); TEST_ASSERT_EQUAL_INT(mulPropagationFresh,r.delayState);
}
void test_mulPropagationVersionTwoIsExplicitAndRejectsIncompatibleMetadata(void) {
  if (MUL_PROP_MAX_AGE_MS==1000) return;
  mulRelation_t r=publish(1); uint8_t frame[74];
  mulTestPacket_t p={.source={0x85,0,0,0,0,0,0xcf,0xbc},.txTimestamp=mulTimestampMask(clockState.localRx+MUL_FOLLOWER_SLOT_MS*MS)};
  mulRelationEncode(frame,&p,&r); TEST_ASSERT_EQUAL_UINT8(2,frame[34]);
  mulRelation_t out; mulTestPacket_t decoded;
  frame[34]=1; TEST_ASSERT_FALSE(mulBroadcastDecode(frame,74,&decoded,&out)); frame[34]=2;
  frame[35]|=0x80; TEST_ASSERT_FALSE(mulBroadcastDecode(frame,74,&decoded,&out)); frame[35]&=0x7f;
  frame[35]=(frame[35]&7)|(mulPropagationExpired<<3);
  TEST_ASSERT_FALSE(mulBroadcastDecode(frame,74,&decoded,&out));
}
