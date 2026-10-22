/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_RX_H
#define MUL_RX_H

#include "uwb.h"
#include "mul_common.h"
#include "mul_relation.h"

// Initial LPS estimate: round(154.6 m * 63897600000 / 299792458).
// Bitcraze TWR's aggregate, allocated TX/RX 44%/56% per APS014 table 4.
// Not per-device calibration; see docs/mul_architecture.md.
#define MUL_ANTENNA_TX_DTU 14498
#define MUL_ANTENNA_RX_DTU 18453
#define MUL_ANTENNA_SOURCE "BITCRAZE_LPS_INITIAL"
void mulAntennaConfigure(dwDevice_t *dev);
bool mulAntennaValid(dwDevice_t *dev);

// Concrete DW1000 RX operations shared by the passive follower and RLR.
void mulRxInit(dwDevice_t *dev);
void mulRxRearm(dwDevice_t *dev);
bool mulRxReadBroadcast(dwDevice_t *dev,mulTestPacket_t *packet,uint64_t *localRx,mulRelation_t *relation);
bool mulRxReadBroadcastNetwork(const mulNetwork_t *network,dwDevice_t *dev,mulTestPacket_t *packet,uint64_t *localRx,mulRelation_t *relation);
bool mulRxRead(dwDevice_t *dev, mulTestPacket_t *packet, uint64_t *localRx);

#endif // MUL_RX_H
