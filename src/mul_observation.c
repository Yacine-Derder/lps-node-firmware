/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_observation.h"
#include <string.h>

void mulObservationInit(mulObservation_t observations[2], const uint8_t receiver[8])
{
  memset(observations,0,2*sizeof(*observations));
  for (unsigned int i=0;i<2;i++) memcpy(observations[i].receiverClock,receiver,8);
}

bool mulObservationFresh(const mulObservation_t *observation, uint32_t nowMs)
{
  return observation->valid && (uint32_t)(nowMs-observation->acceptedMs) < MUL_SOURCE_TIMEOUT_MS;
}

void mulObservationExpire(mulObservation_t observations[2], uint32_t nowMs)
{
  for (unsigned int i=0;i<2;i++) if (!mulObservationFresh(&observations[i],nowMs)) observations[i].valid=false;
}

mulSequenceResult_t mulObservationRecord(mulObservation_t observations[2],
  const mulTestPacket_t *packet, const mulPowerRx_t *rx, uint32_t nowMs)
{
  mulAlrRole_t role=mulAlrRoleForAddress(packet->source);
  if (role == mulAlrUnsupported) return mulSequenceOld;
  mulObservation_t *o=&observations[role];
  bool rebase=o->source.hasSequence && packet->sequence != o->source.sequence &&
              (uint32_t)(nowMs-o->acceptedMs) >= MUL_SOURCE_TIMEOUT_MS;
  if ((!o->source.hasSequence || rebase) && o->generation == UINT32_MAX) {
    o->valid=false; return mulSequenceOld; // Generation exhaustion fails closed.
  }
  if (rebase) o->source.hasSequence=false;
  bool first=!o->source.hasSequence;
  mulSequenceResult_t result=mulObservePacket(&o->source,packet,rx->raw);
  if (result == mulSequenceOld || result == mulSequenceDuplicate) return result;
  if (first) o->generation++;
  o->acceptedMs=nowMs;
  o->correctedRx=mulTimestampMask(rx->corrected);
  o->power=rx->power;
  memcpy(o->sourceClock,packet->source,8);
  o->valid=true;
  o->remoteEpochKnown=false;
  return result;
}
