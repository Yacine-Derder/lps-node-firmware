/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "uwb_control.h"
#include "phy_interop_dw.h"
#include "mul_diag_dw.h"
#include "mul_ads_dw.h"
static volatile bool requested,running;
static bool allowed;
static uint32_t events[5];
void uwbControlInit(const uwbConfig_t *c) {
 uint8_t role=2,ref,fol;bool enabled=false;
 bool network=cfgMulRead(c->address[0],&role,&ref,&fol);
 uint8_t mode=255,node=0;
 bool identity=cfgReadU8(cfgMode,&mode)&&mode==c->mode&&cfgReadU8(cfgAddress,&node)&&node==c->address[0];
 allowed=cfgStorageReady()&&identity&&network&&c->mode<=MODE_MUL_RLR;
 if(c->mode>=MODE_MUL_ALR)allowed=allowed&&network&&
   (c->mode==MODE_MUL_ALR?role<2: c->address[0]!=ref&&c->address[0]!=fol);
 if(role==3&&c->mode!=MODE_MUL_RLR)allowed=false;
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
 allowed=cfgStorageReady()&&identity&&network&&role!=3;
#endif
 requested=allowed&&cfgRfEnabled(&enabled)&&enabled;running=false;
}
bool uwbControlCanEnable(void) {return allowed&&!cfgPendingRestart();}
bool uwbControlRequest(bool enabled) {
 if(enabled&&!uwbControlCanEnable())return false;
 requested=enabled;return true;
}
bool uwbControlRequested(void) {return requested;}
bool uwbControlRunning(void) {return running;}
bool uwbControlApply(dwDevice_t *dev,uwbAlgorithm_t *algorithm) {
 if(requested==running)return false;
 dwIdle(dev);dwClearAllStatus(dev);running=false;
 if(requested){algorithm->init(uwbGetConfig(),dev);running=true;}
 return true;
}
bool uwbControlEvent(uwbEvent_t event) {
 if(!running||!requested)return false;
 if((unsigned)event<5&&events[event]!=UINT32_MAX)events[event]++;
 return true;
}
uint32_t uwbControlCount(unsigned event) {return event<5?events[event]:0;}
bool uwbControlResetCounters(void) {
 if(requested||running)return false;
 for(unsigned i=0;i<5;i++)events[i]=0;
 return true;
}
