#include "mul_localization.h"
#include <inttypes.h>
#include <stdio.h>
#include "../portable/mul_core/mul_localization.c"
static void unsignedField(const char *name,uint32_t v) {
  printf(" %s=",name); if (v==UINT32_MAX) printf("UNKNOWN"); else printf("%" PRIu32,v);
}
void mulLocalizationPrint(const mulLocalization_t *o)
{
  static const char *const states[]={"NONE","FRESH","REUSED","EXPIRED","INVALIDATED"};
  static const char *const statuses[]={"NO_RELATION","NO_RATE","STALE_RATE","INCONSISTENT","AVAILABLE"};
  printf("MUL OBS: ref=%02X/%" PRIu32 " fol=%02X/%" PRIu32 " t_ms=%" PRIu32 " rx=%02X%08" PRIX32
    " gen=%" PRIu32 "/%" PRIu32 " epoch=%08" PRIX32 "%08" PRIX32 " status=%s dtu=",
    o->reference[0],o->referenceSequence,o->follower[0],o->followerSequence,o->observedMs,
    (unsigned int)(o->receiverTimestamp>>32),(uint32_t)o->receiverTimestamp,
    o->referenceGeneration,o->followerGeneration,(uint32_t)(o->followerEpoch>>32),(uint32_t)o->followerEpoch,
    o->status<=mulDownlinkAvailable ? statuses[o->status] : "INCONSISTENT");
  if (o->status==mulDownlinkAvailable) printf("%" PRId32 " mm=%" PRId32,o->tdoaDtu,o->rangeDifferenceMm);
  else printf("NA mm=NA");
  printf(" slot_ms=%u",(unsigned int)o->slotMs);
  unsignedField("delay_dtu",o->delayDtu==UINT16_MAX ? UINT32_MAX : o->delayDtu);
  unsignedField("delay_age_ms",o->delayAgeMs);
  printf(" delay_state=%s rate=",o->delayState<=mulPropagationInvalidated ? states[o->delayState] : "INVALIDATED");
  if (o->rateState==mulRateStationary) printf("STATIC/%" PRId32,o->ratePpb);
  else printf(o->rateState==mulRateStale ? "STALE" : "NONE");
  unsignedField("clock_age_ms",o->clockAgeMs==UINT16_MAX ? UINT32_MAX : o->clockAgeMs);
  printf(" clock=%u power=%u assoc=%u",(unsigned int)o->clockValid,(unsigned int)o->powerFlags,(unsigned int)o->associationValid);
  unsignedField("motion_mm",o->motionBoundMm); unsignedField("motion_dtu",o->motionBoundDtu);
  printf(" quality=%02X uncertainty=UNKNOWN\r\n",o->unknownComponents);
}
