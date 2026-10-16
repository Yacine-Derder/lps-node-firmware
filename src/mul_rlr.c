/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_rlr.h"

#include <stdio.h>

static void mulRlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  (void)config;
  dwIdle(dev);
  printf("MUL RLR: Mode initialized\r\n");
}

static uint32_t mulRlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  (void)dev;
  (void)event;
  // Block the UWB task on its IRQ semaphore while this mode remains idle.
  return MAX_TIMEOUT;
}

uwbAlgorithm_t mulRlrAlgorithm = {
  .init = mulRlrInit,
  .onEvent = mulRlrOnEvent,
};
