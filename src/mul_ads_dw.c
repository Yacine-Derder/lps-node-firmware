/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_ads_dw.h"
#if MUL_TWR_ROLE
#include "mul_role_state.h"
#include "task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define state mulRoleScratch.ads
_Static_assert(sizeof(mulAdsDwState_t)<=128,"ADS must fit existing scratch");
static uint32_t milliseconds(void) { return (uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS; }
static void logAbort(void) {
  printf("MULH ABORT session=%08" PRIX32 "%08" PRIX32 " id=%" PRIu32 " cause=%u stage=%u by_stage=%u,%u,%u,%u,%u,%u,%u" "\r\n",
    (uint32_t)(state.core.session>>32),(uint32_t)state.core.session,state.core.transaction,(unsigned)state.core.lastAbortReason,(unsigned)state.core.lastAbortStage,(unsigned)state.core.failedStages[0],(unsigned)state.core.failedStages[1],(unsigned)state.core.failedStages[2],(unsigned)state.core.failedStages[3],(unsigned)state.core.failedStages[4],(unsigned)state.core.failedStages[5],(unsigned)state.core.failedStages[6]);
}
static void logTimes(const mulAdsPacket_t *p) {
  printf("MULH TIMES session=%08" PRIX32 "%08" PRIX32 " id=%" PRIu32 " t1=%02X%08" PRIX32 " t2=%02X%08" PRIX32 " t3=%02X%08" PRIX32 " t4=%02X%08" PRIX32 " t5=%02X%08" PRIX32 " t6=%02X%08" PRIX32 "\r\n",
    (uint32_t)(state.core.session>>32),(uint32_t)state.core.session,state.core.transaction,(unsigned)(state.core.stamps[0]>>32),(uint32_t)state.core.stamps[0],(unsigned)(p->stamps[0]>>32),(uint32_t)p->stamps[0],(unsigned)(p->stamps[1]>>32),(uint32_t)p->stamps[1],(unsigned)(state.core.stamps[1]>>32),(uint32_t)state.core.stamps[1],(unsigned)(state.core.stamps[2]>>32),(uint32_t)state.core.stamps[2],(unsigned)(p->stamps[2]>>32),(uint32_t)p->stamps[2]);
}
static void rearm(dwDevice_t *d) { dwNewReceive(d); dwSetDefaults(d); dwStartReceive(d); }
static void abortExchange(dwDevice_t *d,mulAdsAbortReason_t cause) {
  uint32_t before=state.core.aborted;
  mulAdsAbortWithReason(&state.core,cause); rearm(d);
  if(state.core.aborted!=before) logAbort();
}
static void init(uwbConfig_t *config,dwDevice_t *d) {
#if defined(__arm__)
  // Only this optional diagnostic prints floating-point fields. Keep nano's
  // formatter here rather than forcing it into every MUL production image.
  extern void _printf_float(void);
  void (*volatile formatter)(void)=_printf_float;
  (void)formatter;
#endif
  (void)config; dwIdle(d); memset(&state,0,sizeof(state));
  if(dwSpiRead16(d,TX_ANTD,NO_SUB)!=MUL_ADS_DW_ANT_DTU ||
     dwSpiRead16(d,LDE_IF,LDE_RXANTD_SUB)!=MUL_ADS_DW_ANT_DTU) {
    printf("MULH INIT FAILED antenna readback\r\n"); return;
  }
  state.ready=true;
  mulAdsInit(&state.core,0x84,0x86,MUL_TWR_ROLE==1,MUL_TWR_SESSION ? MUL_TWR_SESSION : 1);
  state.lastStart=state.lastReport=milliseconds();
  printf("MULH INIT radio=DW1000 role=%u session=%08" PRIX32 "%08" PRIX32 " ant=%u ch=5 version=1 bytes=56 uncalibrated=1\r\n",MUL_TWR_ROLE,(uint32_t)(state.core.session>>32),(uint32_t)state.core.session,MUL_ADS_DW_ANT_DTU);
  rearm(d);
}
static uint32_t event(dwDevice_t *d,uwbEvent_t e) {
  if(!state.ready) return 50;
  uint32_t now=milliseconds(),completed=state.core.completed;
  if(mulAdsTick(&state.core,now)) { rearm(d); logAbort(); }
  if(e==eventPacketSent) {
    dwTime_t t={.full=0}; dwGetTransmitTimestamp(d,&t);
    mulAdsTxDone(&state.core,mulTimestampMask(t.full),now);
    // WAIT4RESP has already armed RX: do not clear a concurrently received frame.
    if(state.core.stage==adsIdle) rearm(d);
  } else if(e==eventPacketReceived) {
    uint8_t frame[MUL_ADS_LENGTH]; mulAdsPacket_t p; dwTime_t t={.full=0};
    uint32_t before=state.core.aborted; bool log=false; unsigned n=dwGetDataLength(d);
    if(n==sizeof(frame)) {
      dwGetData(d,frame,n); dwGetRawReceiveTimestamp(d,&t);
      if(!mulAdsDecode(frame,n,&p)) state.invalid++;
      else {
        log=MUL_TWR_ROLE==1 && state.core.stage==adsReportRx && !state.core.pending && p.type==mulAdsReport &&
          p.session==state.core.session && p.transaction==state.core.transaction && p.source==state.core.peer && p.destination==state.core.local;
        mulAdsReceive(&state.core,&p,mulTimestampMask(t.full),now);
      }
    } else state.invalid++;
    rearm(d);
    if(state.core.aborted!=before) logAbort();
    if(MUL_ADS_TRACE_TIMES && log) logTimes(&p);
  } else if(e==eventReceiveFailed || e==eventReceiveTimeout) {
    if(e==eventReceiveFailed) state.errors++; else state.timeouts++;
    abortExchange(d,e==eventReceiveFailed ? adsAbortRadio : adsAbortRxTimeout);
  }
  if(MUL_TWR_ROLE==1 && state.core.stage==adsIdle && (uint32_t)(now-state.lastStart)>=1000U) {
    state.lastStart=now; mulAdsBegin(&state.core,now);
  }
  mulAdsPacket_t p;
  if(mulAdsTakeTx(&state.core,now,&p)) {
    /* libdw1000 dwSetData copies length+FCS bytes with hardware CRC enabled. */
    uint8_t frame[MUL_ADS_LENGTH+2]={0};
    if(!mulAdsEncode(frame,sizeof(frame),&p)) abortExchange(d,adsAbortEncode);
    else {
      dwNewTransmit(d); dwSetDefaults(d); dwWaitForResponse(d,p.type!=mulAdsReport);
      dwSetData(d,frame,MUL_ADS_LENGTH); dwStartTransmit(d);
      // Missing TX completion is bounded by the core deadline; no assumed TX time.
    }
  }
  if(MUL_TWR_ROLE==1 && state.core.completed!=completed)
    printf("MULH RESULT session=%08" PRIX32 "%08" PRIX32 " id=%" PRIu32 " tof_dtu=%.6f ratio=%.9f nominal_m=%.4f uncalibrated=1 range_valid=%u\r\n",(uint32_t)(state.core.session>>32),(uint32_t)state.core.session,state.core.transaction,state.core.tof,state.core.rateRatio,state.core.tof*(299792458.0/63897600000.0),(unsigned)state.core.rangeValid);
  if((uint32_t)(now-state.lastReport)>=1000U) {
    state.lastReport=now;
    printf("MULH STAT attempt=%" PRIu32 " done=%" PRIu32 " abort=%" PRIu32 " reject=%" PRIu32 " invalid=%" PRIu32 " err=%" PRIu32 " to=%" PRIu32 " stage=%u session=%08" PRIX32 "%08" PRIX32 " bad_range=%" PRIu32 "\r\n",state.core.attempts,state.core.completed,state.core.aborted,state.core.rejected,state.invalid,state.errors,state.timeouts,state.core.stage,(uint32_t)(state.core.session>>32),(uint32_t)state.core.session,state.core.invalidRanges);
  }
  return 5; /* Nonblocking cooperative timeout/watchdog wake, not a radio timestamp. */
}
uwbAlgorithm_t mulAdsAlgorithm={.init=init,.onEvent=event};
#endif
