/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_ALR_H
#define MUL_ALR_H

#include "uwb.h"

#ifndef MUL_SLOT_TRACE
#define MUL_SLOT_TRACE 0
#endif
#if MUL_SLOT_TRACE != 0 && MUL_SLOT_TRACE != 1
#error MUL_SLOT_TRACE must be 0 or 1
#endif
#if MUL_SLOT_TRACE
void mulAlrSlotPoll(bool (*write)(const char *,unsigned));
void mulAlrTiming(unsigned event,uint32_t cycle,unsigned initiator,unsigned responder,unsigned why,
 uint64_t a,uint64_t b,uint64_t c,uint64_t d);
#else
#define mulAlrTiming(...) ((void)0)
#endif

extern uwbAlgorithm_t mulAlrAlgorithm;

#endif // MUL_ALR_H
