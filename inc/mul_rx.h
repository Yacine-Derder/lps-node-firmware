/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_RX_H
#define MUL_RX_H

#include "uwb.h"
#include "mul_common.h"

// Concrete DW1000 RX operations shared by the passive follower and RLR.
void mulRxInit(dwDevice_t *dev);
void mulRxRearm(dwDevice_t *dev);
bool mulRxRead(dwDevice_t *dev, mulTestPacket_t *packet, uint64_t *localRx);

#endif // MUL_RX_H
