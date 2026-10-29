/*
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * LPS node firmware.
 *
 * Copyright 2016, Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 */
/* cfg.h: node configuration handling */
#ifndef __CFG_H__
#define __CFG_H__

#include <stdbool.h>
#include <stdint.h>
#include "mul_schedule.h"
#include "mul_clock_fit.h"

typedef enum {
  cfgAddress = 0,
  cfgMode,
  cfgAnchorlist,
  cfgAnchorPos,
  cfgSmartPower,
  cfgForceTxPower,
  cfgTxPower,
  cfgLowBitrate,
  cfgLongPreamble,
  cfgMulNetwork, // New three-byte TLV: local role, reference ID, follower ID.
  cfgRfEnable, // Optional one-byte boolean; absent retains legacy enabled behavior.
  cfgMulSlotMs, // Additive uint32 delay TLV; absent old EEPROM retains 500ms.
  cfgMulSchedule, // Optional canonical32-byte schedule, including its CRC.
  cfgMulClock, // Additive versioned12-byte clock estimator configuration.
  cfgMulGeometrySource, // Additive byte:0 TWR (legacy default),1 external,2 none.
} ConfigField;

void cfgInit();
#include "mul_plan_store.h"
// Separate EEPROM journal; physical load/save require acknowledged RF OFF.
mulPlanStoreResult_t cfgMulPlanCached(const mulStoredPlan_t **stored);
mulPlanStoreResult_t cfgMulPlanLoad(mulStoredPlan_t *stored);
bool cfgMulPlanSave(const mulRangingPlan_t *plan,bool selected,uint32_t *generation);
bool cfgPendingRestart(void);
bool cfgStorageReady(void);
bool cfgRfEnabled(bool *enabled);
bool cfgGeometrySourceRead(mulDelaySource_t *source);
bool cfgGeometrySourceWrite(mulDelaySource_t source);
bool cfgMulClockRead(mulClockFitConfig_t *config);
bool cfgMulClockWrite(const mulClockFitConfig_t *config);
bool cfgMulSlotRead(uint16_t *slotMs);
bool cfgMulSlotWrite(uint16_t slotMs);
bool cfgMulScheduleRead(uint8_t node,mulSchedule_t *schedule);
bool cfgMulScheduleWrite(uint8_t node,const mulSchedule_t *schedule);

// Independent commit-last journal at384/416, no legacy TLV rewrite.
bool cfgAntennaWrite(uint16_t tx,uint16_t rx);
bool cfgReset(void);

bool cfgFieldSize(ConfigField field, uint8_t * size);

bool cfgReadU8(ConfigField field, uint8_t * value);

bool cfgWriteU8(ConfigField field, uint8_t data);

bool cfgReadU32(ConfigField field, uint32_t * value);

bool cfgWriteU32(ConfigField field, uint32_t value);

bool cfgReadU8list(ConfigField field, uint8_t list[], uint8_t lenth);

bool cfgWriteU8list(ConfigField field, uint8_t list[], uint8_t lenth);

bool cfgReadFP32listLength(ConfigField field, uint8_t * size);

bool cfgReadFP32list(ConfigField field, float list[], uint8_t length);

bool cfgWriteFP32list(ConfigField field, float list[], uint8_t length);

// Legacy EEPROM is read without writes. Missing roster derives its original
// role once; changing address first persists that role, so role follows no ID.
// role:0 reference,1 follower,2 disabled/unassigned,3 explicit passive RLR. IDs are full canonical suffixes.
bool cfgMulRead(uint8_t node,uint8_t *role,uint8_t *reference,uint8_t *follower);
bool cfgMulWrite(uint8_t role,uint8_t reference,uint8_t follower);
bool cfgMulChangeAddress(uint8_t address);
void cfgSetBinaryMode(bool enable);

bool cfgIsBinaryMode();

#endif
