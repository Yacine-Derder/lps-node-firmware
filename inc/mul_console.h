/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_CONSOLE_H
#define MUL_CONSOLE_H
#include <stdbool.h>
/* Additive ':' line entry leaves the original immediate character menu intact.
 * Fragmented/oversized lines are consumed, never replayed as legacy commands. */
bool mulConsoleByte(char ch);
#endif
