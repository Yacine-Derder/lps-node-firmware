/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_twr.h"
#include "mul_rx.h"
#include "mul_multi_protocol.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "task.h"
#if MUL_TWR_TRACE
#include "usbd_cdc_if.h"
#endif

#include "../portable/mul_core/mul_legacy_twr.c"

/* One physical radio owner; configuration changes only during stopped init.
 * The portable codec/receiver keeps explicit context ownership. */
static const void *twrContext=&mulDefaultNetwork;
static struct {const mulRangingPlan_t *plan;mulRangingOperation_t op;uint32_t cycle;} plannedSession;
#if MUL_RX_DIAG
#define RX_DIAG(t,e,s) do { if((t)->planned&&plannedSession.op.kind==MUL_PLAN_AA)mulRxDiagDecision(e,s); } while(0)
#else
#define RX_DIAG(t,e,s) ((void)0)
#endif
#if MUL_TWR_TRACE
#define AA_TRACE_CAPACITY 16u
typedef char aaTraceRecordMustFit40[(sizeof(mulAaTrace_t)<=40)?1:-1];
static mulAaTrace_t aaTrace[AA_TRACE_CAPACITY];
static uint32_t aaDropped;
static uint8_t aaHead,aaTail,aaCount,aaLocal,aaInput,aaSending;
static uint64_t aaNow,aaEnd;
static bool aaNowValid,aaSelected;
static uint32_t aaSelectedCycle;static uint8_t aaSelectedOp,aaSelectedLocal;
static void trace(const mulTwr_t *twr,unsigned event,unsigned stage,unsigned why,uint64_t stamp) {
 if(!twr->planned||plannedSession.op.kind!=MUL_PLAN_AA)return;
 mulAaTrace_t r={.now=aaNow,.stamp=stamp,
  .deadline=event==mulAaWindow?aaEnd:(event==mulAaTarget||event==mulAaWaitEvent||(event==mulAaRx&&stage==mulTwrPoll))?0:twr->deadline,
  .cycle=twr->id,.operation=twr->operation,.initiator=plannedSession.op.initiator,
  .responder=plannedSession.op.responder,.local=aaLocal,.event=event,.stage=stage,
  .flags=(twr->txPending?1:0)|(aaNowValid?2:0)|((aaInput&7)<<2)|(twr->active?32:0),.why=why};
 taskENTER_CRITICAL();
 if(aaCount==AA_TRACE_CAPACITY)aaDropped++;
 else {aaTrace[aaHead]=r;aaHead=(aaHead+1)%AA_TRACE_CAPACITY;aaCount++;}
 taskEXIT_CRITICAL();
}
static unsigned traceReason(const char *reason) {
 static const char *const names[]={"timeout","radio_event","late","late_setup","delayed_start",
  "tx_stamp","dx_time","antenna_config","window_or_epoch","reservation_end","reservation_turnaround",
  "packet","exchange","response_interval","intervals","range","result","rx_stamp","measurement_age"};
 if(!reason)return 20; // Broadcast preemption, previously deferred.
 for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcmp(reason,names[i]))return i+1;
 return 21;
}
#define TRACE(t,e,s,w,v) do { RX_DIAG(t,e,s);trace(t,e,s,w,v); } while(0)
#else
#define TRACE(t,e,s,w,v) RX_DIAG(t,e,s)
#endif
bool mulTwrTraceTake(mulAaTrace_t *out,uint32_t *dropped) {
#if MUL_TWR_TRACE
 if(!out||!dropped)return false;
 taskENTER_CRITICAL();bool ready=aaCount!=0;
 if(ready){*out=aaTrace[aaTail];aaTail=(aaTail+1)%AA_TRACE_CAPACITY;aaCount--;}
 *dropped=aaDropped;taskEXIT_CRITICAL();return ready;
#else
 (void)out;(void)dropped;return false;
#endif
}
void mulTwrTracePoll(bool (*write)(const char *,unsigned)) {
#if MUL_TWR_TRACE
 // Only main calls this. Hold one popped record across USB backpressure.
 static mulAaTrace_t pending;static uint32_t lost;static bool ready;
 if(!ready)ready=mulTwrTraceTake(&pending,&lost);
 if(!ready||!write)return;
 char line[CDC_TRY_WRITE_MAX+1];
 int n=snprintf(line,sizeof(line),"AA1 c=%" PRIu32 " o=%u p=%02X>%02X l=%02X e=%u s=%02X f=%02X why=%u n=%02X%08" PRIX32 " t=%02X%08" PRIX32 " d=%02X%08" PRIX32 " lost=%" PRIu32 "\r\n",
  pending.cycle,pending.operation,pending.initiator,pending.responder,pending.local,pending.event,pending.stage,pending.flags,pending.why,
  (unsigned)(pending.now>>32),(uint32_t)pending.now,(unsigned)(pending.stamp>>32),(uint32_t)pending.stamp,
  (unsigned)(pending.deadline>>32),(uint32_t)pending.deadline,lost);
 if(n>0&&(unsigned)n<sizeof(line)&&write(line,(unsigned)n))ready=false;
#else
 (void)write;
#endif
}
bool mulTwrConfigurePlan(mulTwr_t *twr,const mulRangingPlan_t *plan,uint32_t cycle,unsigned index) {
 mulRangingOperation_t op;
 if(!twr||!mulRangingPlanOperation(plan,cycle,index,&op))return false;
 if(twr->planned&&twr->attempted&&cycle!=twr->id&&(uint32_t)(cycle-twr->id)>=UINT32_C(0x80000000))return false;
 if(twr->active&&(!twr->planned||twr->id!=cycle||twr->operation!=index))return false;
 if(twr->planned&&twr->attempted&&twr->id==cycle) {
  if(index<twr->operation||(index==twr->operation&&!twr->active))return false;
 }else twr->attempted=false;
 if(!twr->planned||twr->id!=cycle||twr->operation!=index)twr->attempted=false;
 twr->planned=true;twr->multiProfile=true;twr->id=cycle;twr->operation=(uint8_t)index;
 plannedSession.plan=plan;plannedSession.op=op;plannedSession.cycle=cycle;return true;
}
static bool aaOperation(const mulTwr_t *twr) {
 return !twr->planned||plannedSession.op.kind==MUL_PLAN_AA;
}
void mulTwrInvalidateAa(mulTwr_t *twr) {
 if(twr->planned)twr->syncDirty=true;
 twr->aa.invalidated=true;twr->aa.valid=false;
}
bool mulTwrDelayPowerCorrected(const mulTwr_t *twr) {return twr->planned?twr->aa.powerCorrected:twr->powerCorrected;}
void mulTwrConfigureNetwork(const mulNetwork_t *network) {twrContext=network?network:&mulDefaultNetwork;}
bool mulTwrConfigureSchedule(mulTwr_t *twr,const mulSchedule_t *schedule,unsigned slot,unsigned window) {
 if(!twr||mulScheduleValidate(schedule)||schedule->profile<MUL_PROFILE_MULTI||!slot||slot>=schedule->count||window>=MUL_SYNC_WINDOWS)return false;
 twrContext=schedule;twr->planned=0;twr->multiProfile=1;twr->syncWindow=window;twr->followerSlot=slot;return true;
}
static const mulNetwork_t *networkFor(const mulTwr_t *twr,mulNetwork_t *pair) {
 if(twr->planned){if(!mulPlanOperationNetwork(plannedSession.plan,twr->id,twr->operation,pair))memset(pair,0,sizeof(*pair));return pair;}
 if(!twr->multiProfile)return (const mulNetwork_t *)twrContext;
 if(!mulSchedulePair((const mulSchedule_t *)twrContext,twr->followerSlot,pair))memset(pair,0,sizeof(*pair));
 return pair;
}
#define twrNetwork networkFor(twr,&pairNetwork)
static unsigned pollMs(const mulTwr_t *twr){return twr->multiProfile?mulScheduleSyncPollMs(twr->syncWindow):MUL_TWR_POLL_MS;}
static unsigned endMs(const mulTwr_t *twr){return twr->multiProfile?mulScheduleSyncEndMs(twr->syncWindow):MUL_TWR_END_MS;}
static const mulSchedule_t *experimental(const mulTwr_t *twr) {
 if(twr->planned)return &plannedSession.plan->broadcast;
 const mulSchedule_t *s=twr->multiProfile?(const mulSchedule_t *)twrContext:NULL;
 return s&&s->profile==MUL_PROFILE_EXPERIMENTAL?s:NULL;
}
static uint64_t leadDtu(const mulTwr_t *twr) {
 const mulSchedule_t *s=experimental(twr);
 return s?((uint64_t)s->txLeadUs*MUL_TWR_DTU_PER_MS+999)/1000:5*MUL_TWR_DTU_PER_MS;
}
static unsigned replyMs(const mulTwr_t *twr){return twr->planned?plannedSession.plan->replyMs:mulScheduleReplyMs(experimental(twr));}
static unsigned tailMs(const mulTwr_t *twr) {
 const mulSchedule_t *s=experimental(twr);
 return s?(s->airtimeUs+s->rxBudgetUs+100u*s->guard100us+999)/1000:20;
}
static uint64_t tailDtu(const mulTwr_t *twr) {
 if(twr->planned){const mulSchedule_t *s=&plannedSession.plan->broadcast;return ((uint64_t)(s->airtimeUs+s->rxBudgetUs)*MUL_TWR_DTU_PER_MS+999)/1000;}
 return tailMs(twr)*MUL_TWR_DTU_PER_MS;
}
static unsigned stepMs(const mulTwr_t *twr){return experimental(twr)?replyMs(twr)+tailMs(twr):MUL_TWR_STEP_MS;}
static uint64_t microsDtu(uint32_t us){return ((uint64_t)us*MUL_TWR_DTU_PER_MS+999)/1000;}
static uint64_t pollOffset(const mulTwr_t *twr){return twr->planned?microsDtu(plannedSession.op.pollUs):pollMs(twr)*MUL_TWR_DTU_PER_MS;}
static uint64_t endOffset(const mulTwr_t *twr){return twr->planned?microsDtu(plannedSession.op.endUs):endMs(twr)*MUL_TWR_DTU_PER_MS;}
static bool due(const mulTwr_t *twr,uint32_t cycle) {
 if(twr->planned)return plannedSession.cycle==cycle;
 return twr->multiProfile?mulScheduleSyncOwner((const mulSchedule_t *)twrContext,cycle,twr->syncWindow)==twr->followerSlot:mulTwrDue(cycle);
}
static bool evaluate(mulTwr_t *twr)
{
  uint32_t raw,corrected;
  if (!mulTwrResultsReply(twr->stamps,twr->power,&raw,&corrected,replyMs(twr))) return false;
  twr->rawTof=(uint16_t)raw;
  twr->candidatePowerCorrected=corrected != UINT32_MAX;
  twr->candidateTof=(uint16_t)(twr->candidatePowerCorrected ? corrected : raw);
  return true;
}

static bool samePower(const mulPower_t *a, const mulPower_t *b)
{
  return a->powerCentiDbm==b->powerCentiDbm && a->deltaDtu==b->deltaDtu &&
         a->status==b->status && a->table==b->table;
}

static uint64_t hardwareNow(dwDevice_t *dev)
{
  dwTime_t now = {.full=0};
  dwGetSystemTimestamp(dev, &now);
#if MUL_TWR_TRACE
  aaNow=mulTimestampMask(now.full);aaNowValid=true;
#endif
  return mulTimestampMask(now.full);
}

static void clearErrors(dwDevice_t *dev)
{
  const uint8_t errors[] = {0x08,0x04};
  dwSpiWrite(dev, SYS_STATUS, 3, errors, sizeof(errors));
}

void mulTwrInit(mulTwr_t *twr) {
 memset(twr, 0, sizeof(*twr));mulTwrConfigureNetwork(NULL);
#if MUL_TWR_TRACE
 aaSelected=false;aaNowValid=false;aaInput=7;aaSending=0;
#endif
}

bool mulTwrDue(uint32_t sequence) { return sequence != 0 && sequence % 5 == 0; }

void mulTwrCancel(mulTwr_t *twr, dwDevice_t *dev, const char *reason, bool invalidateDelay)
{
  bool wasActive = twr->active;
  if(wasActive)TRACE(twr,mulAaFail,aaSending?aaSending:twr->stage,traceReason(reason),twr->expectedTx);
#if MUL_TWR_TRACE
  aaSending=0;
#endif
  twr->active = twr->txPending = false;
  if (invalidateDelay) {
    if(twr->planned&&aaOperation(twr))mulTwrInvalidateAa(twr);
    if (twr->delayValid) twr->syncDirty = true;
    twr->invalidated = twr->invalidated || wasActive || twr->delayValid || twr->estimateId!=0;
    twr->delayValid = false;
  }
  if (wasActive) {
    dwIdle(dev);
    clearErrors(dev);
    mulRxRearm(dev);
    if (reason) printf("MUL TWR: id=%" PRIu32 " result=FAILED reason=%s\r\n", twr->id, reason);
    else twr->deferredCancel = true;
  }
}

void mulTwrFlush(mulTwr_t *twr)
{
  if (twr->deferredCancel) {
    twr->deferredCancel = false;
    printf("MUL TWR: id=%" PRIu32 " result=FAILED reason=broadcast_priority\r\n", twr->id);
  }
}

static bool sendPacket(mulTwr_t *twr, dwDevice_t *dev, mulTwrType_t type, uint64_t target)
{
  mulNetwork_t pairNetwork;
#if MUL_TWR_TRACE
  aaSending=(uint8_t)type;
#endif
  TRACE(twr,mulAaTarget,type,0,target);
  if (!mulAntennaValid(dev)) {
    mulTwrCancel(twr, dev, "antenna_config", true);
    return false;
  }
  // Same raw-DX_TIME/live-TX_ANTD contract as Stage 2; never use immediate TX.
  uint16_t antenna = dwSpiRead16(dev, TX_ANTD, NO_SUB);
  dwTime_t raw = {.full=mulTimestampMask((mulTimestampMask(target-antenna)+511) & ~UINT64_C(511))};
  if (mulTimestampDifference(raw.full, hardwareNow(dev)) < (int64_t)leadDtu(twr) ||
      mulTimestampDifference(twr->windowEnd, target) < (int64_t)tailDtu(twr)) {
    mulTwrCancel(twr, dev, "late", false);
    return false;
  }
  dwNewTransmit(dev);
  dwSetDefaults(dev);
  clearErrors(dev);
  dwWaitForResponse(dev, false);
  dwSetTxRxTime(dev, raw);
  dwTime_t verified = {.full=0};
  dwSpiRead(dev, DX_TIME, NO_SUB, verified.raw, LEN_DX_TIME);
  if (verified.full != raw.full) {
    mulTwrCancel(twr, dev, "dx_time", true);
    return false;
  }
  twr->expectedTx = mulTimestampMask(verified.full + antenna);
  if (type == mulTwrPoll) twr->stamps[0] = twr->expectedTx;
  if (type == mulTwrResponse) twr->stamps[2] = twr->expectedTx;
  if (type == mulTwrFinal) twr->stamps[4] = twr->expectedTx;
  mulTwrPacket_t packet = {.type=type, .id=twr->id, .txTimestamp=twr->expectedTx,
                           .tof=type == mulTwrResult ? twr->candidateTof : 0};
  memcpy(packet.stamps, twr->stamps, sizeof(packet.stamps));
  memcpy(packet.power, twr->power, sizeof(packet.power));
  uint8_t frame[MUL_MULTI_TWR_BYTES+2] = {0}; // libdw1000 reads two FCS reserve bytes.
  unsigned length=twr->multiProfile?MUL_MULTI_TWR_BYTES:MUL_TWR_FRAME_LENGTH;
  if(twr->planned) {
    if(!mulPlanTwrEncode(frame,plannedSession.plan,twr->operation,&packet))return false;
  }else if(twr->multiProfile) {
    if(!mulMultiTwrEncode(frame,(const mulSchedule_t *)twrContext,twr->syncWindow,&packet))return false;
  } else if(!mulTwrEncodeNetwork(twrNetwork,frame,&packet))return false;
  dwSetData(dev,frame,length);
  if (mulTimestampDifference(raw.full, hardwareNow(dev)) < (int64_t)leadDtu(twr)) {
    mulTwrCancel(twr, dev, "late_setup", false);
    return false;
  }
  twr->stage = type;
  twr->txPending = true;
  twr->deadline = mulTimestampMask(twr->expectedTx + stepMs(twr)*MUL_TWR_DTU_PER_MS);
  dwStartTransmit(dev);
  if (dwSpiRead16(dev, SYS_STATUS, 3) & 0x0408) {
    mulTwrCancel(twr, dev, "delayed_start", false);
    return false;
  }
  TRACE(twr,mulAaArm,type,0,twr->expectedTx);
#if MUL_TWR_TRACE
  aaSending=0;
#endif
  return true;
}

bool mulTwrStartTargets(mulTwr_t *twr,dwDevice_t *dev,uint32_t cycle,uint64_t poll,uint64_t end) {
 if(!twr||!twr->planned||poll>MUL_TIMESTAMP_MASK||end>MUL_TIMESTAMP_MASK||!due(twr,cycle)||
    twr->active||(twr->attempted&&twr->id==cycle))return false;
 int64_t ahead=mulTimestampDifference(poll,hardwareNow(dev));
 if(ahead<=0||ahead>(int64_t)(plannedSession.plan->broadcast.periodMs*MUL_TWR_DTU_PER_MS))return false;
 int64_t span=mulTimestampDifference(end,poll);
 if(span<(int64_t)(4*replyMs(twr)*MUL_TWR_DTU_PER_MS+tailDtu(twr))||
    span>(int64_t)(plannedSession.plan->broadcast.periodMs*MUL_TWR_DTU_PER_MS))return false;
 twr->id=cycle;twr->attempted=twr->active=true;twr->windowEnd=end;
#if MUL_TWR_TRACE
 aaLocal=plannedSession.op.initiator;aaInput=7;aaNowValid=false;aaEnd=twr->windowEnd;
#endif
 TRACE(twr,mulAaWindow,mulTwrPoll,0,poll);
 memset(twr->stamps,0,sizeof(twr->stamps));memset(twr->power,0,sizeof(twr->power));
 return sendPacket(twr,dev,mulTwrPoll,poll);
}

bool mulTwrStart(mulTwr_t *twr, dwDevice_t *dev, uint32_t sequence, uint64_t origin)
{
  if (!due(twr,sequence) || twr->active || (twr->attempted && twr->id == sequence)) return false;
  twr->id = sequence;
  twr->attempted = twr->active = true;
  twr->windowEnd = mulTimestampMask(origin+endOffset(twr));
#if MUL_TWR_TRACE
  aaLocal=plannedSession.op.initiator;aaInput=7;aaNowValid=false;aaEnd=twr->windowEnd;
#endif
  TRACE(twr,mulAaWindow,mulTwrPoll,0,mulTimestampMask(origin+pollOffset(twr)));
  memset(twr->stamps, 0, sizeof(twr->stamps));
  memset(twr->power, 0, sizeof(twr->power));
  return sendPacket(twr, dev, mulTwrPoll, mulTimestampMask(origin+pollOffset(twr)));
}

static void logSuccess(const mulTwr_t *twr, mulAlrRole_t role, dwDevice_t *dev)
{
  mulNetwork_t pairNetwork;
  // Only after RESULT: this exchange has no pending TX. Generated plans may
  // reserve another exchange soon; CDC queue writes remain nonblocking.
  printf("MUL TWR D: id=%" PRIu32 " ant=%u/%u cal=LPS P=",
    twr->id, (unsigned int)dwSpiRead16(dev, TX_ANTD, NO_SUB),
    (unsigned int)dwSpiRead16(dev, LDE_IF, LDE_RXANTD_SUB));
  for (unsigned int i=0; i<6; i++) {
    printf("%02X%08" PRIX32 "%s", (unsigned int)(twr->stamps[i]>>32),
      (uint32_t)twr->stamps[i], i == 5 ? "" : ",");
  }
  printf(" raw_tof=%" PRIu32 " corrected_tof=",(uint32_t)twr->rawTof);
  if (twr->candidatePowerCorrected) printf("%" PRIu32,(uint32_t)twr->candidateTof); else printf("UNAVAILABLE");
  printf(" I=%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 " Q=",
    (uint32_t)mulTimestampDifference(twr->stamps[3],twr->stamps[0]),
    (uint32_t)mulTimestampDifference(twr->stamps[4],twr->stamps[3]),
    (uint32_t)mulTimestampDifference(twr->stamps[5],twr->stamps[2]),
    (uint32_t)mulTimestampDifference(twr->stamps[2],twr->stamps[1]));
  for (unsigned int i=0;i<3;i++) printf("%d/%d/%u/%u%s",twr->power[i].powerCentiDbm,
    twr->power[i].deltaDtu,twr->power[i].status,twr->power[i].table,i==2 ? "\r\n" : ",");
  // Vacuum speed of light 299792458 m/s; display integer millimetres, not an accuracy claim.
  uint32_t mm = (uint32_t)(((uint64_t)twr->candidateTof*UINT64_C(299792458000) + UINT64_C(31948800000)) /
                         UINT64_C(63897600000));
  printf("MUL TWR: id=%" PRIu32 " role=%s ref=0x%02X fol=0x%02X result=OK tof_dtu=%" PRIu32
         " distance_m=%" PRIu32 ".%03" PRIu32 " power=%s raw_mm=%" PRIu32 " uncertainty=UNKNOWN\r\n",
         twr->id, role == mulAlrReference ? "INIT" : "RESP",twrNetwork->addresses[0][0],twrNetwork->addresses[1][0], (uint32_t)twr->candidateTof, mm/1000, mm%1000, twr->candidatePowerCorrected ? "APPLIED" : "UNAVAILABLE",
         (uint32_t)(((uint64_t)twr->rawTof*UINT64_C(299792458000)+UINT64_C(31948800000))/UINT64_C(63897600000)));
}

bool mulTwrOnEvent(mulTwr_t *twr, dwDevice_t *dev, uwbEvent_t event, mulAlrRole_t role,
  uint32_t sequence, uint64_t origin, uint64_t referenceOrigin, uint64_t epoch, uint32_t nowMs)
{
  mulNetwork_t pairNetwork;
#if MUL_TWR_TRACE
  aaLocal=role==mulAlrReference?plannedSession.op.initiator:plannedSession.op.responder;
  aaInput=(uint8_t)event;aaNowValid=false;aaSending=0;
#endif
  if ((twr->active || (event == eventPacketReceived && dwGetDataLength(dev) == (twr->multiProfile?MUL_MULTI_TWR_BYTES:MUL_TWR_FRAME_LENGTH))) &&
      !mulAntennaValid(dev)) {
    if (!twr->active) printf("MUL TWR: result=FAILED reason=antenna_config\r\n");
    mulTwrCancel(twr, dev, "antenna_config", true);
    mulRxRearm(dev);
    return true;
  }
  if(event==eventPacketReceived)RX_DIAG(twr,0,0);
  int64_t phase = mulTimestampDifference(hardwareNow(dev), origin);
#if MUL_TWR_TRACE
  // Record the assigned responder window once even when no poll ever arrives.
  if(twr->planned&&plannedSession.op.kind==MUL_PLAN_AA&&role==mulAlrFollower&&
     (!aaSelected||aaSelectedCycle!=twr->id||aaSelectedOp!=twr->operation||aaSelectedLocal!=aaLocal)) {
    aaSelected=true;aaSelectedCycle=twr->id;aaSelectedOp=twr->operation;aaSelectedLocal=aaLocal;
    aaEnd=mulTimestampMask(origin+endOffset(twr));
    TRACE(twr,mulAaWindow,mulTwrPoll,0,mulTimestampMask(origin+pollOffset(twr)));
  }
#endif
#if MUL_TWR_TRACE
  if(!twr->active&&!twr->attempted&&role==mulAlrFollower&&
     (event==eventReceiveTimeout||event==eventReceiveFailed))
    TRACE(twr,mulAaWaitEvent,mulTwrPoll,2,0);
#endif
  bool expired=twr->planned?mulTimestampDifference(hardwareNow(dev),twr->windowEnd)>=0:
    phase<0||phase>=(int64_t)endOffset(twr);
  if (twr->active && (expired ||
      sequence != twr->id || (role == mulAlrFollower && epoch != twr->epoch))) {
    mulTwrCancel(twr, dev, "window_or_epoch", role == mulAlrFollower && epoch != twr->epoch);
  }
  if (event == eventPacketReceived &&
      (dwGetDataLength(dev) == MUL_TEST_FRAME_LENGTH || dwGetDataLength(dev) == MUL_RELATION_FRAME_LENGTH||dwGetDataLength(dev)==MUL_MULTI_REFERENCE_BYTES||dwGetDataLength(dev)==MUL_MULTI_FOLLOWER_BYTES)) {
    mulTwrCancel(twr, dev, NULL, false); // Caller programs/re-arms broadcast radio before logging.
    return false;
  }
  if (!twr->active && (event != eventPacketReceived || dwGetDataLength(dev) != (twr->multiProfile?MUL_MULTI_TWR_BYTES:MUL_TWR_FRAME_LENGTH))) return false;
  if (twr->active && mulTimestampDifference(hardwareNow(dev), twr->deadline) >= 0) {
    mulTwrCancel(twr, dev, "timeout", false);
    return true;
  }
  if (event == eventTimeout) return true; // Absolute deadline; early wake cannot extend it.
  if (event == eventPacketSent && twr->txPending) {
    dwTime_t actual = {.full=0};
    dwGetTransmitTimestamp(dev, &actual);
    if (mulTimestampMask(actual.full) != twr->expectedTx) {
      mulTwrCancel(twr, dev, "tx_stamp", true);
      return true;
    }
    TRACE(twr,mulAaTx,twr->stage,0,mulTimestampMask(actual.full));
    twr->txPending = false;
    if (twr->stage == mulTwrResult) {
      twr->active = false;
      mulRxRearm(dev);
      TRACE(twr,mulAaDone,twr->stage,0,0);
      logSuccess(twr, role, dev);
    } else {
      twr->stage++;
      mulRxRearm(dev);
      TRACE(twr,mulAaListen,twr->stage,0,0);
    }
    return true;
  }
  if (event != eventPacketReceived || twr->txPending) {
    mulTwrCancel(twr, dev, "radio_event", false);
    return true;
  }
  uint8_t frame[MUL_MULTI_TWR_BYTES];
  mulTwrPacket_t packet;unsigned length=twr->multiProfile?MUL_MULTI_TWR_BYTES:MUL_TWR_FRAME_LENGTH;
  bool valid=dwGetDataLength(dev)==length;
  if (valid) {
    dwGetData(dev,frame,length);
    if(twr->planned)valid=mulPlanTwrDecode(plannedSession.plan,sequence,twr->operation,frame,length,&packet);
    else if(twr->multiProfile) {
      unsigned slot;valid=mulMultiTwrDecode((const mulSchedule_t *)twrContext,twr->syncWindow,frame,length,&packet,&slot)&&slot==twr->followerSlot;
    } else valid=mulTwrDecodeNetwork(twrNetwork,frame,length,&packet);
    valid=valid&&sender(packet.type)!=role&&packet.id==sequence;
  }
  if (!valid) {
    TRACE(twr,mulAaReject,twr->stage,12,0);
    if (twr->active) mulTwrCancel(twr, dev, "packet", true); else mulRxRearm(dev);
    return true;
  }
  dwTime_t rx = {.full=0};
  // RX_STAMP already includes LDE_RXANTD; keep it for timing guards/replies.
  dwGetRawReceiveTimestamp(dev, &rx);
  rx.full = mulTimestampMask(rx.full);
  mulPowerRx_t capture={.raw=rx.full,.corrected=rx.full};
  if (packet.type <= mulTwrFinal) capture=mulPowerCapture(dev,rx.full);
  int64_t captureAge = mulTimestampDifference(hardwareNow(dev), rx.full);
  if (captureAge < 0 || captureAge > (int64_t)(15*MUL_TWR_DTU_PER_MS)) {
    TRACE(twr,mulAaReject,packet.type,18,rx.full);
    if (twr->active) mulTwrCancel(twr, dev, "rx_stamp", false); else mulRxRearm(dev);
    return true;
  }
  if (packet.type == mulTwrPoll && role == mulAlrFollower && !twr->active) {
    int64_t arrivalPhase = mulTimestampDifference(rx.full, origin);
    int64_t remotePhase = twr->planned&&plannedSession.op.kind==MUL_PLAN_AR?0:mulTimestampDifference(packet.stamps[0], referenceOrigin);
    bool phaseValid;
    if(twr->planned) {
      phaseValid=arrivalPhase>=(int64_t)microsDtu(plannedSession.op.startUs)&&arrivalPhase<(int64_t)endOffset(twr);
      // Only AA's initiator is the reference clock. An AR initiator may be a
      // different ALR: never compare its P1 directly to reference-domain origin.
      if(plannedSession.op.kind==MUL_PLAN_AA)phaseValid=phaseValid&&remotePhase>=(int64_t)microsDtu(plannedSession.op.startUs)&&remotePhase<(int64_t)endOffset(twr);
    }else phaseValid=arrivalPhase>=(int64_t)((pollMs(twr)-50)*MUL_TWR_DTU_PER_MS)&&arrivalPhase<=(int64_t)((pollMs(twr)+50)*MUL_TWR_DTU_PER_MS)&&
      remotePhase>=(int64_t)((pollMs(twr)-50)*MUL_TWR_DTU_PER_MS)&&remotePhase<=(int64_t)((pollMs(twr)+50)*MUL_TWR_DTU_PER_MS);
    if (!due(twr,sequence) || (twr->attempted && twr->id == sequence) || !phaseValid) {
      TRACE(twr,mulAaReject,packet.type,22,rx.full);
      mulRxRearm(dev);
      return true;
    }
    memset(twr->stamps, 0, sizeof(twr->stamps));
    memset(twr->power, 0, sizeof(twr->power));
    twr->id = sequence;
    twr->attempted = twr->active = true;
    // Preserve the committed estimate during an attempted refresh.
    twr->windowEnd = mulTimestampMask(origin+endOffset(twr));
    twr->epoch = epoch;
    // Backdate the coarse acquisition time by hardware RX-to-callback latency,
    // rounded upwards. Neither callback delay nor RESULT delivery can re-date it.
    // Commit acquisition time only after RESULT, backdated from stored P2.
    TRACE(twr,mulAaRx,packet.type,0,rx.full);
    twr->stamps[0] = packet.stamps[0];
    twr->stamps[1] = capture.corrected; twr->power[0]=capture.power;
    sendPacket(twr, dev, mulTwrResponse, mulTimestampMask(rx.full+replyMs(twr)*MUL_TWR_DTU_PER_MS));
    return true;
  }
  unsigned int prefix = packet.type == mulTwrResponse ? 1 : packet.type == mulTwrFinal ? 3 :
                        packet.type == mulTwrReport ? 5 : 6;
  bool metadataMatch=true;
  for (unsigned int i=0;i<prefix/2;i++) if (!samePower(&packet.power[i],&twr->power[i])) metadataMatch=false;
  if (!metadataMatch || !twr->active || packet.type != twr->stage || packet.id != twr->id ||
      memcmp(packet.stamps, twr->stamps, prefix*sizeof(uint64_t))) {
    if (twr->active) mulTwrCancel(twr, dev, "exchange", true); else mulRxRearm(dev);
    return true;
  }
  TRACE(twr,mulAaRx,packet.type,0,rx.full);
  if (packet.type == mulTwrResponse) {
    if (!intervalReply(packet.stamps[2],packet.stamps[1],replyMs(twr))) {
      mulTwrCancel(twr, dev, "response_interval", true);
      return true;
    }
    twr->stamps[1] = packet.stamps[1]; twr->stamps[2] = packet.stamps[2]; twr->stamps[3] = capture.corrected;
    twr->power[0]=packet.power[0]; twr->power[1]=capture.power;
    sendPacket(twr, dev, mulTwrFinal, mulTimestampMask(rx.full+replyMs(twr)*MUL_TWR_DTU_PER_MS));
  } else if (packet.type == mulTwrFinal) {
    twr->stamps[3] = packet.stamps[3]; twr->stamps[4] = packet.stamps[4]; twr->stamps[5] = capture.corrected;
    twr->power[1]=packet.power[1]; twr->power[2]=capture.power;
    if (!evaluate(twr)) mulTwrCancel(twr, dev, "intervals", true);
    else sendPacket(twr, dev, mulTwrReport, mulTimestampMask(rx.full+replyMs(twr)*MUL_TWR_DTU_PER_MS));
  } else if (packet.type == mulTwrReport) {
    twr->stamps[5] = packet.stamps[5]; twr->power[2]=packet.power[2];
    if (!intervalReply(packet.txTimestamp,packet.stamps[5],replyMs(twr)) || !evaluate(twr))
      mulTwrCancel(twr, dev, "range", true);
    else sendPacket(twr, dev, mulTwrResult, mulTimestampMask(rx.full+replyMs(twr)*MUL_TWR_DTU_PER_MS));
  } else if (packet.type == mulTwrResult) {
    int64_t resultInterval = mulTimestampDifference(packet.txTimestamp, twr->stamps[4]);
    if (resultInterval < (int64_t)(3*replyMs(twr)*MUL_TWR_DTU_PER_MS/2) ||
        resultInterval > (int64_t)(3*replyMs(twr)*MUL_TWR_DTU_PER_MS) ||
        !evaluate(twr) || twr->candidateTof != packet.tof) {
      mulTwrCancel(twr, dev, "result", true);
    } else {
      // P2 is power corrected; reconstruct its raw capture before backdating.
      uint64_t rawPoll=mulTimestampMask(twr->stamps[1]-twr->power[0].deltaDtu);
      int64_t elapsed=mulTimestampDifference(hardwareNow(dev),rawPoll);
      if (elapsed<0 || elapsed>(int64_t)(200*MUL_TWR_DTU_PER_MS)) {
        mulTwrCancel(twr,dev,"measurement_age",true); return true;
      }
      twr->measuredMs=nowMs-(uint32_t)(((uint64_t)elapsed+MUL_TWR_DTU_PER_MS-1)/MUL_TWR_DTU_PER_MS);
      twr->estimateId=twr->id;
      twr->tof=twr->candidateTof; twr->powerCorrected=twr->candidatePowerCorrected;
      twr->invalidated=false;
      twr->delayValid = true;
      if(twr->planned&&aaOperation(twr)) {
        const mulNetwork_t *pair=twrNetwork;
        twr->aa=(mulTwrAa_t){.epoch=twr->epoch,.measuredMs=twr->measuredMs,.sequence=twr->estimateId,
          .tof=twr->tof,.reference=pair->addresses[0][0],.follower=pair->addresses[1][0],
          .valid=true,.powerCorrected=twr->powerCorrected};
      }
      twr->syncDirty = true;
      twr->active = false;
      mulRxRearm(dev);
      TRACE(twr,mulAaDone,twr->stage,0,0);
      logSuccess(twr, role, dev);
    }
  }
  return true;
}

uint32_t mulTwrWait(const mulTwr_t *twr, dwDevice_t *dev, uint64_t origin, uint32_t fallback)
{
  if (!twr->active) return fallback;
  uint64_t now = hardwareNow(dev);
  int64_t step = mulTimestampDifference(twr->deadline, now);
  int64_t end = mulTimestampDifference(twr->planned?twr->windowEnd:mulTimestampMask(origin+endOffset(twr)), now);
  if (end < step) step = end;
  uint32_t wait = step <= 0 ? 1 : (uint32_t)(((uint64_t)step+MUL_TWR_DTU_PER_MS-1)/MUL_TWR_DTU_PER_MS);
  return wait < fallback ? wait : fallback;
}

static const mulNetwork_t *delayNetwork(const mulTwr_t *twr,mulNetwork_t *network) {
 if(!twr->planned)return networkFor(twr,network);
 uint8_t ref=twr->aa.reference,fol=twr->aa.follower;
 if(twr->aa.reference==twr->aa.follower) {
  ref=plannedSession.plan->broadcast.ids[0];
  fol=plannedSession.op.kind==MUL_PLAN_AA?plannedSession.op.responder:plannedSession.op.initiator;
 }
 if(!mulNetworkInit(network,ref,fol))memset(network,0,sizeof(*network));
 network->slotMs=plannedSession.plan->broadcast.slotMs;return network;
}
bool mulTwrDelay(const mulTwr_t *twr, const mulClock_t *clock, mulClockDelay_t *delay)
{
  mulNetwork_t pairNetwork;
  if (twr == NULL || clock == NULL || delay == NULL) return false;
  if(twr->planned) {
    if(!twr->aa.valid)return false;
    *delay=(mulClockDelay_t){.epoch=twr->aa.epoch,.oneWayDtu=twr->aa.tof,.units=mulClockDelayReferenceDtu,
      .measuredMs=twr->aa.measuredMs,.referenceSequence=twr->aa.sequence,.maxAgeMs=MUL_PROP_MAX_AGE_MS,
      .valid=true,.measurementBoundDtu=MUL_CLOCK_BOUND_UNKNOWN,.motionBoundDtuPerMs=MUL_CLOCK_BOUND_UNKNOWN};
    const mulNetwork_t *pair=delayNetwork(twr,&pairNetwork);
    memcpy(delay->reference,pair->addresses[0],8);memcpy(delay->follower,pair->addresses[1],8);
    return delay->epoch==clock->epoch;
  }
  if(!twr->delayValid)return false;
  *delay = (mulClockDelay_t){.epoch=twr->epoch, .oneWayDtu=twr->tof, .units=mulClockDelayReferenceDtu,
    .measuredMs=twr->measuredMs, .referenceSequence=twr->estimateId, .maxAgeMs=MUL_PROP_MAX_AGE_MS,
    .valid=true, .measurementBoundDtu=MUL_CLOCK_BOUND_UNKNOWN, .motionBoundDtuPerMs=MUL_CLOCK_BOUND_UNKNOWN};
  memcpy(delay->reference, twrNetwork->addresses[0], 8); memcpy(delay->follower, twrNetwork->addresses[1], 8);
  return delay->epoch == clock->epoch;
}

mulPropagationState_t mulTwrPropagationState(const mulTwr_t *twr,const mulClock_t *clock,uint32_t nowMs)
{
  if(twr->planned) {
    if(twr->aa.invalidated||(twr->aa.valid&&twr->aa.epoch!=clock->epoch))return mulPropagationInvalidated;
    if(!twr->aa.valid)return mulPropagationNone;
    if((uint32_t)(nowMs-twr->aa.measuredMs)>MUL_PROP_MAX_AGE_MS)return mulPropagationExpired;
    return (uint32_t)(clock->sequence-twr->aa.sequence)<=1?mulPropagationFresh:mulPropagationReused;
  }
  if (twr->invalidated || (twr->delayValid && twr->epoch!=clock->epoch)) return mulPropagationInvalidated;
  if (!twr->delayValid) return mulPropagationNone;
  if ((uint32_t)(nowMs-twr->measuredMs)>MUL_PROP_MAX_AGE_MS) return mulPropagationExpired;
  return (uint32_t)(clock->sequence-twr->estimateId)<=1 ? mulPropagationFresh : mulPropagationReused;
}

void mulTwrSync(mulTwr_t *twr, const mulClock_t *clock, uint32_t nowMs, bool force)
{
  mulNetwork_t pairNetwork;
  const mulNetwork_t *pair=delayNetwork(twr,&pairNetwork);
  mulClockDelay_t delay;
  bool supplied = mulTwrDelay(twr, clock, &delay);
  mulClockDelayStatus_t status = mulClockDelayStatusNetwork(pair,clock, supplied ? &delay : NULL, nowMs);
  mulClockCorrectedMapping_t mapped;
  bool available = mulClockMapRefToFollowerSimultaneousNetwork(pair,clock, clock->referenceTx,
    supplied ? &delay : NULL, nowMs, &mapped);
  mulClockMapStatus_t mapStatus = mulClockMapStatus(clock, nowMs);
  if (force || twr->syncDirty || available != twr->syncAvailable) {
    printf("MUL ALR: SYNC src=0x%02X map=%s delay=%s correction=%s delay_age_ms=%" PRIu32
           " physical_bound=UNKNOWN\r\n",
      pair->addresses[0][0],mapStatus == mulClockMapValid ? "VALID" : mapStatus == mulClockMapWarmup ? "WARMUP" :
      mapStatus == mulClockMapStale ? "STALE" : "INVALID",
      status == mulClockDelayValid ? "VALID" : status == mulClockDelayStale ? "STALE" : "INVALID",
      available ? "AVAILABLE" : "UNAVAILABLE", supplied ? nowMs-delay.measuredMs : 0);
  }
  twr->syncAvailable = available;
  twr->syncDirty = false;
}

uint32_t mulTwrDelayWait(const mulTwr_t *twr, uint32_t nowMs, uint32_t fallback)
{
  uint32_t age = nowMs - (twr->planned?twr->aa.measuredMs:twr->measuredMs);
  if ((twr->planned?twr->aa.valid:twr->delayValid) && age <= MUL_PROP_MAX_AGE_MS) {
    uint32_t remaining = MUL_PROP_MAX_AGE_MS + 1 - age;
    if (remaining < fallback) return remaining;
  }
  return fallback;
}
