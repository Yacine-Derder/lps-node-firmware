/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_ads.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint64_t wrap(uint64_t v) { return mulTimestampMask(v); }
static void times(uint64_t t[6],double rate,uint64_t a,uint64_t b) {
  const double p=20000,db=1000000000,da=2000000000;
  t[0]=a; t[1]=wrap(b+(uint64_t)llround(rate*p));
  t[2]=wrap(b+(uint64_t)llround(rate*(p+db)));
  t[3]=wrap(a+(uint64_t)(db+2*p)); t[4]=wrap(a+(uint64_t)(db+2*p+da));
  t[5]=wrap(b+(uint64_t)llround(rate*(3*p+db+da)));
}
static mulAdsPacket_t wire(mulAds_t *s,unsigned now) {
  mulAdsPacket_t p,d; uint8_t f[128];
  assert(mulAdsTakeTx(s,now,&p)); assert(!mulAdsTakeTx(s,now,&d));
  assert(mulAdsEncode(f,sizeof(f),&p)); assert(mulAdsDecode(f,56,&d));
  assert(d.session==p.session && d.transaction==p.transaction && d.type==p.type && d.source==p.source && d.destination==p.destination);
  for(unsigned i=0;i<3;i++) assert(d.stamps[i]==p.stamps[i]);
  for(unsigned n=0;n<128;n++) if(n!=56) assert(!mulAdsDecode(f,n,&d));
  const unsigned strict[]={0,1,2,3,4,6,12,14,20,21,24,25,53,54,55};
  for(unsigned i=0;i<sizeof(strict)/sizeof(strict[0]);i++) {
    f[strict[i]]^=0x80; d.transaction=42; assert(!mulAdsDecode(f,56,&d)); assert(d.transaction==42); f[strict[i]]^=0x80;
  }
  assert(mulAdsDecode(f,56,&d)); return d;
}
static void exchange(bool reverse,double rate,uint64_t a,uint64_t b) {
  mulAds_t i,r; uint64_t t[6]; times(t,rate,a,b);
  assert(mulAdsInit(&i,reverse?0x86:0x84,reverse?0x84:0x86,true,123));
  assert(mulAdsInit(&r,i.peer,i.local,false,1));
  assert(mulAdsBegin(&i,0)); assert(!mulAdsBegin(&i,0));
  mulAdsPacket_t p=wire(&i,1),bad=p; bad.destination=0x85;
  assert(!mulAdsReceive(&r,&bad,t[1],1));
  assert(mulAdsTxDone(&i,t[0],1)); assert(!mulAdsTxDone(&i,t[0],1));
  assert(mulAdsReceive(&r,&p,t[1],2)); assert(!mulAdsReceive(&r,&p,t[1],2));
  mulAdsPacket_t response=wire(&r,3); assert(mulAdsTxDone(&r,t[2],3));
  bad=response; bad.session++; assert(!mulAdsReceive(&i,&bad,t[3],3));
  bad=response; bad.transaction++; assert(!mulAdsReceive(&i,&bad,t[3],3));
  assert(mulAdsReceive(&i,&response,t[3],4)); assert(!mulAdsReceive(&i,&response,t[3],4));
  mulAdsPacket_t final=wire(&i,5); assert(mulAdsTxDone(&i,t[4],5));
  assert(mulAdsReceive(&r,&final,t[5],6));
  mulAdsPacket_t report=wire(&r,7); assert(mulAdsTxDone(&r,wrap(t[5]+12345),7));
  assert(mulAdsReceive(&i,&report,wrap(t[4]+123456),8));
  assert(fabs(i.tof-20000*2*rate/(1+rate))<0.00001);
  assert(fabs(i.rateRatio-rate)<1e-12 && i.completed==1 && r.completed==1);
  assert(!mulAdsReceive(&i,&report,0,9)); assert(!mulAdsReceive(&r,&p,0,9));
  p.session++; assert(mulAdsReceive(&r,&p,0,10)); /* Explicit new sender session. */
  assert(mulAdsTick(&r,110)); assert(r.aborted==1 && r.stage==adsIdle);
  assert(mulAdsBegin(&i,UINT32_MAX-50)); assert(!mulAdsTick(&i,48)); assert(mulAdsTick(&i,49));
  assert(!mulAdsReceive(&i,&response,0,50));
  assert(mulAdsBegin(&i,51)); mulAdsPacket_t ignored=wire(&i,51); (void)ignored;
  mulAdsAbort(&i); assert(i.stage==adsIdle && !i.pending); /* TX scheduling/start failure. */
  assert(mulAdsBegin(&i,52)); assert(i.attempts==4 && i.aborted==2);
}
int main(void) {
  assert(sizeof(mulAds_t)<=128);
  for(unsigned reverse=0;reverse<2;reverse++) for(int ppm=-500;ppm<=500;ppm+=50)
    exchange(reverse,1.0+ppm/1e6,MUL_TIMESTAMP_MASK-100000000, UINT64_C(0x8765432100));
  double tof=42,ratio=42; uint64_t t[6]; times(t,1,123,456);
  assert(mulAdsCalculate(t,&tof,&ratio) && fabs(tof-20000)<1e-9);
  t[4]=t[3]; assert(!mulAdsCalculate(t,&tof,&ratio)); assert(tof==20000);
  times(t,1,123,456); t[3]=wrap(t[0]-1); assert(!mulAdsCalculate(t,&tof,&ratio));
  times(t,1.002,123,456); assert(!mulAdsCalculate(t,&tof,&ratio));
  times(t,1,123,456); t[0]=UINT64_C(1)<<40; assert(!mulAdsCalculate(t,&tof,&ratio));
  /* Products exceed uint64 but binary64 calculation remains fractional-DTU accurate. */
  t[0]=0;t[1]=100;t[2]=6000000100ULL;t[3]=6000001000ULL;t[4]=12000001000ULL;t[5]=12000001100ULL;
  assert(mulAdsCalculate(t,&tof,&ratio) && fabs(tof-500)<1e-5);
  assert(!mulAdsCalculate(NULL,&tof,&ratio));
  uint8_t frame[56]; mulAdsPacket_t p={.source=0x84,.destination=0x86,.session=1,.transaction=1,.type=mulAdsPoll};
  assert(!mulAdsEncode(frame,55,&p)); p.stamps[0]=1; assert(!mulAdsEncode(frame,56,&p));
  p.type=mulAdsReport; p.stamps[0]=MUL_TIMESTAMP_MASK; assert(mulAdsEncode(frame,56,&p));
  puts("ADS codec/state/math: both roles, six measured timestamps, wrap/skew/overflow/precision, stale/duplicate/mismatch, restart/session, timeout/retry/abort passed");
}
