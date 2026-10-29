/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_ANTENNA_H
#define MUL_ANTENNA_H
#include <stdint.h>
// Legacy aggregate, initial44/56 split, not a measured per-device calibration.
#define MUL_ANTENNA_TX_DTU 14498
#define MUL_ANTENNA_RX_DTU 18453
#define MUL_ANTENNA_SOURCE "BITCRAZE_LPS_INITIAL"
void mulAntennaSelect(uint16_t tx,uint16_t rx);
void mulAntennaGet(uint16_t *tx,uint16_t *rx);
#endif
