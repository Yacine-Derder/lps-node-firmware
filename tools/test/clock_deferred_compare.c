/* Host-only exact posterior comparison fixture; no device or persisted data. */
#include "mul_clock_fit.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 assert(argc==5);unsigned period=atoi(argv[1]),compensate=atoi(argv[2]),method=atoi(argv[3]),mode=atoi(argv[4]);
 mulClock_t clock;mulClockReset(&clock);mulClockFit_t fit;mulClockFitReset(&fit);
 mulClockFitConfig_t c;mulClockFitDefaults(&c);c.estimator=method;c.compensate=compensate;c.periodMs=period;
 mulNetwork_t network;assert(mulNetworkInit(&network,132,133));network.slotMs=5;
 uint32_t seq=UINT32_MAX-20,now=UINT32_MAX-2000;
 int64_t tx=MUL_TIMESTAMP_MASK-63897600000LL/2,phase=0;
#if CLOCK_COST_DEFERRED
 double priorSquared=0,posteriorSquared=0,maxPrior=0;unsigned comparisons=0;
#endif
 for(unsigned i=0;i<5000;i++) {
  unsigned gap=i%97==0?6000/period:i%43==0?3000/period:i%23==0?2:1;
  int64_t dt=(int64_t)gap*period*63897600;
  double ppb=mode==1?-35000:15000;
  if(mode==2)ppb+=sin(i/73.)*80+.03*i;
  if(mode==3&&i>2500)ppb+=150;
  phase+=(int64_t)llround(dt*ppb/1e9);tx+=dt;seq+=gap;now+=gap*period;
  mulTestPacket_t p={.sequence=seq,.txTimestamp=mulTimestampMask(tx)};
  memcpy(p.source,network.addresses[0],8);
  uint64_t rx=mulTimestampMask(tx+100000+phase+(i%79==0?100000:i%31==0?3000:0));
  mulClockDelay_t delay={.valid=true,.epoch=clock.epoch,.oneWayDtu=400+(i%17),
   .measuredMs=now-100,.maxAgeMs=MUL_PROP_MAX_AGE_MS,.referenceSequence=seq-1,.units=mulClockDelayReferenceDtu};
  memcpy(delay.reference,network.addresses[0],8);memcpy(delay.follower,network.addresses[1],8);
  if(mode==4&&i%211==0)fit.covariance[(i/211)%3][(i/633)%3]=NAN;
  if(mode==5&&i%211==0)fit.state[(i/211)%3]=INFINITY;
  const mulClockDelay_t *selected=i%101==0?NULL:&delay;
#if CLOCK_COST_DEFERRED
  mulClockFitPending_t pending;
  mulClockFitBegin(&network,&clock,&fit,&c,&p,rx,now,true,selected,&pending);
  int32_t rawSkew=clock.skewPpb,prior;unsigned rawStatus=clock.status;
  if(pending.active) {
   assert(clock.sequence==p.sequence&&clock.referenceTx==p.txTimestamp&&clock.localRx==rx);
   mulClockFit_t before=fit;
   bool valid=mulClockFitPrior(&clock,&fit,&pending,5,&prior);
   assert(!memcmp(&fit,&before,sizeof(fit))); // Forecast cannot consume the queued sample.
   if(valid) {
    double x[3],cov[3][3],h=5/2000.;
    assert(mulClockFitPredict(&fit,(double)pending.dt/63897600000.,x,cov));
    assert(cov[1][1]+2*h*cov[1][2]+h*h*cov[2][2]<=1000000.000001);
    assert(fabs(prior-(x[1]+h*x[2]))<=.500001);
    clock.skewPpb=prior;clock.status=mulClockValid;
   }else clock.status=mulClockWarmup;
   clock.skewPpb=rawSkew;clock.status=rawStatus;
   mulClockFitFinish(&network,&clock,&fit,&c,&p,rx,now,&pending);
   if(valid&&fit.applied&&i>100&&mode<4) {
    double e=prior-ppb,post=clock.skewPpb-ppb;
    priorSquared+=e*e;posteriorSquared+=post*post;
    if(fabs(e)>maxPrior)maxPrior=fabs(e);
    comparisons++;
   }
   before=fit;mulClock_t oldClock=clock;
   mulClockFitFinish(&network,&clock,&fit,&c,&p,rx,now,&pending);
   assert(!memcmp(&fit,&before,sizeof(fit))&&!memcmp(&clock,&oldClock,sizeof(clock))); // Exactly once.
  }
#else
  mulClockObserveFitted(&network,&clock,&fit,&c,&p,rx,now,true,selected);
#endif
  assert(fwrite(&clock,sizeof(clock),1,stdout)==1);assert(fwrite(&fit,sizeof(fit),1,stdout)==1);
 }
#if CLOCK_COST_DEFERRED
 if(comparisons)fprintf(stderr,"{\"period\":%u,\"compensate\":%u,\"mode\":%u,\"samples\":%u,\"prior_rmse_ppb\":%.9g,\"posterior_rmse_ppb\":%.9g,\"prior_max_error_ppb\":%.9g}\n",
   period,compensate,mode,comparisons,sqrt(priorSquared/comparisons),sqrt(posteriorSquared/comparisons),maxPrior);
#endif
 return 0;
}
