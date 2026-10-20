/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_rlr.h"
#include "mul_common.h"
#include "mul_rx.h"
#include "mul_power.h"
#include "mul_observation.h"
#include "mul_downlink.h"
#include "mul_localization.h"
#include "mul_role_state.h"
#include "task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Fixed slots keyed by the full identities in mulAlrRoleForAddress(). No dynamic
// allocation. Each observation preserves the source and receiver clock domains.
static mulObservation_t rlrObservations[MUL_ALR_COUNT];
static mulAssociation_t rlrAssociation;

static void mulRlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  memset(&mulRoleScratch,0,sizeof(mulRoleScratch));
  mulClockReset(&mulRoleScratch.rlr.rate);
  mulObservationInit(rlrObservations,config->address);
  memset(&rlrAssociation,0,sizeof(rlrAssociation));
  printf("MUL RLR: Mode initialized slot_ms=%u rate_unit=ppb rate_policy=%s prop_max_age_ms=%u motion_mm_s=%d\r\n",
    (unsigned int)MUL_FOLLOWER_SLOT_MS,MUL_EXPERIMENTAL_STATIONARY_RLR_RATE ? "STATIC_EXPERIMENTAL" : "UNAVAILABLE",
    (unsigned int)MUL_PROP_MAX_AGE_MS,(int)MUL_RELATIVE_SPEED_MM_S);
  mulRxInit(dev);
}

static uint32_t mulRlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  mulTestPacket_t packet;
  uint64_t arrival = 0;
  mulRelation_t relation={0};
  uint32_t captureAgeMs=UINT32_MAX;
  bool valid=event==eventPacketReceived && mulRxReadBroadcast(dev,&packet,&arrival,&relation);
  mulPowerRx_t rx={0};
  if (valid && mulAlrRoleForAddress(packet.source) != mulAlrUnsupported) rx=mulPowerCapture(dev,arrival);
  if (valid && (relation.available || relation.version==2)) {
    dwTime_t hardware={.full=0}; dwGetSystemTimestamp(dev,&hardware);
    int64_t elapsed=mulTimestampDifference(hardware.full,arrival);
    if (elapsed>=0 && elapsed<=INT64_C(15)*63897600) captureAgeMs=(uint32_t)((elapsed+63897599)/63897600);
  }
  mulRxRearm(dev); // Freeze timestamp AND power diagnostics before re-arming.
  uint32_t now=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
  mulAssociationExpire(&rlrAssociation,rlrObservations,now);
  if (valid) {
    mulAlrRole_t role=mulAlrRoleForAddress(packet.source);
    if (role != mulAlrUnsupported) {
      mulSequenceResult_t result;
      mulPair_t pair;
      bool hadFollower=rlrObservations[1].source.hasSequence;
      bool associated=mulAssociate(&rlrAssociation,rlrObservations,&packet,&rx,now,&result,&pair);
      mulObservation_t *observation=&rlrObservations[role];
      mulSourceState_t *state=&observation->source;
      if (role==mulAlrFollower && result!=mulSequenceDuplicate && result!=mulSequenceOld) {
        if (hadFollower && relation.epoch!=mulRoleScratch.rlr.relation.epoch) {
          associated=false; rlrAssociation.pending=false;
          mulClockReset(&mulRoleScratch.rlr.rate);
          if (observation->generation==UINT32_MAX) observation->valid=false; else observation->generation++;
        }
        mulRoleScratch.rlr.relation=relation; // Even unavailable replaces previous metadata.
        observation->remoteEpochKnown=relation.epoch!=0; // Acquisition-local, not a boot UUID.
      }
      if (role==mulAlrReference && result!=mulSequenceDuplicate && result!=mulSequenceOld) {
        if (result==mulSequenceFirst) mulClockReset(&mulRoleScratch.rlr.rate);
        // Reuse skew ONLY, never the arrival mapping/intercept at the RLR.
        mulClockObserve(&mulRoleScratch.rlr.rate,&packet,arrival,now,true);
      }
      if (!associated && result != mulSequenceDuplicate && result != mulSequenceOld) {
        printf("MUL RLR: RX src=0x%02X seq=%" PRIu32
               " tx=0x%02X%08" PRIX32 " rx=0x%02X%08" PRIX32
               " n=%" PRIu32 " gaps=%" PRIu32 " dup=%" PRIu32 " old=%" PRIu32
               " crx=0x%02X%08" PRIX32 " Q=%d/%d/%u/%u gen=%" PRIu32 "\r\n",
               (unsigned int)packet.source[0], packet.sequence,
               (unsigned int)(packet.txTimestamp >> 32), (uint32_t)packet.txTimestamp,
               (unsigned int)(arrival >> 32), (uint32_t)arrival,
               state->received, state->gaps, state->duplicates, state->outOfOrder,
               (unsigned int)(rx.corrected>>32),(uint32_t)rx.corrected,
               rx.power.powerCentiDbm,rx.power.deltaDtu,rx.power.status,rx.power.table,
               observation->generation);
      }
      if (associated) {
        pair.follower.remoteEpochKnown=observation->remoteEpochKnown;
        mulLocalization_t observation;
        mulLocalizationObserve(&observation,&pair,&relation,&mulRoleScratch.rlr.rate,
          now,captureAgeMs,MUL_EXPERIMENTAL_STATIONARY_RLR_RATE!=0);
        mulLocalizationPrint(&observation);
      }
    }
  }
  if (rlrAssociation.pending) return MUL_PAIR_MAX_AGE_MS+1-(now-rlrObservations[0].acceptedMs);
  return MAX_TIMEOUT;
}

uwbAlgorithm_t mulRlrAlgorithm = {
  .init = mulRlrInit,
  .onEvent = mulRlrOnEvent,
};
