#include "mul_clock_workspace.h"
#if defined(STM32F072xB) && defined(__arm__)
extern mulClockWorkspace_t *uwbTdoa3ClockWorkspace(void);
mulClockWorkspace_t *mulClockWorkspace(void){return uwbTdoa3ClockWorkspace();}
#else
/* Native radio tests have no stock firmware image/storage to overlay. */
static mulClockWorkspace_t hostWorkspace;
mulClockWorkspace_t *mulClockWorkspace(void){return &hostWorkspace;}
#endif
