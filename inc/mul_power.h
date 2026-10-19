/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_POWER_H
#define MUL_POWER_H
#include "mul_common.h"
#include "libdw1000.h"

typedef enum {
  mulPowerMissing = 0,
  mulPowerApplied,
  mulPowerInvalid,
  mulPowerOutOfRange,
  mulPowerUnsupported,
} mulPowerStatus_t;

// Small transportable metadata. delta = corrected RX - raw RX, local DTU.
// Table 1/2: Bitcraze BIAS_500_16 / BIAS_500_64. Table 0: unavailable.
// powerCentiDbm == INT16_MIN means no finite estimate. Missing metadata is zero.
typedef struct {
  int16_t powerCentiDbm;
  int8_t deltaDtu;
  uint8_t status;
  uint8_t table;
} mulPower_t;

typedef struct {
  uint64_t raw;
  uint64_t corrected;
  mulPower_t power;
} mulPowerRx_t;

// Call once with the hardware RX_STAMP before RX re-arm; raw is immutable input.
// No antenna adjustment here: LDE_RXANTD is already included in RX_STAMP.
mulPowerRx_t mulPowerCapture(dwDevice_t *dev, uint64_t raw);
#endif
