#include "mul_plan_protocol.h"
#include <string.h>
volatile unsigned failure;
__attribute__((noinline,noclone)) void costBegin(unsigned n){__asm volatile(""::"r"(n):"memory");}
__attribute__((noinline,noclone)) void costEnd(void){__asm volatile("":::"memory");}
__attribute__((noinline,noclone)) void costDone(void){__asm volatile("":::"memory");}
void _start(void){
 for(unsigned n=3;n<=4;n++)for(unsigned ff=0;ff<=1;ff++){
  mulRangingPlan_t p={0};mulScheduleLegacy(&p.broadcast,132,134,5);mulScheduleExperimental(&p.broadcast);
  p.broadcast.count=n;p.broadcast.ids[2]=133;if(n==4)p.broadcast.ids[3]=135;
  p.broadcast.periodMs=100;p.broadcast.txLeadUs=1000;p.broadcast.rxBudgetUs=2000;p.broadcast.rangingFlags=0;
  p.rlrCount=1;p.rlrs[0]=141;p.replyMs=5;mulRangingPlanTdma(&p);p.twrWindowMs=30;if(ff)p.enabled|=MUL_PLAN_FF;
  unsigned key=(n-3)*2+ff;mulTwrPacket_t packet={.type=mulTwrPoll,.id=0,.txTimestamp=123,.stamps={123}},out;uint8_t frame[90];
  static const uint32_t expected[]={0x2aed8a85,0x8f209da3,0x617ae520,0xec2b4b2a};
  costBegin(key*10);if(mulRangingPlanFingerprint(&p)!=expected[key])failure++;costEnd();
  costBegin(key*10+1);if(!mulPlanTwrEncode(frame,&p,0,&packet))failure++;costEnd();
  costBegin(key*10+2);if(!mulPlanTwrDecode(&p,0,0,frame,90,&out))failure++;costEnd();
#ifdef MUL_PLAN_TWR_BINDING
  mulPlanTwrBinding_t binding;uint8_t fast[90];
  costBegin(key*10+3);if(!mulPlanTwrBind(&binding,&p,0,0))failure++;costEnd();
  costBegin(key*10+4);if(!mulPlanTwrEncodeBound(fast,&binding,&packet))failure++;costEnd();
  costBegin(key*10+5);if(!mulPlanTwrDecodeBound(&binding,fast,90,&out))failure++;costEnd();
  if(memcmp(frame,fast,90))failure++;
#endif
 }
 costDone();for(;;){}
}
