/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_OBSERVATION_H
#define MUL_OBSERVATION_H
#include "mul_common.h"
#include "mul_power.h"

// One bounded slot per full supported source identity. source.txTimestamp is in
// sourceClock; source.rxTimestamp and correctedRx are in receiverClock.
// generation is receiver-local, inferred from accepted progress/liveness only.
// No remote boot epoch is present on the broadcast wire.
typedef struct {
  mulSourceState_t source;
  uint64_t correctedRx;
  uint8_t sourceClock[8];
  uint8_t receiverClock[8];
  uint32_t generation;
  uint32_t acceptedMs;
  mulPower_t power;
  bool valid;
  bool remoteEpochKnown;
} mulObservation_t;

void mulObservationInit(mulObservation_t observations[2], const uint8_t receiver[8]);
bool mulObservationFresh(const mulObservation_t *observation, uint32_t nowMs);
void mulObservationExpire(mulObservation_t observations[2], uint32_t nowMs);
mulSequenceResult_t mulObservationRecord(mulObservation_t observations[2],
  const mulTestPacket_t *packet, const mulPowerRx_t *rx, uint32_t nowMs);
#endif
