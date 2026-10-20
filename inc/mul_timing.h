/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_TIMING_H
#define MUL_TIMING_H

#include <stdint.h>

// Build all three nodes with the same slot. No EEPROM or wire-format change.
#ifndef MUL_FOLLOWER_SLOT_MS
#define MUL_FOLLOWER_SLOT_MS 500
#endif
#if MUL_FOLLOWER_SLOT_MS != 500 && MUL_FOLLOWER_SLOT_MS != 50 && MUL_FOLLOWER_SLOT_MS != 10
#error "MUL_FOLLOWER_SLOT_MS must be 500, 50 or 10"
#endif
#define MUL_FOLLOWER_SLOT_DTU (MUL_FOLLOWER_SLOT_MS * UINT64_C(63897600))

#endif
