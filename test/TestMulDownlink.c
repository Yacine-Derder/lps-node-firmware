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
