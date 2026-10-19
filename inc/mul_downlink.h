/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_DOWNLINK_H
#define MUL_DOWNLINK_H
#include "mul_relation.h"
#include "mul_observation.h"
// Current-frame relation only. captureAgeMs is hardware RX-to-callback age,
// rounded outward. No cached relation may be substituted for unavailable metadata.
bool mulDownlinkRelation(const mulRelation_t *relation,mulPair_t *pair,
  uint32_t nowMs,uint32_t captureAgeMs,int64_t *txSeparationReferenceDtu);
#endif
