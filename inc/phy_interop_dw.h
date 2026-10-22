/* SPDX-License-Identifier: MIT */
#ifndef PHY_INTEROP_DW_H
#define PHY_INTEROP_DW_H
#include "../portable/mul_core/mul_diag_config.h"
#ifndef MUL_HYBRID_CH5
#define MUL_HYBRID_CH5 0
#endif
#if (MUL_HYBRID_CH5 != 0 && MUL_HYBRID_CH5 != 1) || (MUL_HYBRID_CH5 && (PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE))
#error "MUL_HYBRID_CH5 is 0/1 and cannot be combined with diagnostic role overrides"
#endif
#include "phy_diag.h"
#include "uwb.h"
void phyInteropConfigure(dwDevice_t *dev);
extern uwbAlgorithm_t phyInteropAlgorithm;
#endif
