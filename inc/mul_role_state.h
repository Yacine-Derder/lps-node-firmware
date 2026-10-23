/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_ROLE_STATE_H
#define MUL_ROLE_STATE_H
#include "mul_twr.h"
#include "mul_multi_protocol.h"
#include "mul_ads_dw.h"
#include "mul_relation.h"
#include "mul_observation.h"
#include "phy_diag.h"
#include "mul_diag_dw.h"
// A node runs exactly one persisted mode until reboot. Mode 5 uses TWR state;
// mode 6 is passive and reuses that RAM for received relations and RX rate state.
typedef union {
  mulTwr_t twr;
  mulAdsDwState_t ads;
  mulDiagDwState_t diag;
  phyDiagState_t interop; // Experimental role shares existing 128-byte reservation.
  struct { mulRelation_t relation; mulClock_t rate; } rlr;
} mulRoleScratch_t;
extern mulRoleScratch_t mulRoleScratch;
/* Additional existing mode-exclusive IO: no stack/buffer size reduction. */
typedef union {
  struct {uint8_t frame[MUL_MULTI_FOLLOWER_BYTES+2];mulClock_t clock;} alr;
  mulObservation_t observations[MUL_ALR_COUNT];
} mulRoleIo_t;
extern mulRoleIo_t mulRoleIo;
#endif
