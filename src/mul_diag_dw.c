/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_diag_dw.h"
#if MUL_DIAG_ROLE
#include "mul_role_state.h"
#include "task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define state mulRoleScratch.diag
_Static_assert(sizeof(mulDiagDwState_t)<=128,"diagnostic must fit existing scratch");
static const uint8_t identity[8]={0x84,0,0,0,0,0,0xcf,0xbc};
static uint32_t milliseconds(void) { return (uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS; }
static void rearm(dwDevice_t *dev) { dwNewReceive(dev); dwSetDefaults(dev); dwStartReceive(dev); }
static void init(uwbConfig_t *config,dwDevice_t *dev) {
  (void)config; dwIdle(dev); memset(&state,0,sizeof(state));
  state.lastTxMs=state.lastReportMs=milliseconds();
  printf("MULG INIT radio=DW1000 role=%s src=84 core=1 type=70 version=1 bytes=44 bits=40 unit=DTU ch=5\r\n",MUL_DIAG_ROLE==1 ? "TX" : "RX");
  if (MUL_DIAG_ROLE==2) rearm(dev);
}
static uint32_t event(dwDevice_t *dev,uwbEvent_t event) {
  uint32_t now=milliseconds();
  if (MUL_DIAG_ROLE==1) {
    if (event==eventPacketSent && state.pending) {
      dwTime_t stamp={.full=0}; dwGetTransmitTimestamp(dev,&stamp);
      state.lastTxMarker=mulTimestampMask(stamp.full); state.lastMarkerSequence=state.sequence-1;
      state.markerValid=true; state.done++; state.pending=false; dwIdle(dev);
      printf("MULG DONE src=84 seq=%" PRIu32 " done=%" PRIu32 " tx=%02X%08" PRIX32 " bits=40 unit=DTU\r\n",
        state.lastMarkerSequence,state.done,(unsigned)(state.lastTxMarker>>32),(uint32_t)state.lastTxMarker);
    }
    if (state.pending && (uint32_t)(now-state.lastTxMs)>=100) { state.errors++; state.pending=false; dwIdle(dev); }
    if (!state.pending && (uint32_t)(now-state.lastTxMs)>=1000) {
      mulCoreDiag_t message={.sequence=state.sequence++,.markerSequence=state.lastMarkerSequence,
        .txMarker=state.lastTxMarker,.markerValid=state.markerValid,.radio=MUL_CORE_DW1000};
      memcpy(message.source,identity,8);
      if (mulCoreEncodeDiag(state.frame,sizeof(state.frame),&message)!=mulCoreOk) { state.errors++; return 50; }
      dwNewTransmit(dev); dwSetDefaults(dev); dwWaitForResponse(dev,false);
      dwSetData(dev,state.frame,MUL_CORE_DIAG_LENGTH); state.pending=true; state.attempts++; state.lastTxMs=now;
      dwStartTransmit(dev);
      printf("MULG TX src=84 seq=%" PRIu32 " attempt=%" PRIu32 "\r\n",message.sequence,state.attempts);
    }
  } else {
    if (event==eventPacketReceived) {
      dwTime_t stamp={.full=0}; mulCoreDiag_t message; bool valid=false;
      unsigned int length=dwGetDataLength(dev);
      if (length==MUL_CORE_DIAG_LENGTH) {
        dwGetData(dev,state.frame,length); dwGetRawReceiveTimestamp(dev,&stamp);
        valid=mulCoreDecodeDiag(state.frame,length,&message)==mulCoreOk && message.source[0]==0x86 && message.radio==MUL_CORE_DW3000;
      }
      rearm(dev);
      if (valid) {
        /* v1 has no boot ID. After >=5s without a decoded expected peer,
         * begin an explicitly logged ordering epoch, preserving totals. This
         * does not prove a reboot or count loss across the idle boundary. */
        if (state.rx.baseline && (uint32_t)(now-state.lastRxMs)>=5000U) {
          printf("MULG EPOCH reason=idle previous=%" PRIu32 " next=%" PRIu32 " idle_ms=%" PRIu32 "\r\n",state.rx.last,message.sequence,now-state.lastRxMs);
          mulCoreSequenceBeginEpoch(&state.rx);
        }
        state.lastRxMs=now;
        mulCoreOrder_t order=mulCoreObserveSequence(&state.rx,message.sequence);
        printf("MULG RX src=86 seq=%" PRIu32 " valid=1 order=%u rx=%02X%08" PRIX32 " prev_valid=%u prev_seq=%" PRIu32 " prev_tx=%02X%08" PRIX32 " gaps=%" PRIu32 " bits=40 unit=DTU\r\n",
          message.sequence,(unsigned)order,(unsigned)(stamp.full>>32),(uint32_t)stamp.full,message.markerValid,message.markerSequence,
          (unsigned)(message.txMarker>>32),(uint32_t)message.txMarker,state.rx.gaps);
      } else state.invalid++;
    } else if (event==eventReceiveTimeout) { state.timeouts++; rearm(dev); }
    else if (event==eventReceiveFailed) { state.errors++; rearm(dev); }
  }
  if ((uint32_t)(now-state.lastReportMs)>=1000) {
    state.lastReportMs=now;
    printf("MULG STAT tx=%" PRIu32 " done=%" PRIu32 " valid=%" PRIu32 " gaps=%" PRIu32 " dup=%" PRIu32 " old=%" PRIu32 " invalid=%" PRIu32 " err=%" PRIu32 " to=%" PRIu32 "\r\n",
      state.attempts,state.done,state.rx.valid,state.rx.gaps,state.rx.duplicates,state.rx.old,state.invalid,state.errors,state.timeouts);
  }
  return 50;
}
uwbAlgorithm_t mulDiagAlgorithm={.init=init,.onEvent=event};
#endif
