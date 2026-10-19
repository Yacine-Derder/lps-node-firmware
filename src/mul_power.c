/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_power.h"
#include <math.h>
#include <limits.h>

mulPowerRx_t mulPowerCapture(dwDevice_t *dev, uint64_t raw)
{
  mulPowerRx_t result = {.raw=mulTimestampMask(raw), .corrected=mulTimestampMask(raw),
    .power={.powerCentiDbm=INT16_MIN, .status=mulPowerUnsupported}};
  // Current LPS PHY: 500 MHz bandwidth. Do not enter the vendor's 900 MHz
  // signed-left-shift branches, or let unknown PRF/channel select a default table.
  if ((dev->channel != CHANNEL_1 && dev->channel != CHANNEL_2 &&
       dev->channel != CHANNEL_3 && dev->channel != CHANNEL_5) ||
      (dev->pulseFrequency != TX_PULSE_FREQ_16MHZ && dev->pulseFrequency != TX_PULSE_FREQ_64MHZ)) return result;
  result.power.table = dev->pulseFrequency == TX_PULSE_FREQ_16MHZ ? 1 : 2;
  float power = dwGetReceivePower(dev);
  result.power.status = mulPowerInvalid;
  if (!isfinite(power)) return result;
  if (power >= -327.67f && power <= 327.67f) result.power.powerCentiDbm = (int16_t)lroundf(power*100.0f);
  result.power.status = mulPowerOutOfRange;
  if (power < -95.0f || power > -61.0f) return result; // No extrapolation/clamping outside calibration.
  dwTime_t corrected = {.full=result.raw};
  // Uses the same frozen RX diagnostics as the first read (auto re-enable off).
  // Reuse vendor tables, interpolation, sign, conversion and truncation exactly.
  dwCorrectTimestamp(dev, &corrected);
  int64_t delta = mulTimestampDifference(corrected.full, result.raw);
  result.power.status = mulPowerInvalid;
  if (delta < -64 || delta > 64) return result;
  result.corrected = mulTimestampMask(corrected.full);
  result.power.deltaDtu = (int8_t)delta;
  result.power.status = mulPowerApplied;
  return result;
}
