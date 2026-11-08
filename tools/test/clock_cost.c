/* Offline Cortex-M0 instruction-cost fixture: real production observation path.
 * No peripheral accesses, UART, RTOS, or physical timing claims. */
#include "mul_clock_fit.h"
#include <string.h>
static mulClock_t clockState;
static mulClockFit_t fit;
static mulClockFitConfig_t config;
#ifndef CLOCK_COST_PERIOD_MS
#define CLOCK_COST_PERIOD_MS 1000
#endif
volatile unsigned clockCostFailure;
__attribute__((noinline)) void clockCostBegin(unsigned kind) {__asm volatile(""::"r"(kind):"memory");}
__attribute__((noinline)) void clockCostEnd(unsigned kind) {__asm volatile(""::"r"(kind):"memory");}
__attribute__((noinline)) void clockCostDone(void) {__asm volatile("":::"memory");}
void _start(void) {
 mulClockReset(&clockState);mulClockFitReset(&fit);mulClockFitDefaults(&config);
 config.estimator=mulClockEstimatorKalman;config.compensate=1;config.periodMs=CLOCK_COST_PERIOD_MS;
 mulNetwork_t network;mulNetworkInit(&network,132,133);network.slotMs=5;
 uint64_t tx=MUL_TIMESTAMP_MASK-63897600000ULL/2,rx=tx-100000;
 uint32_t now=10000,seq=0;
 for(unsigned i=0;i<16;i++) {
  unsigned gap=i==14?3000/CLOCK_COST_PERIOD_MS:1,kind=i==0?0:i==13?2:i==14?3:1;
  uint64_t dt=(uint64_t)gap*CLOCK_COST_PERIOD_MS*63897600;
  tx=mulTimestampMask(tx+dt);
  rx=mulTimestampMask(rx+dt+dt*15000/1000000000);
  now+=gap*CLOCK_COST_PERIOD_MS;seq+=gap;
  mulTestPacket_t p={.sequence=seq,.txTimestamp=tx};memcpy(p.source,mulDefaultNetwork.addresses[0],8);
  mulClockDelay_t delay={.valid=true,.epoch=clockState.epoch,.oneWayDtu=400,
   .measuredMs=now-100,.maxAgeMs=6000,.referenceSequence=seq-1,.units=mulClockDelayReferenceDtu};
  memcpy(delay.reference,mulDefaultNetwork.addresses[0],8);memcpy(delay.follower,mulDefaultNetwork.addresses[1],8);
  clockCostBegin(kind);
#if CLOCK_COST_MOTION_GUARD
  mulClockObserveArrivalOnly(&network,&clockState,&fit,&config,&p,mulTimestampMask(rx+(i==13?3000:0)),now);
  clockCostEnd(kind);
#elif CLOCK_COST_DEFERRED
  mulClockFitPending_t pending;
  mulClockFitBegin(&network,&clockState,&fit,&config,&p,mulTimestampMask(rx+(i==13?3000:0)),now,true,&delay,&pending);
  int32_t rawSkew=clockState.skewPpb,prior;unsigned rawStatus=clockState.status;
  if(pending.active){
    bool available=mulClockFitPrior(&clockState,&fit,&pending,5,&prior);
    clockState.status=available?mulClockValid:mulClockWarmup;
    if(available)clockState.skewPpb=prior;
  }
  clockCostEnd(kind);
  if(pending.active){
    clockCostBegin(4);
    clockState.skewPpb=rawSkew;clockState.status=rawStatus;
    mulClockFitFinish(&network,&clockState,&fit,&config,&p,mulTimestampMask(rx+(i==13?3000:0)),now,&pending);
    clockCostEnd(4);
  }
#else
  mulClockObserveFitted(&network,&clockState,&fit,&config,&p,mulTimestampMask(rx+(i==13?3000:0)),now,true,&delay);
  clockCostEnd(kind);
#endif
#if CLOCK_COST_MOTION_GUARD
  if(fit.applied||fit.ready||fit.accepted)clockCostFailure++;
#else
  if(i>8&&(!fit.applied||fit.resets>2))clockCostFailure++;
#endif
 }
 clockCostDone();for(;;){}
}
