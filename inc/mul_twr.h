/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_TWR_H
#define MUL_TWR_H

#include "mul_clock.h"
#include "uwb.h"
#include "mul_power.h"
#include "mul_plan_protocol.h"

#include "../portable/mul_core/mul_legacy_twr.h"

// Bounded radio/measurement state. Packet buffers live on the UWB task stack.
typedef struct {
  uint64_t stamps[6];
  uint64_t expectedTx;
  uint64_t deadline;
  uint64_t windowEnd;
  uint64_t epoch;
  uint32_t id;
  uint32_t measuredMs; // Committed measurement only; never updated by attempts.
  uint32_t estimateId;
  uint16_t tof; // Committed estimate (corrected if available, otherwise RAW).
  uint16_t rawTof;
  uint16_t candidateTof;

  mulPower_t power[3];
  mulTwrType_t stage;
  uint8_t active:1;
  uint8_t txPending:1;
  uint8_t attempted:1;
  uint8_t delayValid:1;
  uint8_t syncAvailable:1;
  uint8_t deferredCancel:1;
  uint8_t syncDirty:1;
  uint8_t powerCorrected:1;
  uint8_t candidatePowerCorrected:1;
  uint8_t invalidated:1;
  uint8_t multiProfile:1;
  uint8_t syncWindow:1;
  uint8_t followerSlot:3;
  uint8_t planned:1;
  uint8_t operation;
} mulTwr_t;

void mulTwrEncode(uint8_t frame[MUL_TWR_FRAME_LENGTH], const mulTwrPacket_t *packet);
bool mulTwrDecode(const uint8_t *frame, size_t length, mulTwrPacket_t *packet);
bool mulTwrCalculate(const uint64_t stamps[6], uint32_t *tofReferenceDtu);
bool mulTwrResults(const uint64_t stamps[6], const mulPower_t power[3],
  uint32_t *raw, uint32_t *corrected); // UINT32_MAX corrected means unavailable.
void mulTwrConfigureNetwork(const mulNetwork_t *network);
void mulTwrInit(mulTwr_t *twr);
bool mulTwrConfigurePlan(mulTwr_t *twr,const mulRangingPlan_t *plan,uint32_t cycle,unsigned operation);
bool mulTwrConfigureSchedule(mulTwr_t *twr,const mulSchedule_t *schedule,unsigned slot,unsigned window);
void mulTwrCancel(mulTwr_t *twr, dwDevice_t *dev, const char *reason, bool invalidateDelay);
void mulTwrFlush(mulTwr_t *twr);
bool mulTwrDue(uint32_t referenceSequence);
bool mulTwrStart(mulTwr_t *twr, dwDevice_t *dev, uint32_t sequence, uint64_t origin);
// Caller never dispatches here while a broadcast TX is armed. A normal broadcast
// is left unread for the ALR path and cancels any exchange, giving it priority.
bool mulTwrOnEvent(mulTwr_t *twr, dwDevice_t *dev, uwbEvent_t event, mulAlrRole_t role,
  uint32_t sequence, uint64_t localOrigin, uint64_t referenceOrigin, uint64_t epoch, uint32_t nowMs);
uint32_t mulTwrWait(const mulTwr_t *twr, dwDevice_t *dev, uint64_t localOrigin, uint32_t fallback);
bool mulTwrDelay(const mulTwr_t *twr, const mulClock_t *clock, mulClockDelay_t *delay);
mulPropagationState_t mulTwrPropagationState(const mulTwr_t *twr, const mulClock_t *clock, uint32_t nowMs);
void mulTwrSync(mulTwr_t *twr, const mulClock_t *clock, uint32_t nowMs, bool force);
uint32_t mulTwrDelayWait(const mulTwr_t *twr, uint32_t nowMs, uint32_t fallback);

#endif
