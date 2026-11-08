/* Deterministic production-C guards; no hardware or physical accuracy claim. */
#include "mul_clock_fit.h"
#include "mul_relation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MS INT64_C(63897600)
static mulClockDelay_t sample(const mulClock_t *clock) {
 mulClockDelay_t d={.epoch=clock->epoch,.oneWayDtu=213,.valid=true,
  .maxAgeMs=6000,.referenceSequence=clock->sequence,.measuredMs=clock->observedMs,
  .measurementBoundDtu=UINT64_MAX,.motionBoundDtuPerMs=UINT64_MAX};
 memcpy(d.reference,mulDefaultNetwork.addresses[0],8);memcpy(d.follower,mulDefaultNetwork.addresses[1],8);
 return d;
}
static void delay(void) {
#if MUL_PROP_MOTION_BUDGET_MM < 0
 // Disabled policy must not constrain caller TTL by the deployment default.
 assert(mulPropagationMotionValid(UINT32_MAX));
#endif
 mulClock_t clock;mulClockReset(&clock);
 clock.hasBaseline=true;clock.status=mulClockValid;clock.sequence=100;
 clock.referenceTx=MUL_TIMESTAMP_MASK-10*MS;clock.localRx=5000;clock.observedMs=UINT32_MAX-50;
 mulClockDelay_t d=sample(&clock);
 uint32_t horizon=mulPropagationMotionHorizon(d.maxAgeMs);
 assert(horizon<=6000);
 mulClockDelayStatus_t expected=mulPropagationMotionValid(0)?mulClockDelayValid:mulClockDelayMotion;
 assert(mulClockDelayStatus(&clock,&d,d.measuredMs)==expected);
 if(expected==mulClockDelayValid) {
  assert(mulClockDelayStatus(&clock,&d,d.measuredMs+horizon)==mulClockDelayValid);
  if(horizon<6000)assert(mulClockDelayStatus(&clock,&d,d.measuredMs+horizon+1)==mulClockDelayMotion);
 }
 assert(mulClockDelayStatus(&clock,&d,d.measuredMs+6001)==mulClockDelayStale);
 d.epoch++;assert(mulClockDelayStatus(&clock,&d,d.measuredMs)==mulClockDelayEpochMismatch);d.epoch--;
 d.reference[0]++;assert(mulClockDelayStatus(&clock,&d,d.measuredMs)==mulClockDelayInvalid);d.reference[0]--;
 d.oneWayDtu=-1;assert(mulClockDelayStatus(&clock,&d,d.measuredMs)==mulClockDelayInvalid);d.oneWayDtu=213;
 // A plausible NLOS bias is not observable as motion. Do not manufacture a
 // calibrated measurement bound or overwrite this raw sample.
 d.oneWayDtu=1000;
 assert(mulClockDelayStatus(&clock,&d,d.measuredMs)==expected);
 assert(d.oneWayDtu==1000&&d.measurementBoundDtu==UINT64_MAX);d.oneWayDtu=213;
 d.source=mulDelayExternal;assert(mulClockDelayStatus(&clock,&d,d.measuredMs)==expected);
 d.referenceSequence=clock.sequence+1;assert(mulClockDelayStatus(&clock,&d,d.measuredMs)==mulClockDelayEpochMismatch);d.referenceSequence=clock.sequence;
 mulClockCorrectedMapping_t m;
 bool mapped=mulClockMapRefToFollowerSimultaneous(&clock,mulTimestampMask(clock.referenceTx+20*MS),&d,d.measuredMs,&m);
 assert(mapped==!!mulPropagationMotionValid(20));
 if(mapped){assert(m.totalPhysicalBoundDtu==UINT64_MAX);assert(m.measurementBoundReferenceDtu==UINT64_MAX);}
 if(horizon<6000&&horizon>0) {
  // Origin can be fresh while extrapolation crosses the motion budget.
  assert(!mulClockMapRefToFollowerSimultaneous(&clock,mulTimestampMask(clock.referenceTx+(horizon+1)*MS),&d,d.measuredMs,&m));
 }
 // Radio/clock reset retires the delay, even at identical sequence and uptime.
 mulClockReset(&clock);assert(mulClockDelayStatus(&clock,&d,d.measuredMs)==mulClockDelayEpochMismatch);
#if MUL_RELATIVE_SPEED_MM_S == 1000
 assert(mulPropagationMotionDtu(1000)==214);
#endif
}
static void relation(void) {
 mulClock_t c;mulClockReset(&c);c.hasBaseline=true;c.status=mulClockValid;c.sequence=100;
 c.referenceTx=MUL_TIMESTAMP_MASK-10*MS;c.localRx=20000;c.observedMs=100;
 mulClockDelay_t d=sample(&c);
 mulRelation_t r=mulRelationPublish(&c,&d,mulTimestampMask(c.localRx+5*MS),c.localRx,100,false);
 if(!mulPropagationMotionValid(6))assert(!r.available);
 else {
  assert(r.available);uint64_t tx=mulTimestampMask(c.localRx+5*MS),mapped;
  assert(mulRelationInverse(&r,tx,0,&mapped));
  assert(!mulRelationInverse(&r,tx,mulPropagationMotionHorizon(6000)+1,&mapped));
  uint8_t frame[74];mulTestPacket_t p={.sequence=100,.txTimestamp=tx},decoded;mulRelation_t dr;
  memcpy(p.source,mulDefaultNetwork.addresses[1],8);
  mulRelationEncode(frame,&p,&r);assert(mulBroadcastDecode(frame,74,&decoded,&dr));
  assert(dr.available&&dr.epoch==r.epoch);
  frame[34]=255;assert(!mulBroadcastDecode(frame,74,&decoded,&dr));
 }
 // A valid legacy producer may advertise a relation outside our local motion
 // budget. Decode/raw acquisition must survive while application fails closed.
 r=(mulRelation_t){.version=2,.available=true,.epoch=1,.referenceSequence=100,
   .twrSequence=100,.referenceOrigin=50000,.followerOrigin=60000,.originAgeMs=5,
   .delayAgeMs=500,.maxAgeMs=6000,.delayDtu=213,.delayState=mulPropagationFresh};
 mulTestPacket_t packet={.sequence=100,.txTimestamp=60000+5*MS},decoded;
 memcpy(packet.source,mulDefaultNetwork.addresses[1],8);
 uint8_t bytes[74];mulRelation_t decodedRelation;mulRelationEncode(bytes,&packet,&r);
 assert(mulBroadcastDecode(bytes,74,&decoded,&decodedRelation));
 uint64_t converted;
 assert(mulRelationInverse(&decodedRelation,packet.txTimestamp,0,&converted)==!!mulPropagationMotionValid(500));
}
static void receiver(void) {
#if MUL_RX_MOTION_GUARD
 mulClock_t c;mulClockReset(&c);mulClockFit_t fit;mulClockFitReset(&fit);
 mulClockFitConfig_t config;mulClockFitDefaults(&config);config.periodMs=100;config.estimator=2;
 for(unsigned i=0;i<60;i++) {
  // Oscillator+motion are observationally confounded; wraps in both domains.
  mulTestPacket_t p={.sequence=i,.txTimestamp=mulTimestampMask(MUL_TIMESTAMP_MASK-50*MS+(int64_t)i*100*MS)};
  memcpy(p.source,mulDefaultNetwork.addresses[0],8);
  uint64_t rx=mulTimestampMask(p.txTimestamp+30000+(int64_t)i*64000);
  uint32_t now=UINT32_MAX-2000+i*100;
  mulClockResult_t result=mulClockObserveArrivalOnly(NULL,&c,&fit,&config,&p,rx,now);
  assert(result!=mulClockIgnored&&c.localRx==rx&&c.referenceTx==p.txTimestamp);
  assert(!fit.count&&!fit.ready&&!fit.applied&&fit.uncertaintyPpb==UINT32_MAX);
  assert(fit.fallback==mulClockFitUncertain&&fit.rejected==i+1);
  uint32_t rejected=fit.rejected;
  assert(mulClockObserveArrivalOnly(NULL,&c,&fit,&config,&p,rx,now)==mulClockIgnored&&fit.rejected==rejected);
 }
 assert(c.status==mulClockValid); // Arrival scheduling survives; no KF update.
#endif
}
int main(void){delay();relation();receiver();puts("Motion guards: age/budget boundaries, unknown speed, external/TWR, identities, future/reordered samples, epoch/reset, 40/32-bit wrap, extrapolation and wire roundtrip passed");}
