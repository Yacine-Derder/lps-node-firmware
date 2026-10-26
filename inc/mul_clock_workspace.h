/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_CLOCK_WORKSPACE_H
#define MUL_CLOCK_WORKSPACE_H
#include "mul_clock_fit.h"
typedef struct {mulClockFit_t fit;mulClockFitConfig_t config;} mulClockWorkspace_t;
/* Explicit MUL-owned storage; only the boot-selected radio owner may use it. */
mulClockWorkspace_t *mulClockWorkspace(void);
#endif
