/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_downlink.h"
#include <string.h>
bool mulDownlinkRelation(const mulRelation_t *r,mulPair_t *pair,
  uint32_t nowMs,uint32_t captureAgeMs,int64_t *txSeparation)
{
  pair->clockRelationAvailable=false; pair->geometricTdoaAvailable=false;
  if (!r->available || captureAgeMs>15 ||
      !mulObservationFresh(&pair->reference,nowMs) || !mulObservationFresh(&pair->follower,nowMs) ||
      mulAlrRoleForAddress(pair->reference.sourceClock)!=mulAlrReference ||
      mulAlrRoleForAddress(pair->follower.sourceClock)!=mulAlrFollower ||
      memcmp(pair->reference.receiverClock,pair->follower.receiverClock,8) ||
      r->referenceSequence!=pair->reference.source.sequence ||
      r->referenceOrigin!=pair->reference.source.txTimestamp) return false;
  uint32_t age=nowMs-pair->follower.acceptedMs;
  if (age>r->remainingMs || captureAgeMs>r->remainingMs-age) return false;
  uint64_t converted;
  if (!mulRelationInverse(r,pair->follower.source.txTimestamp,age+captureAgeMs,&converted)) return false;
  int64_t dt=mulTimestampDifference(converted,pair->reference.source.txTimestamp);
  if (dt<INT64_C(450)*63897600 || dt>INT64_C(550)*63897600) return false;
  *txSeparation=dt; pair->clockRelationAvailable=true;
  return true;
}
