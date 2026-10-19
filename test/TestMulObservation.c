/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_observation.h"
#include "mul_common.h"
#include <string.h>

static mulObservation_t observations[2];
static const uint8_t receiver[8]={0x86,0,0,0,0,0,0xcf,0xbc};
static const uint8_t identities[2][8]={{0x84,0,0,0,0,0,0xcf,0xbc},{0x85,0,0,0,0,0,0xcf,0xbc}};
void setUp(void) { mulObservationInit(observations,receiver); }
void tearDown(void) {}
static mulSequenceResult_t record(int role,uint32_t seq,uint64_t raw,int delta,uint32_t now) {
  mulTestPacket_t packet={.sequence=seq,.txTimestamp=mulTimestampMask(UINT64_C(0x1234567800)+seq)};
  memcpy(packet.source,identities[role],8);
  mulPowerRx_t rx={.raw=raw,.corrected=mulTimestampMask(raw+delta),
    .power={.powerCentiDbm=-8000,.deltaDtu=delta,.status=mulPowerApplied,.table=2}};
  return mulObservationRecord(observations,&packet,&rx,now);
}

void test_mulObservationRetainsIdentityDomainsRawCorrectedAndUnknownRemoteEpoch(void) {
  TEST_ASSERT_EQUAL_INT(mulSequenceFirst,record(0,50,0,-5,100));
  mulObservation_t *o=&observations[0];
  TEST_ASSERT_TRUE(o->valid); TEST_ASSERT_FALSE(o->remoteEpochKnown);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(identities[0],o->sourceClock,8);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(receiver,o->receiverClock,8);
  TEST_ASSERT_EQUAL_UINT64(0,o->source.rxTimestamp);
  TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK-4,o->correctedRx);
  TEST_ASSERT_EQUAL_INT(-5,o->power.deltaDtu); TEST_ASSERT_EQUAL_UINT32(1,o->generation);
  TEST_ASSERT_EQUAL_INT(mulSequenceFirst,record(1,900,200,7,200));
  TEST_ASSERT_EQUAL_UINT32(50,observations[0].source.sequence);
  TEST_ASSERT_EQUAL_UINT32(900,observations[1].source.sequence);
}

void test_mulObservationDuplicatesOldAndGapsPreserveAcceptedMarkersAndAge(void) {
  record(0,UINT32_MAX,100,5,10);
  TEST_ASSERT_EQUAL_INT(mulSequenceForward,record(0,0,200,6,20));
  TEST_ASSERT_EQUAL_INT(mulSequenceGap,record(0,2,300,7,30));
  TEST_ASSERT_EQUAL_INT(mulSequenceDuplicate,record(0,2,400,8,40));
  TEST_ASSERT_EQUAL_INT(mulSequenceOld,record(0,1,500,9,50));
  TEST_ASSERT_EQUAL_UINT64(300,observations[0].source.rxTimestamp);
  TEST_ASSERT_EQUAL_UINT64(307,observations[0].correctedRx);
  TEST_ASSERT_EQUAL_UINT32(30,observations[0].acceptedMs);
  TEST_ASSERT_EQUAL_UINT32(1,observations[0].source.gaps);
  TEST_ASSERT_EQUAL_UINT32(1,observations[0].source.duplicates);
  TEST_ASSERT_EQUAL_UINT32(1,observations[0].source.outOfOrder);
}

void test_mulObservationExpiresAcrossUptimeWrapAndRebasesOnlyChangedStaleSource(void) {
  record(0,20,100,0,UINT32_MAX-1000);
  record(1,90,200,0,500);
  mulObservationExpire(observations,2000);
  TEST_ASSERT_FALSE(observations[0].valid); TEST_ASSERT_TRUE(observations[1].valid);
  TEST_ASSERT_EQUAL_INT(mulSequenceDuplicate,record(0,20,300,0,2000));
  TEST_ASSERT_FALSE(observations[0].valid);
  TEST_ASSERT_EQUAL_INT(mulSequenceFirst,record(0,0,400,0,2000));
  TEST_ASSERT_EQUAL_UINT32(2,observations[0].generation);
  TEST_ASSERT_EQUAL_UINT32(1,observations[1].generation);
  TEST_ASSERT_TRUE(observations[0].valid); TEST_ASSERT_FALSE(observations[0].remoteEpochKnown);
}

void test_mulObservationRejectsUnsupportedIdentityAndRepresentsMissingPower(void) {
  mulTestPacket_t packet={.source={0x84,1,0,0,0,0,0xcf,0xbc}};
  mulPowerRx_t rx={.raw=100,.corrected=100};
  mulObservationRecord(observations,&packet,&rx,1);
  TEST_ASSERT_FALSE(observations[0].valid); TEST_ASSERT_FALSE(observations[1].valid);
  memcpy(packet.source,identities[0],8);
  mulObservationRecord(observations,&packet,&rx,1);
  TEST_ASSERT_EQUAL_INT(mulPowerMissing,observations[0].power.status);
  TEST_ASSERT_EQUAL_UINT64(100,observations[0].source.rxTimestamp);
  TEST_ASSERT_EQUAL_UINT64(100,observations[0].correctedRx);
}
