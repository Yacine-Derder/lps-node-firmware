/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_DIAG_DW_H
#define MUL_DIAG_DW_H
#include "../portable/mul_core/mul_diag_config.h"
#include "mul_core.h"
#include "uwb.h"
/* Runtime ownership/timers are adapter details; core has no hardware state. */
typedef struct {
  mulCoreSequence_t rx;
  uint32_t sequence,attempts,done,invalid,errors,timeouts,lastTxMs,lastReportMs;
  uint8_t frame[MUL_CORE_DIAG_LENGTH+2];
  bool pending,markerValid;
  uint64_t lastTxMarker;
  uint32_t lastMarkerSequence;
} mulDiagDwState_t;
extern uwbAlgorithm_t mulDiagAlgorithm;
#endif
