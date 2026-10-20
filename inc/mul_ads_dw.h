/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_ADS_DW_H
#define MUL_ADS_DW_H
#include "../portable/mul_core/mul_diag_config.h"
#include "mul_ads.h"
#include "uwb.h"
#ifndef MUL_ADS_DW_ANT_DTU
#define MUL_ADS_DW_ANT_DTU 16384u /* libdw1000 nominal default, NOT calibrated */
#endif
#if MUL_ADS_DW_ANT_DTU > 65535
#error "DW antenna delay must fit the hardware register"
#endif
typedef struct { mulAds_t core; uint32_t lastStart,lastReport,errors,invalid,timeouts; bool ready; } mulAdsDwState_t;
extern uwbAlgorithm_t mulAdsAlgorithm;
#endif
