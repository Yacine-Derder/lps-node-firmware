/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_PLAN_PROTOCOL_H
#define MUL_PLAN_PROTOCOL_H
#include "mul_ranging_plan.h"
#include "mul_multi_protocol.h"
// RF5 uses semantic plan fingerprints; live RF4 peers fail closed.
#define MUL_PLAN_RADIO_VERSION 5u
size_t mulPlanBroadcastEncode(uint8_t *out,size_t capacity,const mulRangingPlan_t *plan,unsigned slot,uint32_t cycle,uint64_t tx,const mulRelation_t *relation);
bool mulPlanBroadcastDecode(const mulRangingPlan_t *plan,const uint8_t *frame,size_t length,mulMultiBroadcast_t *out);
bool mulPlanTwrEncode(uint8_t out[MUL_MULTI_TWR_BYTES],const mulRangingPlan_t *plan,unsigned operation,const mulTwrPacket_t *packet);
bool mulPlanTwrDecode(const mulRangingPlan_t *plan,uint32_t cycle,unsigned operation,const uint8_t *frame,size_t length,mulTwrPacket_t *out);
/* Prepared outside packet turnaround. Owns the validated pair/cycle/index/hash
 * snapshot; rebind on every operation/configuration change. The full packet,
 * power, prefix and radio-deadline checks remain the caller's responsibility. */
#define MUL_PLAN_TWR_BINDING 1u
typedef struct {
 mulNetwork_t network;
 uint32_t fingerprint,cycle;
 uint8_t operation;
} mulPlanTwrBinding_t;
bool mulPlanTwrBind(mulPlanTwrBinding_t *binding,const mulRangingPlan_t *plan,uint32_t cycle,unsigned operation);
bool mulPlanTwrEncodeBound(uint8_t out[MUL_MULTI_TWR_BYTES],const mulPlanTwrBinding_t *binding,const mulTwrPacket_t *packet);
bool mulPlanTwrDecodeBound(const mulPlanTwrBinding_t *binding,const uint8_t *frame,size_t length,mulTwrPacket_t *out);
bool mulPlanOperationNetwork(const mulRangingPlan_t *plan,uint32_t cycle,unsigned operation,mulNetwork_t *network);
#endif
