/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_clock_workspace.h"
/* Owned by the boot-selected MUL radio owner. Never aliases upstream state. */
static mulClockWorkspace_t workspace;
mulClockWorkspace_t *mulClockWorkspace(void){return &workspace;}
