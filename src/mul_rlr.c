/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_rlr.h"
#include "mul_common.h"
#include "mul_rx.h"
#include "mul_power.h"
#include "mul_observation.h"
#include "task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Fixed slots keyed by the full identities in mulAlrRoleForAddress(). No dynamic
// allocation. Each observation preserves the source and receiver clock domains.
static mulObservation_t rlrObservations[MUL_ALR_COUNT];

static void mulRlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  mulObservationInit(rlrObservations,config->address);
  printf("MUL RLR: Mode initialized\r\n");
  mulRxInit(dev);
}

static uint32_t mulRlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  mulTestPacket_t packet;
  uint64_t arrival = 0;
  bool valid = event == eventPacketReceived && mulRxRead(dev, &packet, &arrival);
  mulPowerRx_t rx={0};
  if (valid && mulAlrRoleForAddress(packet.source) != mulAlrUnsupported) rx=mulPowerCapture(dev,arrival);
  mulRxRearm(dev); // Freeze timestamp AND power diagnostics before re-arming.
  uint32_t now=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
  mulObservationExpire(rlrObservations,now);
  if (valid) {
    mulAlrRole_t role=mulAlrRoleForAddress(packet.source);
    if (role != mulAlrUnsupported) {
      mulSequenceResult_t result=mulObservationRecord(rlrObservations,&packet,&rx,now);
      mulObservation_t *observation=&rlrObservations[role];
      mulSourceState_t *state=&observation->source;
      if (result != mulSequenceDuplicate && result != mulSequenceOld) {
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
    }
  }
  return MAX_TIMEOUT;
}

uwbAlgorithm_t mulRlrAlgorithm = {
  .init = mulRlrInit,
  .onEvent = mulRlrOnEvent,
};
