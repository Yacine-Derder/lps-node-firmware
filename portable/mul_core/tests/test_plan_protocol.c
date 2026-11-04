#include "mul_plan_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void binding_equivalence(void) {
 for(unsigned n=3;n<=4;n++)for(unsigned ff=0;ff<2;ff++) {
  mulRangingPlan_t p={0};mulScheduleLegacy(&p.broadcast,132,134,5);mulScheduleExperimental(&p.broadcast);
  p.broadcast.count=n;p.broadcast.ids[2]=133;if(n==4)p.broadcast.ids[3]=135;
  p.broadcast.periodMs=100;p.broadcast.txLeadUs=1000;p.broadcast.rxBudgetUs=2000;p.broadcast.rangingFlags=0;
  p.rlrCount=1;p.rlrs[0]=141;p.replyMs=5;mulRangingPlanTdma(&p);p.twrWindowMs=30;if(ff)p.enabled|=MUL_PLAN_FF;
  unsigned kinds=0;
  for(uint32_t step=0;step<40;step++) {
   uint32_t cycle=UINT32_MAX-20+step;mulPlanTwrBinding_t b;
   assert(mulPlanTwrBind(&b,&p,cycle,0));
   mulRangingOperation_t op;assert(mulRangingPlanOperation(&p,cycle,0,&op));kinds|=op.kind;
   assert(b.network.addresses[0][0]==op.initiator&&b.network.addresses[1][0]==op.responder);
   for(unsigned stage=mulTwrPoll;stage<=mulTwrResult;stage++) {
    unsigned used=stage==mulTwrPoll?1:stage==mulTwrResponse?3:stage==mulTwrFinal?5:6;
    mulTwrPacket_t packet={.type=stage,.id=cycle,.txTimestamp=123},a,z;
    for(unsigned i=0;i<used;i++)packet.stamps[i]=123;
    uint8_t legacy[90],fast[90];
    assert(mulPlanTwrEncode(legacy,&p,0,&packet)&&mulPlanTwrEncodeBound(fast,&b,&packet));
    assert(!memcmp(legacy,fast,90));
    assert(mulPlanTwrDecode(&p,cycle,0,legacy,90,&a)&&mulPlanTwrDecodeBound(&b,fast,90,&z));
    assert(!memcmp(&a,&z,sizeof a));
    /* Every one-bit mutation retains exactly the original decoder verdict,
     * including source/destination, prefix, power and unused stamp fields. */
    for(unsigned i=0;i<90;i++)for(unsigned bit=0;bit<8;bit++) {
     fast[i]^=1u<<bit;
     assert(mulPlanTwrDecode(&p,cycle,0,fast,90,&a)==mulPlanTwrDecodeBound(&b,fast,90,&z));
     fast[i]^=1u<<bit;
    }
    for(unsigned len=0;len<90;len++)assert(!mulPlanTwrDecodeBound(&b,fast,len,&z));
    mulPlanTwrBinding_t other=b;other.cycle++;assert(!mulPlanTwrDecodeBound(&other,fast,90,&z));
    other=b;other.operation++;assert(!mulPlanTwrDecodeBound(&other,fast,90,&z));
    other=b;other.fingerprint^=1;assert(!mulPlanTwrDecodeBound(&other,fast,90,&z));
    packet.id++;assert(!mulPlanTwrEncodeBound(fast,&b,&packet));
   }
  }
  assert(kinds==(MUL_PLAN_AA|MUL_PLAN_AR|(ff?MUL_PLAN_FF:0)));
 }
}
int main(void) {
 binding_equivalence();
 // The reported RF4 collision used a common reference and first follower.
 mulRangingPlan_t two={.enabled=3,.replyMs=20,.maxUtilization=70,.aaMilliHz=1000,.arMilliHz=50,.rlrCount=1,.rlrs={141}};
 mulScheduleLegacy(&two.broadcast,132,133,10);mulScheduleExperimental(&two.broadcast);
 two.broadcast.rangingFlags=0;two.broadcast.rxBudgetUs=5000;two.broadcast.txLeadUs=1000;two.broadcast.revision=23;
 mulRangingPlan_t three=two;three.broadcast.count=3;three.broadcast.ids[2]=134;
 uint8_t collisionFrame[90];mulMultiBroadcast_t collisionOut;
 assert(mulPlanBroadcastEncode(collisionFrame,90,&two,0,1,100,NULL)==40);
 assert(!mulPlanBroadcastDecode(&three,collisionFrame,40,&collisionOut));
 assert(mulPlanBroadcastEncode(collisionFrame,90,&three,0,1,100,NULL)==40);
 assert(!mulPlanBroadcastDecode(&two,collisionFrame,40,&collisionOut));

 mulRangingPlan_t p={.enabled=3,.replyMs=5,.maxUtilization=80,.aaMilliHz=10000,.arMilliHz=5500,.rlrCount=2,.rlrs={141,142}};
 mulScheduleLegacy(&p.broadcast,132,133,2);mulScheduleExperimental(&p.broadcast);
 p.broadcast.txLeadUs=500;p.broadcast.rxBudgetUs=100;p.broadcast.airtimeUs=400;p.broadcast.rangingFlags=0;
 assert(!mulRangingPlanValidate(&p,NULL));assert(mulRangingPlanCount(&p,0)==32);
 uint8_t frame[90];mulMultiBroadcast_t decoded;
 size_t n=mulPlanBroadcastEncode(frame,sizeof(frame),&p,0,UINT32_MAX,UINT64_C(0xffffffffff),NULL);
 assert(n==40&&frame[34]==5&&mulPlanBroadcastDecode(&p,frame,n,&decoded));
 frame[34]=4;assert(!mulPlanBroadcastDecode(&p,frame,n,&decoded));frame[34]=5;
 assert(decoded.packet.sequence==UINT32_MAX&&decoded.packet.txTimestamp==UINT64_C(0xffffffffff));
 assert(!mulMultiBroadcastDecode(&p.broadcast,frame,n,&decoded));
 mulRangingPlan_t different=p;different.rlrs[1]=143;
 assert(mulScheduleFingerprint(&different.broadcast)==mulScheduleFingerprint(&p.broadcast));
 assert(!mulPlanBroadcastDecode(&different,frame,n,&decoded));
 for(unsigned length=0;length<40;length++)assert(!mulPlanBroadcastDecode(&p,frame,length,&decoded));
 mulRelation_t r={.version=2,.epoch=8,.referenceSequence=10,.maxAgeMs=MUL_PROP_MAX_AGE_MS};
 assert(mulPlanBroadcastEncode(frame,sizeof(frame),&p,1,10,123,&r)==80);
 assert(mulPlanBroadcastDecode(&p,frame,80,&decoded)&&decoded.slot==1);
 bool aa=false,ar=false;
 for(unsigned index=0;index<32;index++) {
  mulRangingOperation_t op;assert(mulRangingPlanOperation(&p,0,index,&op));aa|=op.kind==1;ar|=op.kind==2;
  mulTwrPacket_t packet={.type=mulTwrPoll,.id=0,.stamps={123},.txTimestamp=123},out;
  assert(mulPlanTwrEncode(frame,&p,index,&packet));assert(frame[84]==5&&frame[85]==index);
  assert(mulPlanTwrDecode(&p,0,index,frame,90,&out)&&out.id==0);
  frame[84]=4;assert(!mulPlanTwrDecode(&p,0,index,frame,90,&out));frame[84]=5;
  assert(frame[13]==op.initiator&&frame[5]==op.responder);
  assert(!mulPlanTwrDecode(&different,0,index,frame,90,&out));
  assert(!mulPlanTwrDecode(&p,1,index,frame,90,&out));
  unsigned ignored;assert(!mulMultiTwrDecode(&p.broadcast,0,frame,90,&out,&ignored));
  frame[85]^=1;assert(!mulPlanTwrDecode(&p,0,index,frame,90,&out));frame[85]^=1;
  frame[13]^=1;assert(!mulPlanTwrDecode(&p,0,index,frame,90,&out));
 }
 assert(aa&&ar);
 // Codec5 keeps RF5 framing and binds the exact shared cycle and canonical pair.
 p=three;p.broadcast.periodMs=200;p.broadcast.slotMs=5;p.broadcast.rxBudgetUs=2000;
 mulRangingPlanTdma(&p);assert(!mulRangingPlanValidate(&p,NULL));
 mulRangingPlan_t remote;uint8_t saved[64];assert(mulRangingPlanEncode(saved,&p)&&mulRangingPlanDecode(saved,&remote));
 for(uint32_t cycle=0;cycle<500;cycle++) {
  assert(mulPlanBroadcastEncode(frame,sizeof(frame),&p,0,cycle,123,NULL)==40);
  assert(mulPlanBroadcastDecode(&remote,frame,40,&decoded));
  mulTwrPacket_t poll={.type=mulTwrPoll,.id=cycle,.stamps={123},.txTimestamp=123},out;
  assert(mulPlanTwrEncode(frame,&p,0,&poll));
  assert(mulPlanTwrDecode(&remote,cycle,0,frame,90,&out));
  assert(!mulPlanTwrDecode(&remote,cycle+1,0,frame,90,&out));
  assert(!mulPlanTwrDecode(&remote,cycle,1,frame,90,&out));
 }
 remote.kalmanBudgetMs++;
 mulTwrPacket_t poll={.type=mulTwrPoll,.id=0};assert(mulPlanTwrEncode(frame,&p,0,&poll));
 assert(!mulPlanTwrDecode(&remote,0,0,frame,90,&poll));
 puts("Plan radio v5: full-plan/RLR-roster binding, strict legacy rejection, broadcast40/80, TWR90, all32 operation indices, generic AA/AR identities and cycle/source/index/length rejection passed");
 return 0;
}
