/* SPDX-License-Identifier: MIT */
#include "phy_interop_dw.h"
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE
#include "mul_role_state.h"
#include "task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
void phyInteropConfigure(dwDevice_t *dev) {
  dwEnableMode(dev,MODE_SHORTDATA_FAST_ACCURACY);
  dwSetChannel(dev,CHANNEL_5); dwSetPreambleCode(dev,PREAMBLE_CODE_64MHZ_9);
  dwUseExtendedFrameLength(dev,false); dwSuppressFrameCheck(dev,false);
  dwUseSmartPower(dev,false); dev->forceTxPower=false;
  dwSetFrameFilter(dev,false); dwSetReceiverAutoReenable(dev,false);
  dwReceivePermanently(dev,false); dwWaitForResponse(dev,false);
  dwSetReceiveWaitTimeout(dev,50000); dwInterruptOnReceiveTimeout(dev,true);
  dwInterruptOnReceiveFailed(dev,true);
}
#if PHY_INTEROP_ROLE
#define state mulRoleScratch.interop
static uint32_t milliseconds(void) { return (uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS; }
static void rearm(dwDevice_t *dev) { dwNewReceive(dev); dwSetDefaults(dev); dwStartReceive(dev); }
static void init(uwbConfig_t *config,dwDevice_t *dev) {
  (void)config; dwIdle(dev); memset(&state,0,sizeof(state));
  state.lastTxMs=state.lastReportMs=milliseconds();
  printf("PHY INIT radio=DW1000 role=%s src=84 ch=5 prf=64 pre=128 code=9 sfd=4A rate=6800 phr=STD sts=OFF bytes=34\r\n",PHY_INTEROP_ROLE==1 ? "TX" : "RX");
  if (PHY_INTEROP_ROLE==2) rearm(dev);
}
static uint32_t event(dwDevice_t *dev,uwbEvent_t event) {
  uint32_t now=milliseconds();
  if (PHY_INTEROP_ROLE==1) {
    if (event==eventPacketSent && state.pending) {
      state.done++; state.pending=false; dwIdle(dev);
      printf("PHY DONE src=84 seq=%" PRIu32 " done=%" PRIu32 "\r\n",state.sequence-1,state.done);
    }
    if (state.pending && (uint32_t)(now-state.lastTxMs)>=100) { state.errors++; state.pending=false; dwIdle(dev); }
    if (!state.pending && (uint32_t)(now-state.lastTxMs)>=1000) {
      phyDiagEncode(state.frame,PHY_DIAG_DW1000,state.sequence++);
      dwNewTransmit(dev); dwSetDefaults(dev); dwWaitForResponse(dev,false);
      dwSetData(dev,state.frame,PHY_DIAG_LENGTH); state.pending=true; state.attempts++; state.lastTxMs=now;
      dwStartTransmit(dev);
      printf("PHY TX src=84 seq=%" PRIu32 " attempt=%" PRIu32 "\r\n",state.sequence-1,state.attempts);
    }
  } else {
    if (event==eventPacketReceived) {
      uint8_t source; uint32_t sequence; dwTime_t stamp={.full=0};
      unsigned int length=dwGetDataLength(dev);
      bool valid=false;
      if (length==PHY_DIAG_LENGTH) {
        dwGetData(dev,state.frame,length); dwGetRawReceiveTimestamp(dev,&stamp);
        valid=phyDiagDecode(state.frame,length,&source,&sequence) && source==PHY_DIAG_DWM3000;
      }
      rearm(dev); // Freeze frame/timestamp first, rearm before printing.
      if (valid) {
        phyDiagObserve(&state.rx,sequence);
        printf("PHY RX src=%02X seq=%" PRIu32 " valid=1 rx=%02X%08" PRIX32 " gaps=%" PRIu32 "\r\n",
          source,sequence,(unsigned int)(stamp.full>>32)&255,(uint32_t)stamp.full,state.rx.gaps);
      } else state.invalid++;
    } else if (event==eventReceiveTimeout) { state.timeouts++; rearm(dev); }
    else if (event==eventReceiveFailed) { state.errors++; rearm(dev); }
  }
  if ((uint32_t)(now-state.lastReportMs)>=1000) {
    state.lastReportMs=now;
    printf("PHY STAT tx=%" PRIu32 " done=%" PRIu32 " valid=%" PRIu32 " gaps=%" PRIu32 " dup=%" PRIu32 " old=%" PRIu32 " invalid=%" PRIu32 " err=%" PRIu32 " to=%" PRIu32 "\r\n",
      state.attempts,state.done,state.rx.valid,state.rx.gaps,state.rx.duplicates,state.rx.old,state.invalid,state.errors,state.timeouts);
  }
  return 50; // Bounded service/watchdog wake; not a precise radio timestamp.
}
uwbAlgorithm_t phyInteropAlgorithm={.init=init,.onEvent=event};
#endif
#endif
