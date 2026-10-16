/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_alr.h"

#include <stdio.h>

static void mulAlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  (void)config;
  dwIdle(dev);
  printf("MUL ALR: Mode initialized\r\n");
}

static uint32_t mulAlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  (void)dev;
  (void)event;
  // Block the UWB task on its IRQ semaphore while this mode remains idle.
  return MAX_TIMEOUT;
}

uwbAlgorithm_t mulAlrAlgorithm = {
  .init = mulAlrInit,
  .onEvent = mulAlrOnEvent,
};
