/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_DIAG_CONFIG_H
#define MUL_DIAG_CONFIG_H
#ifndef MUL_DIAG_ROLE
#define MUL_DIAG_ROLE 0
#endif
#ifndef PHY_INTEROP_ROLE
#define PHY_INTEROP_ROLE 0
#endif
#ifndef MUL_TIMING_TRACE
#define MUL_TIMING_TRACE 0
#endif
#if MUL_DIAG_ROLE < 0 || MUL_DIAG_ROLE > 2
#error "MUL_DIAG_ROLE must be 0 (off), 1 (TX), 2 (RX)"
#endif
#if MUL_DIAG_ROLE && PHY_INTEROP_ROLE
#error "Stage 3F and Stage 3G diagnostics are mutually exclusive"
#endif
#if MUL_TIMING_TRACE && !MUL_DIAG_ROLE
#error "MUL_TIMING_TRACE requires the opt-in Stage 3G diagnostic"
#endif
#endif
