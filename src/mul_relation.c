/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_relation.h"
#include <string.h>
static void put(uint8_t *p,uint64_t v,unsigned int n) { for (unsigned int i=0;i<n;i++) p[i]=v>>(8*i); }
static uint64_t get(const uint8_t *p,unsigned int n) {
  uint64_t v=0; for (unsigned int i=0;i<n;i++) v|=(uint64_t)p[i]<<(8*i); return v;
}

void mulRelationEncode(uint8_t frame[MUL_RELATION_FRAME_LENGTH],
  const mulTestPacket_t *p,const mulRelation_t *r)
{
  memset(frame,0,MUL_RELATION_FRAME_LENGTH);
  mulEncodeTestPacket(frame,p->source,p->sequence,p->txTimestamp);
  frame[24]=MUL_PACKET_CLOCK_BROADCAST; frame[34]=1;
  frame[35]=2 | (r->available ? 1 : 0) | (r->powerCorrectedTwr ? 4 : 0); // Physical bounds UNKNOWN.
  put(frame+36,r->epoch,8); put(frame+44,r->referenceSequence,4);
  put(frame+48,r->twrSequence,4); put(frame+52,r->referenceOrigin,5);
  put(frame+57,r->followerOrigin,5); put(frame+62,(uint32_t)r->skewPpb,4);
  put(frame+66,r->originAgeMs,2); put(frame+68,r->delayAgeMs,2);
  put(frame+70,MUL_CLOCK_DELAY_DEFAULT_MAX_AGE_MS,2); put(frame+72,r->remainingMs,2);
}

bool mulRelationInverse(const mulRelation_t *r,uint64_t tx,uint32_t age,uint64_t *ref)
{
  if (!r->available || r->epoch==0 || r->skewPpb < -MUL_CLOCK_MAX_PPB ||
      r->skewPpb > MUL_CLOCK_MAX_PPB || r->remainingMs==0 || age > r->remainingMs ||
      r->delayAgeMs > 1000 || r->remainingMs > 1000-r->delayAgeMs ||
      (uint32_t)r->originAgeMs+age > MUL_CLOCK_MAP_HORIZON_MS ||
      (uint32_t)(r->referenceSequence-r->twrSequence)>3) return false;
  int64_t local=mulTimestampDifference(tx,r->followerOrigin);
  if (local < -(int64_t)MUL_CLOCK_MAP_HORIZON_DTU || local > (int64_t)MUL_CLOCK_MAP_HORIZON_DTU) return false;
  int64_t denominator=INT64_C(1000000000)+r->skewPpb;
  int64_t product=local*r->skewPpb; // <=2 s * 200000 ppb, fits int64.
  int64_t adjustment=product >= 0 ? (product+denominator/2)/denominator : -((-product+denominator/2)/denominator);
  int64_t d=local-adjustment;
  uint64_t magnitude=(uint64_t)(d<0 ? -d : d);
  if (magnitude > MUL_CLOCK_MAP_HORIZON_DTU) return false;
  uint32_t span=(r->delayAgeMs > r->originAgeMs ? r->delayAgeMs-r->originAgeMs : r->originAgeMs-r->delayAgeMs)+
    (uint32_t)((magnitude+63897599)/63897600);
  if (span < r->delayAgeMs) span=r->delayAgeMs;
  if (span+age > 1000 || r->remainingMs > 1000-span) return false;
  *ref=mulTimestampMask(r->referenceOrigin+d); return true;
}

bool mulBroadcastDecode(const uint8_t *frame,size_t length,mulTestPacket_t *p,mulRelation_t *r)
{
  if (!frame || !p || !r) return false;
  mulTestPacket_t packet; mulRelation_t relation={0};
  if (length==MUL_TEST_FRAME_LENGTH) {
    if (!mulDecodeTestPacket(frame,length,&packet)) return false;
  } else {
    if (length!=MUL_RELATION_FRAME_LENGTH || frame[24]!=MUL_PACKET_CLOCK_BROADCAST || frame[34]!=1 ||
        (frame[35]&~7)!=0 || (frame[35]&2)==0 || get(frame+70,2)!=1000) return false;
    uint8_t base[MUL_TEST_FRAME_LENGTH]; memcpy(base,frame,sizeof(base)); base[24]=MUL_PACKET_TEST_BROADCAST;
    if (!mulDecodeTestPacket(base,sizeof(base),&packet) || mulAlrRoleForAddress(packet.source)!=mulAlrFollower) return false;
    relation.epoch=get(frame+36,8); relation.referenceSequence=get(frame+44,4);
    relation.twrSequence=get(frame+48,4); relation.referenceOrigin=get(frame+52,5);
    relation.followerOrigin=get(frame+57,5);
    uint32_t bits=(uint32_t)get(frame+62,4);
    relation.skewPpb=bits<=INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits-INT64_C(4294967296));
    relation.originAgeMs=get(frame+66,2); relation.delayAgeMs=get(frame+68,2); relation.remainingMs=get(frame+72,2);
    relation.available=frame[35]&1; relation.powerCorrectedTwr=frame[35]&4;
    if (relation.available) {
      uint64_t converted;
      if (!mulRelationInverse(&relation,packet.txTimestamp,0,&converted)) return false;
    } else if (relation.twrSequence || relation.referenceOrigin || relation.followerOrigin || relation.skewPpb ||
        relation.originAgeMs || relation.delayAgeMs || relation.remainingMs || relation.powerCorrectedTwr) return false;
  }
  *p=packet; *r=relation; return true;
}

mulRelation_t mulRelationPublish(const mulClock_t *clock,const mulClockDelay_t *delay,
  uint64_t tx,uint64_t hardwareNow,uint32_t nowMs,bool powerCorrectedTwr)
{
  mulRelation_t unavailable={.epoch=clock->epoch,.referenceSequence=clock->sequence};
  int64_t lead=mulTimestampDifference(tx,hardwareNow);
  if (!delay || delay->maxAgeMs!=1000 || lead < 0 || lead > INT64_C(600)*63897600) return unavailable;
  // Outward coarse rounding plus one ms guard; check contracts at actual TX,
  // not the reference RX callback roughly half a second earlier.
  uint32_t atTx=nowMs+(uint32_t)((lead+63897599)/63897600)+1;
  mulClockCorrectedMapping_t origin,inverse;
  if (!mulClockMapRefToFollowerSimultaneous(clock,clock->referenceTx,delay,atTx,&origin) ||
      !mulClockMapFollowerSimultaneousToRef(clock,tx,delay,atTx,&inverse)) return unavailable;
  uint32_t span=origin.applicationAgeMs > inverse.applicationAgeMs ? origin.applicationAgeMs : inverse.applicationAgeMs;
  uint32_t remaining=1000-span;
  uint32_t originRemaining=2000-origin.affine.ageMs;
  if (remaining>originRemaining) remaining=originRemaining;
  if (remaining<=1) return unavailable;
  mulRelation_t result={.available=true,.epoch=clock->epoch,.referenceSequence=clock->sequence,
    .twrSequence=delay->referenceSequence,.referenceOrigin=clock->referenceTx,.followerOrigin=origin.timestamp,
    .skewPpb=clock->skewPpb,.originAgeMs=origin.affine.ageMs,.delayAgeMs=origin.delayAgeMs,
    .remainingMs=remaining-1,.powerCorrectedTwr=powerCorrectedTwr};
  uint64_t check;
  if (!mulRelationInverse(&result,tx,0,&check)) return unavailable;
  return result;
}
