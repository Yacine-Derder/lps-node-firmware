/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_rlr.h"
#include "mul_common.h"
#include "mul_rx.h"
#include "task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Fixed slots keyed by the full identities in mulAlrRoleForAddress(). No dynamic
// allocation and no cross-source timestamp arithmetic or TDoA formation.
static mulSourceState_t rlrSources[MUL_ALR_COUNT];
static TickType_t lastSourceTick[MUL_ALR_COUNT];

static void mulRlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  (void)config;
  memset(rlrSources, 0, sizeof(rlrSources));
  memset(lastSourceTick, 0, sizeof(lastSourceTick));
  printf("MUL RLR: Mode initialized\r\n");
  mulRxInit(dev);
}

static uint32_t mulRlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  mulTestPacket_t packet;
  uint64_t arrival = 0;
  bool valid = event == eventPacketReceived && mulRxRead(dev, &packet, &arrival);
  mulRxRearm(dev); // Capture hardware time first; listen again before logging.
  if (valid) {
    mulAlrRole_t sourceRole = mulAlrRoleForAddress(packet.source);
    if (sourceRole != mulAlrUnsupported) {
      mulSourceState_t *state = &rlrSources[sourceRole];
      TickType_t now = xTaskGetTickCount();
      if (state->hasSequence && packet.sequence != state->sequence &&
          (TickType_t)(now - lastSourceTick[sourceRole]) >= pdMS_TO_TICKS(MUL_SOURCE_TIMEOUT_MS)) {
        state->hasSequence = false; // Rebase a stale source after a possible reboot.
      }
      mulSequenceResult_t result = mulObservePacket(state, &packet, arrival);
      // Suppress repeated fault traffic. The next fresh observation reports
      // accumulated per-source gaps, duplicate, and old-packet counters.
      if (result != mulSequenceDuplicate && result != mulSequenceOld) {
        lastSourceTick[sourceRole] = now;
        printf("MUL RLR: RX src=0x%02X seq=%" PRIu32
               " tx=0x%02X%08" PRIX32 " rx=0x%02X%08" PRIX32
               " n=%" PRIu32 " gaps=%" PRIu32 " dup=%" PRIu32 " old=%" PRIu32 "\r\n",
               (unsigned int)packet.source[0], packet.sequence,
               (unsigned int)(packet.txTimestamp >> 32), (uint32_t)packet.txTimestamp,
               (unsigned int)(arrival >> 32), (uint32_t)arrival,
               state->received, state->gaps, state->duplicates, state->outOfOrder);
      }
    }
  }
  return MAX_TIMEOUT;
}

uwbAlgorithm_t mulRlrAlgorithm = {
  .init = mulRlrInit,
  .onEvent = mulRlrOnEvent,
};
