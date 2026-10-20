/* SPDX-License-Identifier: MIT */
#ifndef PHY_INTEROP_DW_H
#define PHY_INTEROP_DW_H
#include "../portable/mul_core/mul_diag_config.h"
#include "phy_diag.h"
#include "uwb.h"
void phyInteropConfigure(dwDevice_t *dev);
extern uwbAlgorithm_t phyInteropAlgorithm;
#endif
