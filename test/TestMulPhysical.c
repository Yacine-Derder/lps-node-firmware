/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_downlink.h"
#include "mul_relation.h"
#include "mul_common.h"
#include "mul_clock.h"
#include "mul_observation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Independent oracle: SI seconds, metres and three physical oscillators. No
// production mapping/propagation helper constructs timestamps or expected TDoA.
static const long double frequency=63897600000.0L, light=299792458.0L;
static const long double refRate=1.000008L;
static mulPair_t pair;
static mulRelation_t relation;
static mulClock_t rate;
static long double separationSeconds;
static uint32_t now;
void setUp(void) {}
void tearDown(void) {}
static uint64_t clockStamp(long double seconds,long double oscillator,uint64_t origin)
{
  // Positive times only; cast then mask independently of production arithmetic.
  return (origin+(uint64_t)llroundl(seconds*oscillator*frequency)) & UINT64_C(0xffffffffff);
}
static void physical(long double refDistance,long double folDistance,bool wrap)
{
  memset(&pair,0,sizeof(pair));
  const uint8_t ids[3][8]={{0x84,0,0,0,0,0,0xcf,0xbc},
    {0x85,0,0,0,0,0,0xcf,0xbc},{0x86,0,0,0,0,0,0xcf,0xbc}};
  long double folRate=refRate*(1.0L-12000.0L/1e9L);
  long double rlrRate=refRate*(1.0L+20000.0L/1e9L);
  uint64_t origins[3]={UINT64_C(123456789),UINT64_C(823456789),UINT64_C(523456789)};
  if (wrap) for (unsigned int i=0;i<3;i++) origins[i]=UINT64_C(0xffffffffff)-100000-i*1000;
  // ALRs are 2 m apart. Follower waits the configured number of its own clock
  // ticks after physical reference arrival; this is not a reference-time slot.
  separationSeconds=2.0L/light + (MUL_FOLLOWER_SLOT_MS/1000.0L)/folRate;
  pair.reference.source.txTimestamp=clockStamp(0,refRate,origins[0]);
  pair.follower.source.txTimestamp=clockStamp(separationSeconds,folRate,origins[1]);
  pair.reference.source.rxTimestamp=clockStamp(refDistance/light,rlrRate,origins[2]);
  pair.follower.source.rxTimestamp=clockStamp(separationSeconds+folDistance/light,rlrRate,origins[2]);
  pair.reference.correctedRx=pair.reference.source.rxTimestamp;
  pair.follower.correctedRx=pair.follower.source.rxTimestamp;
  pair.reference.valid=pair.follower.valid=true;
  pair.reference.acceptedMs=1000; now=1000+MUL_FOLLOWER_SLOT_MS+1;
  pair.follower.acceptedMs=now;
  pair.reference.source.sequence=6; pair.follower.source.sequence=987;
  memcpy(pair.reference.sourceClock,ids[0],8); memcpy(pair.follower.sourceClock,ids[1],8);
  memcpy(pair.reference.receiverClock,ids[2],8); memcpy(pair.follower.receiverClock,ids[2],8);
  pair.rawArrivalDifferenceDtu=mulTimestampDifference(pair.follower.source.rxTimestamp,pair.reference.source.rxTimestamp);
  pair.correctedArrivalDifferenceDtu=pair.rawArrivalDifferenceDtu;
  pair.correctedAvailable=true;
  pair.reference.power.status=pair.follower.power.status=mulPowerApplied;
  relation=(mulRelation_t){.available=true,.epoch=42,.referenceSequence=6,.twrSequence=5,
    .referenceOrigin=origins[0],.followerOrigin=origins[1],.skewPpb=-12000,
    .originAgeMs=MUL_FOLLOWER_SLOT_MS+1,.delayAgeMs=351+MUL_FOLLOWER_SLOT_MS,.remainingMs=100};
  rate=(mulClock_t){.hasBaseline=true,.status=mulClockValid,.sequence=6,
    .referenceTx=pair.reference.source.txTimestamp,.localRx=pair.reference.source.rxTimestamp,
    .observedMs=1000,.skewPpb=20000};
}
static mulDownlink_t compute(void)
{
  mulDownlink_t d=mulDownlinkCompute(&relation,&pair,&rate,now,0,true);
  TEST_ASSERT_EQUAL_INT(mulDownlinkAvailable,d.status);
  TEST_ASSERT_EQUAL_UINT64(UINT64_MAX,d.physicalBoundDtu);
  return d;
}
void test_mulPhysicalAnalyticalGeometryWithThreeIndependentRatesAndWrap(void)
{
  const long double distances[3][2]={{1,1},{0.5L,1.5L},{1.5L,0.5L}};
  for (unsigned int i=0;i<3;i++) for (unsigned int wrap=0;wrap<2;wrap++) {
    physical(distances[i][0],distances[i][1],wrap!=0);
    int64_t expected=llroundl((distances[i][1]-distances[i][0])/light*refRate*frequency);
    mulDownlink_t d=compute();
    TEST_ASSERT_INT64_WITHIN(2,expected,d.correctedTdoaRef);
    TEST_ASSERT_INT32_WITHIN(10,(int32_t)llroundl((distances[i][1]-distances[i][0])*refRate*1000),d.rangeDifferenceMm);
  }
}
void test_mulPhysicalInjectedRateErrorHasAnalyticalSlotProportionalBias(void)
{
  for (int error=-10;error<=10;error+=20) { // +/-10 ppb = +/-0.010 ppm.
    physical(1,1,true);
    int64_t ideal=compute().correctedTdoaRef;
    rate.skewPpb+=error;
    mulDownlink_t biased=compute();
    // Derive from physical oscillator ratio, independently in floating point.
    long double ratio=1.0L+20000.0L/1e9L;
    long double expectedBias=separationSeconds*refRate*frequency*(ratio/(ratio+error/1e9L)-1.0L);
    TEST_ASSERT_INT64_WITHIN(2,llroundl(expectedBias),biased.correctedTdoaRef-ideal);
    // Per-slot bias per second is invariant (allow quantization at 10 ms).
    long double metres=biased.rangeDifferenceMm/1000.0L;
    long double predicted=-light*(MUL_FOLLOWER_SLOT_MS/1000.0L)*error/1e9L;
    TEST_ASSERT_TRUE(fabsl(metres-predicted)<0.015L);
  }
}
void test_mulPhysicalWrongDomainAndArrivalAlignedOriginAreDetectable(void)
{
  physical(1,1,true);
  rate.skewPpb=0; // Incorrectly treating receiver DTU as reference DTU.
  mulDownlink_t wrong=mulDownlinkCompute(&relation,&pair,&rate,now,0,true);
  TEST_ASSERT_TRUE(wrong.status==mulDownlinkInconsistent || llabs(wrong.correctedTdoaRef)>10000);
  physical(1,1,true);
  // Omitting propagation removal yields a fixed +2 m geometric error, at
  // every slot. Correct simultaneous origin above produces zero instead.
  relation.followerOrigin=clockStamp(2.0L/light,refRate*(1.0L-12000.0L/1e9L),relation.followerOrigin);
  TEST_ASSERT_INT32_WITHIN(10,2000,compute().rangeDifferenceMm);
}
void test_mulPhysicalPowerCorrectionIsAppliedOnceAndRawIsRetained(void)
{
  physical(0.5L,1.5L,true);
  int64_t expected=compute().correctedTdoaRef;
  // Raw follower reception has a known +12 local-DTU bias, correction froze
  // the ideal stamp. The geometry must not receive that correction twice.
  pair.follower.source.rxTimestamp=(pair.follower.source.rxTimestamp+12)&UINT64_C(0xffffffffff);
  pair.rawArrivalDifferenceDtu+=12;
  mulDownlink_t d=compute();
  TEST_ASSERT_EQUAL_INT64(expected,d.correctedTdoaRef);
  TEST_ASSERT_INT64_WITHIN(1,12,d.rawTdoaRef-d.correctedTdoaRef);
}
