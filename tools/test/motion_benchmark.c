/* Matched synthetic measurements through production C. Truth is evaluator-only;
 * exact independent delay is an explicitly ideal INPUT CONTROL, not hardware. */
#include "mul_clock_fit.h"
#include "mul_downlink.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define HZ 63897600000.0
static void position(unsigned scenario,unsigned node,double t,double *px,double *py) {
 double x=node==4?0:20+(node%2)*4,y=node==4?0:(node/2)*4;
 switch(scenario) {
 case 5:if(!node)x-=.2*t;break;
 case 6:if(!node)x+=.2*t;break;
 case 7:if(!node){x=20*cos(.03*t);y=20*sin(.03*t);}break;
 case 8:if(node==0)x+=.2*t;if(node==1)y-=.15*t;break;
 case 9:if(node<4){x+=sin(.05*t+node);y+=cos(.07*t+node);}break;
 case 10:if(node<4)x+=.2*t;break;
 case 11:if(node<4){double a=x;x=a*cos(.02*t)-y*sin(.02*t);y=a*sin(.02*t)+y*cos(.02*t);}break;
 case 12:if(node==0&&t>=20)x+=3;break;
 case 13:if(node<4)y=.0001*node;break;
 case 14:if(node<4&&t>=20&&t<25){x=20;y=0;}break;
 case 15:if(node==4)x+=.2*t;break;
 case 16:if(node<4)x+=.2*t;else y+=.15*t;break;
 case 17:if(node==4)x-=.3*t;break;
 case 18:if(node==4)x-=.002*t*t;break;
 default:break;
 }
 *px=x;*py=y;
}
static double path(unsigned scenario,unsigned a,unsigned b,double t) {
 double ax,ay,bx,by;position(scenario,a,t,&ax,&ay);position(scenario,b,t,&bx,&by);
 return hypot(ax-bx,ay-by);
}
static double distance(unsigned scenario,unsigned node,double t) {return path(scenario,node,4,t);}
static uint64_t stamp(double seconds,double skew,uint64_t origin) {
 return mulTimestampMask(origin+(int64_t)llround(seconds*HZ*(1+skew/1e9)));
}
int main(void) {
 puts("scenario,step,follower,slot_ms,legacy_ppb,independent_ppb,guard_available,legacy_error_mm,independent_error_mm,slot_model_error_mm,delay_hold_error_mm,motion_budget_accept");
 for(unsigned scenario=1;scenario<=18;scenario++) {
  mulNetwork_t net;assert(mulNetworkInit(&net,132,141));net.slotMs=5;
  mulClock_t old,ind,arrival;mulClockReset(&old);mulClockReset(&ind);mulClockReset(&arrival);
  mulClockFit_t fo,fi,fa;mulClockFitReset(&fo);mulClockFitReset(&fi);mulClockFitReset(&fa);
  mulClockFitConfig_t c;mulClockFitDefaults(&c);c.periodMs=100;c.estimator=2;
  mulClockFitConfig_t ci=c;ci.compensate=1;
  const double oscillator=scenario==1?0:10000;
  double held[4];for(unsigned k=1;k<4;k++)held[k]=path(scenario,0,k,0);uint32_t heldAt=0;
  for(unsigned i=0;i<600;i++) {
   if(scenario==4&&i%7==6)continue;
   double t=i*.1,tau=distance(scenario,0,t)/299792458.;
   uint32_t now=UINT32_MAX-30000+i*100;
   mulTestPacket_t p={.sequence=i,.txTimestamp=stamp(t,0,MUL_TIMESTAMP_MASK-100000)};
   memcpy(p.source,net.addresses[0],8);
   int noise=scenario==3?(int)((i*29)%65)-32:0;
   uint64_t rx=mulTimestampMask(stamp(t+tau,oscillator,30000)+noise);
   mulClockDelay_t d={.source=mulDelayExternal,.epoch=ind.epoch,.oneWayDtu=llround(tau*HZ),
    .measuredMs=now,.referenceSequence=i,.maxAgeMs=6000,.valid=true,
    .measurementBoundDtu=UINT64_MAX,.motionBoundDtuPerMs=UINT64_MAX};
   memcpy(d.reference,net.addresses[0],8);memcpy(d.follower,net.addresses[1],8);
   mulClockObserveFitted(&net,&old,&fo,&c,&p,rx,now,true,NULL);
   mulClockObserveFitted(&net,&ind,&fi,&ci,&p,rx,now,true,&d);
   mulClockObserveArrivalOnly(&net,&arrival,&fa,&c,&p,rx,now);
   assert(!fa.count&&!fa.ready&&!fa.applied&&fa.uncertaintyPpb==UINT32_MAX);
   assert(arrival.referenceTx==p.txTimestamp&&arrival.localRx==rx);
   // Independent radio epoch/plan transitions tested separately, no truth injection.
   if(i%60==0){for(unsigned k=1;k<4;k++)held[k]=path(scenario,0,k,t);heldAt=i;}
   if(i<50||old.status!=mulClockValid||ind.status!=mulClockValid)continue;
   const unsigned slots[]={5,10,50,500};
   for(unsigned follower=1;follower<4;follower++)for(unsigned j=0;j<4;j++) {
    mulNetwork_t pairNet;assert(mulNetworkInit(&pairNet,132,132+follower));
    double followerPpb=35000-20000.0*follower;uint64_t followerOrigin=90000+1000*follower;
    unsigned slot=slots[j];double dt=slot/1000.;
    double df=distance(scenario,follower,t+dt),dr=distance(scenario,0,t);
    // Native event inputs include moving-receiver path. For these speeds, the
    // first-order reception-position error is <0.0001mm; no solver is tested.
    uint64_t frx=stamp(t+dt+df/299792458.,oscillator,30000);
    mulPair_t pair={.rawArrivalDifferenceDtu=mulTimestampDifference(frx,rx)};
    mulObservation_t *r=&pair.reference,*f=&pair.follower;
    memcpy(r->sourceClock,mulDefaultNetwork.addresses[0],8);memcpy(f->sourceClock,pairNet.addresses[1],8);
    memcpy(r->receiverClock,net.addresses[1],8);memcpy(f->receiverClock,net.addresses[1],8);
    r->valid=f->valid=true;r->generation=f->generation=1;r->acceptedMs=now;f->acceptedMs=now+slot;
    r->source.sequence=f->source.sequence=i;r->source.txTimestamp=p.txTimestamp;r->source.rxTimestamp=rx;
    // Exact follower clock relation is an input control isolating RX-rate error.
    f->source.txTimestamp=stamp(t+dt,followerPpb,followerOrigin);f->source.rxTimestamp=frx;
    mulRelation_t rel={.version=2,.available=true,.epoch=1,.referenceSequence=i,.twrSequence=i,
     .referenceOrigin=p.txTimestamp,.followerOrigin=stamp(t,followerPpb,followerOrigin),.skewPpb=(int32_t)followerPpb,
     .originAgeMs=slot,.delayAgeMs=slot,.remainingMs=1000,.maxAgeMs=6000,.delayState=mulPropagationFresh};
    mulDownlink_t a=mulDownlinkComputeAtSlot(&pairNet,slot,&rel,&pair,&old,now+slot,1,true);
    mulDownlink_t b=mulDownlinkComputeAtSlot(&pairNet,slot,&rel,&pair,&ind,now+slot,1,true);
    mulDownlink_t g=mulDownlinkComputeAtSlot(&pairNet,slot,&rel,&pair,&arrival,now+slot,1,false);
    assert(g.status==mulDownlinkNoRate);
    assert(a.status==mulDownlinkAvailable&&b.status==mulDownlinkAvailable);
    double actual=(df-dr)*1000;
    double concurrent=(distance(scenario,follower,t)-dr)*1000;
    double heldError=(path(scenario,0,follower,t)-held[follower])*1000;
    unsigned age=(i-heldAt)*100;
    // Same deployment bound/budget used in bounded firmware tests.
    int accepted=(uint64_t)1000*age<=UINT64_C(100)*1000;
    printf("%u,%u,%u,%u,%.0f,%.0f,0,%.6f,%.6f,%.6f,%.6f,%d\n",scenario,i,follower,slot,
     old.skewPpb-oscillator,ind.skewPpb-oscillator,a.rangeDifferenceMm-actual,
     b.rangeDifferenceMm-actual,actual-concurrent,heldError,accepted);
   }
  }
 }
}
