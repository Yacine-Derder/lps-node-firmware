/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_antenna.h"
static uint16_t transmit=MUL_ANTENNA_TX_DTU,receive=MUL_ANTENNA_RX_DTU;
void mulAntennaSelect(uint16_t tx,uint16_t rx){transmit=tx;receive=rx;}
void mulAntennaGet(uint16_t *tx,uint16_t *rx){*tx=transmit;*rx=receive;}
