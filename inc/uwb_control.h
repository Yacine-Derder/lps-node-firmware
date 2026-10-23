/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef UWB_CONTROL_H
#define UWB_CONTROL_H
#include "uwb.h"
/* Requests carry no SPI operations. Only the existing radio owner applies them. */
void uwbControlInit(const uwbConfig_t *config);
bool uwbControlCanEnable(void);
bool uwbControlRequestSlot(uint16_t slotMs);
bool uwbControlRequest(bool enabled);
bool uwbControlRequested(void);
bool uwbControlRunning(void);
bool uwbControlApply(dwDevice_t *dev,uwbAlgorithm_t *algorithm);
bool uwbControlEvent(uwbEvent_t event);
uint32_t uwbControlCount(unsigned event);
bool uwbControlResetCounters(void);
#endif
