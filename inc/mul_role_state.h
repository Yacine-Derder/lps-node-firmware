/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_ROLE_STATE_H
#define MUL_ROLE_STATE_H
#include "mul_twr.h"
#include "mul_relation.h"
// A node runs exactly one persisted mode until reboot. Mode 5 uses TWR state;
// mode 6 is passive and reuses that RAM for received relations and RX rate state.
typedef union {
  mulTwr_t twr;
  struct { mulRelation_t relation; mulClock_t rate; } rlr;
} mulRoleScratch_t;
extern mulRoleScratch_t mulRoleScratch;
#endif
