/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_CLOCK_WORKSPACE_H
#define MUL_CLOCK_WORKSPACE_H
#include "mul_clock_fit.h"
typedef struct {mulClockFit_t fit;mulClockFitConfig_t config;} mulClockWorkspace_t;
/* Only the boot-selected MUL radio owner may use this workspace. Embedded
 * storage overlays inactive stock TDoA3 state; no task/queue buffer is reduced. */
mulClockWorkspace_t *mulClockWorkspace(void);
#endif
