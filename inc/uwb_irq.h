/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef UWB_IRQ_H
#define UWB_IRQ_H
#include "libdw1000.h"
/* A binary semaphore is a wakeup hint, not a count of hardware events. Keep
 * the actual task dispatch here so host tests can exercise level/drain races
 * through libdw1000. Caller has already applied RF control in the UWB owner. */
static inline bool uwbServiceIrq(dwDevice_t *dev,bool notified,
                               bool (*asserted)(void),bool (*requested)(void)) {
 if(!requested()||(!notified&&!asserted()))return false;
 do {dwHandleInterrupt(dev);} while(requested()&&asserted());
 return true;
}
#endif
