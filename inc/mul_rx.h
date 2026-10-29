/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_RX_H
#define MUL_RX_H

#include "uwb.h"
#include "mul_common.h"
#include "mul_relation.h"

#include "mul_antenna.h"
void mulAntennaConfigure(dwDevice_t *dev);
bool mulAntennaValid(dwDevice_t *dev);

// Optional local observations only; never rearm, clear status or schedule a wakeup.
#ifndef MUL_RX_DIAG
#define MUL_RX_DIAG 0
#endif
#if MUL_RX_DIAG != 0 && MUL_RX_DIAG != 1
#error MUL_RX_DIAG must be 0 or 1
#endif
#if MUL_RX_DIAG
#include "mul_ranging_plan.h"
void mulRxDiagEvent(dwDevice_t *dev,uwbEvent_t event);
void mulRxDiagService(dwDevice_t *dev,const mulRangingPlan_t *plan,uint32_t cycle,uint8_t local,uint64_t origin);
void mulRxDiagBeacon(uint64_t stamp);
void mulRxDiagDecision(unsigned event,unsigned stage);
void mulRxDiagPoll(bool (*write)(const char *,unsigned));
#else
#define mulRxDiagEvent(...) ((void)0)
#define mulRxDiagService(...) ((void)0)
#define mulRxDiagBeacon(...) ((void)0)
#define mulRxDiagDecision(...) ((void)0)
#define mulRxDiagPoll(...) ((void)0)
#endif

// Concrete DW1000 RX operations shared by the passive follower and RLR.
void mulRxInit(dwDevice_t *dev);
void mulRxRearm(dwDevice_t *dev);
bool mulRxReadBroadcast(dwDevice_t *dev,mulTestPacket_t *packet,uint64_t *localRx,mulRelation_t *relation);
bool mulRxReadBroadcastNetwork(const mulNetwork_t *network,dwDevice_t *dev,mulTestPacket_t *packet,uint64_t *localRx,mulRelation_t *relation);
bool mulRxRead(dwDevice_t *dev, mulTestPacket_t *packet, uint64_t *localRx);

#endif // MUL_RX_H
