/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_role_state.h"
typedef char mulScratchMustFit128[(sizeof(mulRoleScratch_t)<=128) ? 1 : -1];
mulRoleScratch_t mulRoleScratch;

mulRoleIo_t mulRoleIo;
