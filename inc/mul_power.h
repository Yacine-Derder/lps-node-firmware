/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_POWER_H
#define MUL_POWER_H
#include "mul_common.h"
#include "libdw1000.h"

#include "../portable/mul_core/mul_power_types.h"

// Call once with the hardware RX_STAMP before RX re-arm; raw is immutable input.
// No antenna adjustment here: LDE_RXANTD is already included in RX_STAMP.
mulPowerRx_t mulPowerCapture(dwDevice_t *dev, uint64_t raw);
#endif
