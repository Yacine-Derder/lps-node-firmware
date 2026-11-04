#include "mul_ranging_plan.h"
#include "mul_plan_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static mulRangingPlan_t plan(void) {
 mulRangingPlan_t p={0};mulScheduleLegacy(&p.broadcast,132,134,5);mulScheduleExperimental(&p.broadcast);
 p.broadcast.count=3;p.broadcast.ids[2]=133;p.broadcast.periodMs=200;p.broadcast.txLeadUs=1000;
 p.broadcast.rxBudgetUs=2000;p.broadcast.rangingFlags=0;p.rlrCount=1;p.rlrs[0]=141;p.replyMs=20;
 mulRangingPlanTdma(&p);return p;
}
static void follower_pairs(void) {
 mulRangingPlan_t p=plan();p.broadcast.periodMs=100;p.replyMs=5;p.twrWindowMs=30;
 uint8_t old[64],wire[64];assert(mulRangingPlanEncode(old,&p)&&old[0]==5);
 uint32_t oldHash=mulRangingPlanFingerprint(&p);
 p.broadcast.count=4;p.broadcast.ids[3]=135;p.enabled|=4u;
 mulRangingBudget_t b;assert(!mulRangingPlanValidate(&p,&b));
 assert(b.dataEndUs==29100&&b.reservationUs==30000&&b.guardUs==40900);
 assert(mulRangingPlanEncode(wire,&p)&&wire[0]==6&&wire[1]==7);
 uint8_t damaged[64];memcpy(damaged,wire,64);damaged[0]=5;
 uint32_t crc=mulScheduleCrc(damaged,60);for(unsigned k=0;k<4;k++)damaged[60+k]=crc>>(8*k);
 mulRangingPlan_t rejected;assert(!mulRangingPlanDecode(damaged,&rejected));
 memcpy(damaged,wire,64);damaged[1]&=~4u;crc=mulScheduleCrc(damaged,60);
 for(unsigned k=0;k<4;k++)damaged[60+k]=crc>>(8*k);
 assert(!mulRangingPlanDecode(damaged,&rejected));
 mulRangingPlan_t q;assert(mulRangingPlanDecode(wire,&q));
 const uint8_t i[]={132,132,132,133,133,134,132,133,134,135};
 const uint8_t j[]={133,134,135,134,135,135,141,141,141,141};
 for(unsigned c=0;c<1000;c++) {
  mulRangingOperation_t op,other;assert(mulRangingPlanCount(&p,c)==1);
  assert(mulRangingPlanOperation(&p,c,0,&op)&&mulRangingPlanOperation(&q,c,0,&other));
  assert(op.initiator==i[c%10]&&op.responder==j[c%10]);
  assert(op.kind==(c%10<3?MUL_PLAN_AA:c%10<6?4u:MUL_PLAN_AR));
  assert(op.startUs==29100&&op.pollUs==33100&&op.endUs==59100);
  assert(!memcmp(&op,&other,sizeof(op)));
 }
 p.enabled&=~4u;p.broadcast.count=3;p.broadcast.ids[3]=0;
 assert(mulRangingPlanEncode(wire,&p)&&!memcmp(wire,old,64)&&mulRangingPlanFingerprint(&p)==oldHash);
 for(unsigned n=3;n<=8;n++) {
  p=plan();p.broadcast.periodMs=1000;p.broadcast.count=n;
  for(unsigned k=0;k<n;k++)p.broadcast.ids[k]=100+2*(n-k);
  for(unsigned k=n;k<8;k++)p.broadcast.ids[k]=0;
  p.enabled|=4u;assert(!mulRangingPlanValidate(&p,NULL));
  for(uint32_t c=UINT32_MAX-20;;c++) {
   mulRangingOperation_t op;assert(mulRangingPlanOperation(&p,c,0,&op));
   if(op.kind==4u)assert(op.initiator<op.responder&&op.responder!=p.broadcast.ids[0]);
   if(c==20)break;
  }
 }
 p=plan();p.enabled|=4u;p.policy=MUL_PLAN_FAIR;assert(mulRangingPlanValidate(&p,NULL));
 p=plan();p.enabled|=4u;p.broadcast.count=2;p.broadcast.ids[2]=0;assert(mulRangingPlanValidate(&p,NULL));
}
int main(void) {
 follower_pairs();
 mulRangingPlan_t p=plan();mulRangingBudget_t b;
 assert(!mulRangingPlanValidate(&p,&b));assert(b.dataEndUs==24100&&b.reservationUs==90000&&b.guardUs==85900);
 const unsigned initiators[]={132,132,132,133,134},responders[]={133,134,141,141,141};
 uint8_t bytes[64];assert(mulRangingPlanEncode(bytes,&p)&&bytes[0]==5);
 mulRangingPlan_t q;assert(mulRangingPlanDecode(bytes,&q));assert(mulRangingPlanFingerprint(&p)==mulRangingPlanFingerprint(&q));
 for(unsigned i=0;i<10000;i++) {
  mulRangingOperation_t op,remote;assert(mulRangingPlanCount(&p,i)==1&&mulRangingPlanOperation(&p,i,0,&op));
  assert(!mulRangingPlanOperation(&p,i,1,&remote)&&mulRangingPlanOperation(&q,i,0,&remote));
  assert(op.initiator==initiators[i%5]&&op.responder==responders[i%5]);
  assert(op.kind==(i%5<2?MUL_PLAN_AA:MUL_PLAN_AR));
  assert(op.startUs==24100&&op.pollUs==28100&&op.endUs==114100&&op.endUs+20000<=200000);
  assert(op.kind==remote.kind&&op.initiator==remote.initiator&&op.responder==remote.responder&&
         op.startUs==remote.startUs&&op.pollUs==remote.pollUs&&op.endUs==remote.endUs);
 }
 for(unsigned period=200;period<=1000;period+=50) {
  p.broadcast.periodMs=period;assert(!mulRangingPlanValidate(&p,&b));assert(b.guardUs==period*1000-114100);
  for(uint32_t cycle=UINT32_MAX-3;;cycle++) {
   mulRangingOperation_t op;assert(mulRangingPlanOperation(&p,cycle,0,&op));
   assert(op.initiator==initiators[cycle%5]&&op.responder==responders[cycle%5]);if(cycle==3)break;
  }
 }
 p=plan();p.twrWindowMs=87;assert(mulRangingPlanValidate(&p,NULL));p.twrWindowMs=88;assert(!mulRangingPlanValidate(&p,NULL));
 p=plan();p.broadcast.periodMs=100;assert(mulRangingPlanValidate(&p,NULL));
 p=plan();p.kalmanBudgetMs=4;assert(mulRangingPlanValidate(&p,NULL));
 p=plan();p.kalmanBudgetMs=50;assert(!mulRangingPlanValidate(&p,NULL));p.kalmanBudgetMs=100;assert(mulRangingPlanValidate(&p,NULL));
 p=plan();p.broadcast.slotMs=3;assert(mulRangingPlanValidate(&p,NULL));p.broadcast.rxBudgetUs=500;assert(mulRangingPlanValidate(&p,NULL));
 p=plan();p.enabled=0;assert(mulRangingPlanValidate(&p,NULL));p.enabled=MUL_PLAN_AA;p.rlrCount=0;p.rlrs[0]=0;
 assert(!mulRangingPlanValidate(&p,NULL));
 for(unsigned n=2;n<=8;n++)for(unsigned r=0;r<=8;r++) {
  p=plan();p.broadcast.periodMs=1000;p.broadcast.count=n;
  for(unsigned i=0;i<n;i++)p.broadcast.ids[i]=100+2*(n-i);
  for(unsigned i=n;i<8;i++)p.broadcast.ids[i]=0;
  p.rlrCount=r;for(unsigned i=0;i<8;i++)p.rlrs[i]=i<r?180+2*(r-i):0;
  p.enabled=MUL_PLAN_AA|(r?MUL_PLAN_AR:0);assert(!mulRangingPlanValidate(&p,NULL));
  bool seen[255][255]={0};unsigned total=n-1+n*r;
  for(unsigned cycle=0;cycle<total;cycle++) {
   mulRangingOperation_t op;assert(mulRangingPlanOperation(&p,cycle,0,&op));assert(!seen[op.initiator][op.responder]);
   seen[op.initiator][op.responder]=true;
   assert((op.kind==MUL_PLAN_AA&&op.initiator==p.broadcast.ids[0])||(op.kind==MUL_PLAN_AR&&op.responder>=180));
  }
 }
 p=plan();uint32_t hash=mulRangingPlanFingerprint(&p);
 p.kalmanBudgetMs++;assert(hash!=mulRangingPlanFingerprint(&p));p=plan();p.twrWindowMs++;assert(hash!=mulRangingPlanFingerprint(&p));
 printf("TDMA codec5 hash=%08X: one canonical pair/cycle, phases, guards, wrap, rosters, budgets and fail-closed rejection passed\n",hash);
}
