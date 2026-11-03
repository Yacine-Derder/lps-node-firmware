#include "mul_log.h"
#include "mul_antenna.h"
#include "mul_pose.h"
#include "mul_geometry.h"
#include "mul_clock_fit.h"
#include "mul_clock.h"
#include "mul_clock_workspace.h"
/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_twr.h"
#include "mul_schedule.h"
#include "mul_ranging_plan.h"
#include "mul_plan_protocol.h"
#include "mul_multi_protocol.h"
#include "mul_relation.h"
#include "mul_role_state.h"
#include "mul_downlink.h"
#include "mul_localization.h"
#include "mul_power.h"
#include "mul_observation.h"
#include <math.h>
#include "mul_common.h"
#include "mul_rx.h"

#include <stdlib.h>
#include <string.h>

static uint64_t mapCost,fitCost;static unsigned mapCalls;
static void advance(int64_t at);
static bool timedMap(const mulClock_t *,uint64_t,uint32_t,mulClockMapping_t *);
static bool timedCorrected(const mulNetwork_t *,const mulClock_t *,uint64_t,const mulClockDelay_t *,uint32_t,mulClockCorrectedMapping_t *);
static void timedFinish(const mulNetwork_t *,mulClock_t *,mulClockFit_t *,const mulClockFitConfig_t *,const mulTestPacket_t *,uint64_t,uint32_t,mulClockFitPending_t *);
#define mulClockMapRefToFollower timedMap
#define mulClockMapRefToFollowerSimultaneousNetwork timedCorrected
#define mulClockFitFinish timedFinish
#include "../src/mul_alr.c"
#undef mulClockMapRefToFollower
#undef mulClockMapRefToFollowerSimultaneousNetwork
#undef mulClockFitFinish
static mulLocalization_t capturedObservation;
static unsigned int observationCount;
static void captureObservation(const mulLocalization_t *o) { capturedObservation=*o; observationCount++; }
#define mulLocalizationPrint captureObservation
#include "../src/mul_rlr.c"
#undef mulLocalizationPrint

#define MS MUL_TWR_DTU_PER_MS
#define SEC (1000*MS)
typedef struct {
  dwDevice_t device;
  uint8_t tx[MUL_MULTI_TWR_BYTES+2], rx[MUL_MULTI_TWR_BYTES];
  unsigned int txLength, rxLength, sends, rearms, aborts;
  uint64_t now, raw, actual, arrival;
  uint16_t antenna, rxAntenna, physicalTx, physicalRx, status, injectStatus;
  int64_t stampError, dxError;
  uint64_t setupLag;
  bool armed, receiving;
  float powerDbm; int8_t powerDelta; int16_t rxBiasDtu; unsigned int corrections;
} FakeRadio;
static FakeRadio radios[3];
static mulTwr_t contexts[2];
static mulClock_t model;
static int32_t skew;
static int64_t physical, framePhysical, flight;
static uint64_t offsets[3], origins[2];
static uint32_t exchangeSequence;
static TickType_t ticks;
static bool useAlr[2];
static mulAlrRole_t protocolRole[2];
static const mulSchedule_t *simulationSchedule;
static const mulRangingPlan_t *simulationPlan;
static const uint8_t identities[2][8] = {{0x84,0,0,0,0,0,0xcf,0xbc},{0x85,0,0,0,0,0,0xcf,0xbc}};

TickType_t xTaskGetTickCount(void) { return ticks; }
static FakeRadio *radio(dwDevice_t *device) {
  for (int i = 0; i < 3; i++) if (device == &radios[i].device) return &radios[i];
  TEST_FAIL_MESSAGE("Unknown radio");
  return &radios[0];
}
void dwIdle(dwDevice_t *dev) { FakeRadio *r=radio(dev); if (r->armed) r->aborts++; r->armed=false; r->receiving=false; }
void dwNewTransmit(dwDevice_t *dev) { dwIdle(dev); }
void dwNewReceive(dwDevice_t *dev) { dwIdle(dev); }
void dwStartReceive(dwDevice_t *dev) { radio(dev)->receiving=true; radio(dev)->rearms++; }
void dwSetDefaults(dwDevice_t *dev) { (void)dev; }
void dwReceivePermanently(dwDevice_t *dev, bool enable) { (void)dev; TEST_ASSERT_FALSE(enable); }
void dwWaitForResponse(dwDevice_t *dev, bool enable) { (void)dev; TEST_ASSERT_FALSE(enable); }
void dwNewConfiguration(dwDevice_t *dev) { dwIdle(dev); }
void dwSetFrameFilter(dwDevice_t *dev, bool enable) { (void)dev; TEST_ASSERT_FALSE(enable); }
void dwSetReceiverAutoReenable(dwDevice_t *dev, bool enable) { (void)dev; TEST_ASSERT_FALSE(enable); }
void dwSetReceiveWaitTimeout(dwDevice_t *dev, uint16_t timeout) { (void)dev; TEST_ASSERT_EQUAL_UINT(0,timeout); }
void dwInterruptOnReceiveFailed(dwDevice_t *dev, bool enable) { (void)dev; TEST_ASSERT_TRUE(enable); }
void dwWriteSystemConfigurationRegister(dwDevice_t *dev) { (void)dev; }
void dwWriteSystemEventMaskRegister(dwDevice_t *dev) { (void)dev; }
void dwGetSystemTimestamp(dwDevice_t *dev, dwTime_t *time) { time->full=radio(dev)->now; }
void dwGetTransmitTimestamp(dwDevice_t *dev, dwTime_t *time) { time->full=radio(dev)->actual; }
void dwGetRawReceiveTimestamp(dwDevice_t *dev, dwTime_t *time) { time->full=radio(dev)->arrival; }
void dwSetTxRxTime(dwDevice_t *dev, const dwTime_t time) {
  TEST_ASSERT_EQUAL_UINT64(0, time.full & 511);
  radio(dev)->raw=time.full;
  radio(dev)->now=mulTimestampMask(radio(dev)->now+radio(dev)->setupLag);
}
void dwSpiRead(dwDevice_t *dev, uint8_t reg, uint32_t sub, void *data, size_t size) {
#if MUL_RX_DIAG
  if(reg==0x19){TEST_ASSERT_EQUAL(4,size);uint32_t state=radio(dev)->receiving?0x050500:0x010000;memcpy(data,&state,4);return;}
  if(reg==SYS_STATUS){TEST_ASSERT_EQUAL(5,size);memcpy(data,dev->sysstatus,5);return;}
#endif
  TEST_ASSERT_EQUAL_UINT8(DX_TIME,reg); TEST_ASSERT_EQUAL_UINT32(NO_SUB,sub); TEST_ASSERT_EQUAL_UINT(5,size);
  mulEncodeTimestamp(data, radio(dev)->raw+radio(dev)->dxError);
}
uint16_t dwSpiRead16(dwDevice_t *dev, uint8_t reg, uint32_t sub) {
  if (reg == TX_ANTD) { TEST_ASSERT_EQUAL_UINT32(NO_SUB,sub); return radio(dev)->antenna; }
  if (reg == LDE_IF) { TEST_ASSERT_EQUAL_UINT32(LDE_RXANTD_SUB,sub); return radio(dev)->rxAntenna; }
  TEST_ASSERT_EQUAL_UINT8(SYS_STATUS,reg); TEST_ASSERT_EQUAL_UINT32(3,sub); return radio(dev)->status;
}
void dwSpiWrite(dwDevice_t *dev, uint8_t reg, uint32_t sub, const void *data, size_t size) {
  if(reg==TX_ANTD||reg==LDE_IF){
    TEST_ASSERT_EQUAL_UINT(2,size);const uint8_t *b=data;uint16_t value=b[0]|b[1]<<8;
    if(reg==TX_ANTD){TEST_ASSERT_EQUAL_UINT32(NO_SUB,sub);radio(dev)->antenna=value;}
    else{TEST_ASSERT_EQUAL_UINT32(LDE_RXANTD_SUB,sub);radio(dev)->rxAntenna=value;}
    return;
  }
  const uint8_t expected[] = {0x08,0x04};
  TEST_ASSERT_EQUAL_UINT8(SYS_STATUS,reg); TEST_ASSERT_EQUAL_UINT32(3,sub);
  TEST_ASSERT_EQUAL_UINT(2,size); TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,data,size);
  radio(dev)->status=0;
}
void dwSetData(dwDevice_t *dev, uint8_t *data, unsigned int size) {
  TEST_ASSERT_TRUE(size == MUL_TEST_FRAME_LENGTH || size == MUL_RELATION_FRAME_LENGTH || size == MUL_TWR_FRAME_LENGTH || size==MUL_MULTI_REFERENCE_BYTES || size==MUL_MULTI_FOLLOWER_BYTES || size==MUL_MULTI_TWR_BYTES);
  memcpy(radio(dev)->tx,data,size+2); // Same FCS reserve read as the real driver.
  radio(dev)->txLength=size;
}
void dwStartTransmit(dwDevice_t *dev) {
  FakeRadio *r=radio(dev); r->sends++; r->armed=true; r->status=r->injectStatus;
  r->actual=mulTimestampMask(r->raw+r->antenna+r->stampError);
}
unsigned int dwGetDataLength(dwDevice_t *dev) { return radio(dev)->rxLength; }
void dwGetData(dwDevice_t *dev, uint8_t *data, unsigned int size) {
  TEST_ASSERT_EQUAL_UINT(radio(dev)->rxLength,size); memcpy(data,radio(dev)->rx,size);
}

float dwGetReceivePower(dwDevice_t *dev) { return radio(dev)->powerDbm; }
void dwCorrectTimestamp(dwDevice_t *dev,dwTime_t *t) {
  radio(dev)->corrections++; t->full+=radio(dev)->powerDelta;
}

static long double rate(int node) { return node == 0 ? 1.0L : node == 1 ? 1.0L+skew/1e9L : 1.0L-10000.0L/1e9L; }
static uint64_t clockAt(int node, int64_t at) {
  return mulTimestampMask(offsets[node]+llroundl(at*rate(node)));
}
static void advance(int64_t at) {
  physical=at;
  ticks=(TickType_t)(5000+at/(int64_t)MS);
  for (int i=0;i<3;i++) radios[i].now=clockAt(i,at);
}
// Model CPU time around the real production mapping/math, not an alternative filter.
static bool timedMap(const mulClock_t *c,uint64_t tx,uint32_t now,mulClockMapping_t *out) {
 bool ok=mulClockMapRefToFollower(c,tx,now,out);mapCalls++;if(mapCost)advance(physical+mapCost);return ok;
}
static bool timedCorrected(const mulNetwork_t *n,const mulClock_t *c,uint64_t tx,const mulClockDelay_t *delay,uint32_t now,mulClockCorrectedMapping_t *out) {
 bool ok=mulClockMapRefToFollowerSimultaneousNetwork(n,c,tx,delay,now,out);mapCalls++;if(mapCost)advance(physical+mapCost);return ok;
}
static void timedFinish(const mulNetwork_t *n,mulClock_t *c,mulClockFit_t *fit,const mulClockFitConfig_t *config,const mulTestPacket_t *packet,uint64_t rx,uint32_t now,mulClockFitPending_t *pending) {
 mulClockFitFinish(n,c,fit,config,packet,rx,now,pending);if(fitCost)advance(physical+fitCost);
}
static int64_t sendTime(int node) {
  return physical+llroundl((mulTimestampDifference(radios[node].raw,radios[node].now)+radios[node].physicalTx)/rate(node));
}
static mulTwr_t *ctx(int node) { return useAlr[node] ? &ranging : &contexts[node]; }
static mulClock_t *followerModel(void) { return useAlr[1] ? &followerClock : &model; }
static uint32_t dispatch(int node, uwbEvent_t event) {
  if (useAlr[node]) return mulAlrAlgorithm.onEvent(&radios[node].device,event);
  mulTwrOnEvent(ctx(node), &radios[node].device,event,protocolRole[node],exchangeSequence,
    origins[node],origins[0],followerModel()->epoch,ticks);
  return mulTwrWait(ctx(node),&radios[node].device,origins[node],3000);
}
static void modelAcquire(void) {
  mulClockReset(&model);
  mulTestPacket_t packet;
  memcpy(packet.source,identities[0],8);
  for (unsigned int i=0;i<6;i++) {
    int64_t at=framePhysical-(int64_t)(5-i)*SEC;
    packet.sequence=exchangeSequence-5+i; packet.txTimestamp=clockAt(0,at);
    mulClockObserve(&model,&packet,clockAt(1,at+flight),i*1000,true);
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid,model.status);
}
static void scenario(int32_t ppb, int64_t tof) {
  skew=ppb; flight=tof; framePhysical=0; exchangeSequence=5;
  origins[0]=clockAt(0,0); origins[1]=clockAt(1,tof);
  modelAcquire(); advance(630*(int64_t)MS);
  for (int i=0;i<2;i++) mulRxInit(&radios[i].device);
}
void setUp(void) {
  mulLogSetLevel(mulLogDebug);
  mulAntennaSelect(MUL_ANTENNA_TX_DTU,MUL_ANTENNA_RX_DTU);
  mulGeometrySelect(mulDelayTwr);mulGeometryClear();
  memset(radios,0,sizeof(radios)); memset(contexts,0,sizeof(contexts)); memset(useAlr,0,sizeof(useAlr));
  for (int i=0;i<3;i++) {
    radios[i].antenna=radios[i].physicalTx=MUL_ANTENNA_TX_DTU;
    radios[i].rxAntenna=radios[i].physicalRx=MUL_ANTENNA_RX_DTU;
    radios[i].device.antennaDelay.full=MUL_ANTENNA_TX_DTU;
    radios[i].device.channel=CHANNEL_2; radios[i].device.pulseFrequency=TX_PULSE_FREQ_64MHZ;
    radios[i].powerDbm=NAN;
  }
  observationCount=0; offsets[2]=MUL_TIMESTAMP_MASK-5000*MS;
  offsets[0]=MUL_TIMESTAMP_MASK-660*MS; offsets[1]=UINT64_C(0x723456789a);
  skew=0; flight=0; physical=framePhysical=0; ticks=5000;mapCost=fitCost=0;mapCalls=0;
  protocolRole[0]=mulAlrReference;protocolRole[1]=mulAlrFollower;
  exchangeSequence=5; mulClockReset(&model);
}
void tearDown(void) {}

// Deliver a real encoded frame at hardware marker time, callbacks 1/2 ms later.
static void transfer(int from) {
  int to=1-from; FakeRadio *s=&radios[from], *r=&radios[to];
  TEST_ASSERT_TRUE(s->armed); TEST_ASSERT_TRUE(r->receiving);
  uint8_t payload[MUL_MULTI_TWR_BYTES]; unsigned int length=s->txLength;
  TEST_ASSERT_EQUAL_UINT(ctx(from)->multiProfile?MUL_MULTI_TWR_BYTES:MUL_TWR_FRAME_LENGTH,length);
  memcpy(payload,s->tx,length);
  mulTwrPacket_t packet;
  if(ctx(from)->planned){TEST_ASSERT_TRUE(mulPlanTwrDecode(simulationPlan,exchangeSequence,ctx(from)->operation,payload,length,&packet));}
  else if(ctx(from)->multiProfile) {unsigned slot;TEST_ASSERT_TRUE(mulMultiTwrDecode(simulationSchedule,ctx(from)->syncWindow,payload,length,&packet,&slot));}
  else TEST_ASSERT_TRUE(mulTwrDecode(payload,length,&packet));
  if (s->stampError == 0) TEST_ASSERT_EQUAL_UINT64(s->actual,packet.txTimestamp);
  int64_t when=sendTime(from);
  advance(when+(int64_t)MS); s->armed=false; dispatch(from,eventPacketSent);
  advance(when+flight+2*(int64_t)MS);
  memcpy(r->rx,payload,length); r->rxLength=length; r->arrival=mulTimestampMask(clockAt(to,when+flight)+r->physicalRx-r->rxAntenna+r->rxBiasDtu); r->receiving=false;
  dispatch(to,eventPacketReceived);
}
static void completeExchange(void) { transfer(0); transfer(1); transfer(0); transfer(1); transfer(0); }

void test_mulTwrCompleteSixTimestampExchangeWithIndependentSignedClockRates(void) {
  const int32_t skews[]={0,5000,-10000,150000,-150000};
  const uint32_t metres[]={0,1,5,10,30};
  for (unsigned int a=0;a<5;a++) for (unsigned int b=0;b<5;b++) {
    if (metres[b] == 0 && skews[a] != 0) continue; // Quantized negative sub-DTU ToF must reject.
    setUp(); int64_t tof=(metres[b]*SEC+149896229)/299792458;
    scenario(skews[a],tof);
    TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
    completeExchange();
    TEST_ASSERT_FALSE(ctx(0)->active); TEST_ASSERT_FALSE(ctx(1)->active);
    TEST_ASSERT_TRUE(ctx(1)->delayValid); TEST_ASSERT_UINT32_WITHIN(1,tof,ctx(1)->tof);
    uint32_t pollUptime=(uint32_t)(5000+(framePhysical+
      mulTimestampDifference(ctx(0)->stamps[0],origins[0])+flight)/(int64_t)MS);
    TEST_ASSERT_TRUE((int32_t)(pollUptime-ctx(1)->measuredMs) >= 0);
    TEST_ASSERT_TRUE(pollUptime-ctx(1)->measuredMs <= 2); // Hardware age, not callback/RESULT time.
    TEST_ASSERT_TRUE(radios[0].receiving && radios[1].receiving);
    TEST_ASSERT_EQUAL_UINT(3,radios[0].sends); TEST_ASSERT_EQUAL_UINT(2,radios[1].sends);
    // The six saved markers belong to the stated node/operation, not a common origin.
    TEST_ASSERT_EQUAL_UINT64(ctx(0)->stamps[0],ctx(1)->stamps[0]);
    TEST_ASSERT_EQUAL_UINT64(ctx(0)->stamps[5],ctx(1)->stamps[5]);
    mulClockDelay_t delay; TEST_ASSERT_TRUE(mulTwrDelay(ctx(1),&model,&delay));
    TEST_ASSERT_EQUAL_INT(mulClockDelayValid,mulClockDelayStatus(&model,&delay,ticks));
    TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN,delay.measurementBoundDtu);
    TEST_ASSERT_EQUAL_UINT64(MUL_CLOCK_BOUND_UNKNOWN,delay.motionBoundDtuPerMs);
    mulTwrSync(ctx(1),&model,ticks,false); TEST_ASSERT_TRUE(ctx(1)->syncAvailable);
    mulClockCorrectedMapping_t mapped;
    TEST_ASSERT_TRUE(mulClockMapRefToFollowerSimultaneous(&model,model.referenceTx,&delay,ticks,&mapped));
    TEST_ASSERT_UINT64_WITHIN(2,0,(uint64_t)llabs(mulTimestampDifference(mapped.timestamp,clockAt(1,0))));
  }
}

void test_mulTwrMathDomainPrecisionZeroAndInvalidIntervals(void) {
  uint64_t t[6]={0,0,0,0,0,0}; uint32_t tof=999;
  // Exact six-marker construction with asymmetric replies and an independent -10 ppm clock.
  const int64_t d=63898, a=18*MS, b=23*MS;
  uint64_t localBase=UINT64_C(0xfedcba9876), remoteBase=MUL_TIMESTAMP_MASK-10*MS;
  t[0]=remoteBase; t[1]=localBase;
  t[2]=mulTimestampMask(localBase+llroundl(a*0.999990L));
  t[3]=mulTimestampMask(remoteBase+a+2*d); t[4]=mulTimestampMask(t[3]+b);
  t[5]=mulTimestampMask(localBase+llroundl((a+b+2*d)*0.999990L));
  TEST_ASSERT_TRUE(mulTwrCalculate(t,&tof)); TEST_ASSERT_UINT32_WITHIN(1,d,tof);
  uint64_t saved[6]; memcpy(saved,t,sizeof(t));
  t[4]=t[3]; TEST_ASSERT_FALSE(mulTwrCalculate(t,&tof)); memcpy(t,saved,sizeof(t));
  t[5]=mulTimestampMask(t[2]+(UINT64_C(1)<<39)); TEST_ASSERT_FALSE(mulTwrCalculate(t,&tof));
  TEST_ASSERT_FALSE(mulTwrCalculate(NULL,&tof)); TEST_ASSERT_FALSE(mulTwrCalculate(saved,NULL));
  t[0]=t[1]=0; t[2]=t[3]=20*MS; t[4]=t[5]=40*MS;
  TEST_ASSERT_TRUE(mulTwrCalculate(t,&tof)); TEST_ASSERT_EQUAL_UINT32(0,tof);
  t[5]--; TEST_ASSERT_FALSE(mulTwrCalculate(t,&tof)); // Negative ToF: never clamp.
  t[5]=40*MS+1000000; TEST_ASSERT_FALSE(mulTwrCalculate(t,&tof)); // Implausible range/rate.
}

void test_mulTwrWireFormatStrictBoundsRolesAndBroadcastSeparation(void) {
  mulTwrPacket_t packet={.type=mulTwrPoll,.id=UINT32_MAX,.txTimestamp=MUL_TIMESTAMP_MASK};
  packet.stamps[0]=packet.txTimestamp;
  uint8_t bytes[MUL_TWR_FRAME_LENGTH]; mulTwrEncode(bytes,&packet);
  mulTwrPacket_t decoded; TEST_ASSERT_TRUE(mulTwrDecode(bytes,sizeof(bytes),&decoded));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,decoded.id); TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK,decoded.stamps[0]);
  mulTestPacket_t broadcast; TEST_ASSERT_FALSE(mulDecodeTestPacket(bytes,sizeof(bytes),&broadcast));
  TEST_ASSERT_FALSE(mulDecodeTestPacket(bytes,MUL_TEST_FRAME_LENGTH,&broadcast)); // Even a truncated TWR header.
  for (unsigned int length=0;length<200;length++) {
    if (length != sizeof(bytes)) {
      TEST_ASSERT_FALSE(mulTwrDecode(bytes,length,&decoded));
    }
  }
  const unsigned int fields[]={0,1,2,3,4,5,12,13,20,21,23,24,25,34,64};
  for (unsigned int i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
    bytes[fields[i]]^=0x80; TEST_ASSERT_FALSE(mulTwrDecode(bytes,sizeof(bytes),&decoded)); bytes[fields[i]]^=0x80;
  }
  TEST_ASSERT_FALSE(mulTwrDecode(NULL,sizeof(bytes),&decoded));
  TEST_ASSERT_FALSE(mulTwrDecode(bytes,sizeof(bytes),NULL));
  mulEncodeTestPacket(bytes,identities[0],5,0); TEST_ASSERT_FALSE(mulTwrDecode(bytes,34,&decoded));
}

void test_mulTwrMissingResponseAbsoluteDeadlineAndLatePollRecovery(void) {
  scenario(5000,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  int64_t when=sendTime(0); advance(when+MS); radios[0].armed=false; dispatch(0,eventPacketSent);
  advance(when+10*MS); dispatch(0,eventTimeout); TEST_ASSERT_TRUE(ctx(0)->active);
  uint64_t deadline=ctx(0)->deadline;
  advance(when+51*MS); dispatch(0,eventTimeout);
  TEST_ASSERT_FALSE(ctx(0)->active); TEST_ASSERT_TRUE(radios[0].receiving);
  TEST_ASSERT_EQUAL_UINT64(deadline,ctx(0)->deadline);
  TEST_ASSERT_FALSE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  ctx(0)->attempted=false; advance(649*MS);
  TEST_ASSERT_FALSE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  TEST_ASSERT_FALSE(radios[0].armed); TEST_ASSERT_TRUE(radios[0].receiving);
}

void test_mulTwrDelayedStartDxReadbackAndTxTimestampFailuresNeverPublish(void) {
  for (unsigned int which=0;which<5;which++) {
    setUp(); scenario(0,1000);
    if (which==0) radios[0].injectStatus=0x8;
    if (which==1) radios[0].injectStatus=0x400;
    if (which==2) radios[0].dxError=512;
    if (which==3) radios[0].setupLag=18*MS;
    if (which==4) radios[0].stampError=1;
    bool started=mulTwrStart(ctx(0),&radios[0].device,5,origins[0]);
    if (which==4) {
      TEST_ASSERT_TRUE(started); advance(sendTime(0)+MS); radios[0].armed=false; dispatch(0,eventPacketSent);
    } else TEST_ASSERT_FALSE(started);
    TEST_ASSERT_FALSE(ctx(0)->active); TEST_ASSERT_FALSE(ctx(1)->delayValid);
    TEST_ASSERT_TRUE(radios[0].receiving); TEST_ASSERT_FALSE(radios[0].armed);
  }
}

void test_mulTwrMismatchedStaleMalformedAndDuplicateMessagesCancelSafely(void) {
  for (unsigned int which=0;which<5;which++) {
    setUp(); scenario(-10000,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
    transfer(0); // Responder now has one scheduled response, never a second response to a replay.
    int64_t when=sendTime(1); advance(when+MS); radios[1].armed=false; dispatch(1,eventPacketSent);
    advance(when+2*MS); memcpy(radios[0].rx,radios[1].tx,MUL_TWR_FRAME_LENGTH);
    radios[0].rxLength=MUL_TWR_FRAME_LENGTH; radios[0].arrival=clockAt(0,when+flight); radios[0].receiving=false;
    if (which==0) { radios[0].rx[25]++; radios[0].rx[2]++; } // Wrong exchange, valid MAC sequence.
    if (which==1) radios[0].rx[29]++; // Wrong POLL nonce, same exchange id.
    if (which==2) radios[0].rx[13]^=1; // Wrong full identity.
    if (which==3) radios[0].rx[21]^=1;
    if (which==4) radios[0].arrival=mulTimestampMask(radios[0].arrival-100*MS); // Failed/stale RX capture.
    dispatch(0,eventPacketReceived);
    TEST_ASSERT_FALSE(ctx(0)->active); TEST_ASSERT_FALSE(ctx(1)->delayValid);
    TEST_ASSERT_TRUE(radios[0].receiving);
  }
}

void test_mulTwrResultMustConfirmActualSixMarkersBeforeDelayAcceptance(void) {
  scenario(5000,2000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  transfer(0); transfer(1); transfer(0); transfer(1);
  TEST_ASSERT_FALSE(ctx(1)->delayValid); // A REPORT transmission alone cannot validate initiator TX stamps.
  radios[0].tx[64] ^= 1; // Wrong ToF result despite matching exchange timestamps.
  transfer(0);
  TEST_ASSERT_FALSE(ctx(1)->active); TEST_ASSERT_FALSE(ctx(1)->delayValid);
  TEST_ASSERT_TRUE(radios[1].receiving);
}

void test_mulTwrExpirationLossEpochAndHardWindowInvalidateCorrection(void) {
  scenario(-10000,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  completeExchange(); mulTwrSync(ctx(1),&model,ticks,false); TEST_ASSERT_TRUE(ctx(1)->syncAvailable);
  uint32_t expiry=ctx(1)->measuredMs+MUL_PROP_MAX_AGE_MS+1;
  mulTwrSync(ctx(1),&model,expiry,false); TEST_ASSERT_FALSE(ctx(1)->syncAvailable);
  mulClockReset(&model); mulClockDelay_t delay;
  TEST_ASSERT_FALSE(mulTwrDelay(ctx(1),&model,&delay));
  setUp(); scenario(0,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  advance(801*MS); dispatch(0,eventTimeout);
  TEST_ASSERT_FALSE(ctx(0)->active); TEST_ASSERT_FALSE(radios[0].armed); TEST_ASSERT_TRUE(radios[0].receiving);
}

static void injectBroadcast(int receiver, int senderNode, uint32_t seq, uint64_t tx, uint64_t rx) {
  FakeRadio *r=&radios[receiver]; mulEncodeTestPacket(r->rx,identities[senderNode],seq,tx);
  r->rxLength=MUL_TEST_FRAME_LENGTH; r->arrival=rx; r->receiving=false;
  dispatch(receiver,eventPacketReceived);
}

static void modeFollower(void) {
  useAlr[1]=true; uwbConfig_t configuration={0}; memcpy(configuration.address,identities[1],8);
  mulAlrAlgorithm.init(&configuration,&radios[1].device);
  for (unsigned int i=0;i<6;i++) {
    int64_t at=-(int64_t)(5-i)*SEC; advance(at+flight+MS);
    injectBroadcast(1,0,i,clockAt(0,at),clockAt(1,at+flight));
    TEST_ASSERT_TRUE(flags.tx); TEST_ASSERT_FALSE(ranging.active);
    advance(sendTime(1)+MS); radios[1].armed=false; dispatch(1,eventPacketSent);
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid,followerClock.status);
}

void test_mulTwrLiveFollowerIntegrationBroadcastPriorityAvailabilityAndLoss(void) {
  scenario(5000,2131); modeFollower();
  advance(630*MS); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  completeExchange(); TEST_ASSERT_TRUE(ranging.delayValid); TEST_ASSERT_TRUE(ranging.syncAvailable);
  TEST_ASSERT_TRUE(radios[1].receiving); TEST_ASSERT_FALSE(flags.tx);
  // Next reference broadcast remains the only source of follower scheduling.
  advance(SEC+flight+MS); injectBroadcast(1,0,6,clockAt(0,SEC),clockAt(1,SEC+flight));
  TEST_ASSERT_TRUE(flags.tx); TEST_ASSERT_FALSE(ranging.active);
  mulRelation_t relation; mulTestPacket_t extended;
  TEST_ASSERT_EQUAL_UINT(MUL_RELATION_FRAME_LENGTH,radios[1].txLength);
  TEST_ASSERT_TRUE(mulBroadcastDecode(radios[1].tx,radios[1].txLength,&extended,&relation));
  TEST_ASSERT_TRUE(relation.available); TEST_ASSERT_EQUAL_UINT32(6,relation.referenceSequence);
  TEST_ASSERT_EQUAL_UINT64(radios[1].actual,extended.txTimestamp);
  TEST_ASSERT_TRUE(relation.delayAgeMs>=350+MUL_FOLLOWER_SLOT_MS && relation.remainingMs<=MUL_PROP_MAX_AGE_MS-350-MUL_FOLLOWER_SLOT_MS);
  uint64_t raw=radios[1].raw; unsigned int sends=radios[1].sends;
  // A spurious ranging RX while the mandatory TX is armed must never schedule another TX.
  radios[1].rxLength=MUL_TWR_FRAME_LENGTH;
  dispatch(1,eventPacketReceived);
  TEST_ASSERT_EQUAL_UINT(sends,radios[1].sends); TEST_ASSERT_EQUAL_UINT64(raw,radios[1].raw);
  radios[1].rxLength=68; // Legacy TWR IRQ is still lower priority than TDMA.
  dispatch(1,eventPacketReceived);
  TEST_ASSERT_EQUAL_UINT(sends,radios[1].sends); TEST_ASSERT_EQUAL_UINT64(raw,radios[1].raw);
  TEST_ASSERT_TRUE(flags.tx); TEST_ASSERT_TRUE(radios[1].armed);
  TEST_ASSERT_FALSE(ranging.active);
  advance(4*SEC+MS); dispatch(1,eventTimeout);
  TEST_ASSERT_FALSE(referenceAcquired); TEST_ASSERT_FALSE(ranging.delayValid); TEST_ASSERT_FALSE(ranging.syncAvailable);
  TEST_ASSERT_FALSE(radios[1].armed); TEST_ASSERT_TRUE(radios[1].receiving);
}

void test_mulTwrReferenceModeGatesOnFollowerAndPreservesNextBroadcast(void) {
  skew=-10000; flight=1066; useAlr[0]=true;
  uwbConfig_t configuration={0}; memcpy(configuration.address,identities[0],8);
  advance(-5*(int64_t)SEC-20*(int64_t)MS); mulAlrAlgorithm.init(&configuration,&radios[0].device);
  for (unsigned int i=0;i<6;i++) {
    if (i != 0) advance(framePhysical+(int64_t)SEC-20*(int64_t)MS);
    dispatch(0,eventTimeout); TEST_ASSERT_TRUE(flags.tx);
    int64_t when=sendTime(0); advance(when+MS); radios[0].armed=false; dispatch(0,eventPacketSent);
    framePhysical=when; TEST_ASSERT_EQUAL_UINT32(i,pendingSequence);
    advance(when+MUL_FOLLOWER_SLOT_DTU+flight);
    injectBroadcast(0,1,100+i,clockAt(1,when+MUL_FOLLOWER_SLOT_DTU),clockAt(0,when+MUL_FOLLOWER_SLOT_DTU+flight));
  }
  origins[0]=pendingTimestamp; origins[1]=clockAt(1,framePhysical+flight); modelAcquire();
  mulRxInit(&radios[1].device);
  advance(framePhysical+630*MS); dispatch(0,eventTimeout);
  TEST_ASSERT_TRUE(ranging.active); TEST_ASSERT_FALSE(flags.tx);
  completeExchange(); TEST_ASSERT_TRUE(contexts[1].delayValid);
  TEST_ASSERT_FALSE(ranging.active); TEST_ASSERT_EQUAL_UINT32(5,pendingSequence);
  advance(framePhysical+(int64_t)SEC-20*(int64_t)MS); dispatch(0,eventTimeout);
  TEST_ASSERT_TRUE(flags.tx); TEST_ASSERT_FALSE(ranging.active);
  mulTestPacket_t next; TEST_ASSERT_TRUE(mulDecodeTestPacket(radios[0].tx,34,&next));
  TEST_ASSERT_EQUAL_UINT32(6,next.sequence);
  TEST_ASSERT_EQUAL_INT64(SEC,mulTimestampDifference(next.txTimestamp,origins[0]));
}

void test_mulTwrRlrRemainsPassiveAndCannotDecodeDirectedRangingAsBroadcast(void) {
  scenario(0,1000); uwbConfig_t configuration={0};
  mulRlrAlgorithm.init(&configuration,&radios[1].device);
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  memcpy(radios[1].rx,radios[0].tx,MUL_TWR_FRAME_LENGTH); radios[1].rxLength=MUL_TWR_FRAME_LENGTH;
  radios[1].receiving=false; mulRlrAlgorithm.onEvent(&radios[1].device,eventPacketReceived);
  TEST_ASSERT_TRUE(radios[1].receiving); TEST_ASSERT_EQUAL_UINT(0,radios[1].sends);
  TEST_ASSERT_FALSE(rlrObservations[0].source.hasSequence); TEST_ASSERT_FALSE(rlrObservations[1].source.hasSequence);
}

void test_mulTwrDuplicatePollAndCompletionNeverCreateSecondReply(void) {
  scenario(0,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  uint8_t poll[MUL_TWR_FRAME_LENGTH]; memcpy(poll,radios[0].tx,sizeof(poll));
  completeExchange(); unsigned int sends=radios[1].sends;
  advance(740*MS); memcpy(radios[1].rx,poll,sizeof(poll)); radios[1].rxLength=sizeof(poll);
  radios[1].arrival=clockAt(1,650*MS+flight); radios[1].receiving=false;
  dispatch(1,eventPacketReceived);
  TEST_ASSERT_EQUAL_UINT(sends,radios[1].sends); TEST_ASSERT_FALSE(ctx(1)->active);
  dispatch(1,eventPacketSent); TEST_ASSERT_EQUAL_UINT(sends,radios[1].sends);
  TEST_ASSERT_TRUE(radios[1].receiving);
}

void test_mulTwrUnverifiedInitiatorFinalCannotPublishEvenIfReportArrives(void) {
  scenario(-10000,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  transfer(0); radios[0].stampError=1; transfer(1); // FINAL's actual TX now disagrees with encoding.
  transfer(0); TEST_ASSERT_FALSE(ctx(0)->active); TEST_ASSERT_TRUE(ctx(1)->active);
  TEST_ASSERT_FALSE(ctx(1)->delayValid);
  if (radios[1].armed) transfer(1); // Initiator refuses to confirm the unverified FINAL.
  advance(790*MS); dispatch(1,eventTimeout);
  TEST_ASSERT_FALSE(ctx(1)->active); TEST_ASSERT_FALSE(ctx(1)->delayValid);
  TEST_ASSERT_TRUE(radios[0].receiving && radios[1].receiving);
}

void test_mulTwrReceiveErrorsAndExplicitCancellationRestoreRx(void) {
  const uwbEvent_t errors[]={eventReceiveFailed,eventReceiveTimeout};
  for (unsigned int i=0;i<2;i++) {
    setUp(); scenario(0,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
    transfer(0); dispatch(1,errors[i]);
    TEST_ASSERT_FALSE(ctx(1)->active); TEST_ASSERT_FALSE(radios[1].armed); TEST_ASSERT_TRUE(radios[1].receiving);
    mulTwrCancel(ctx(0),&radios[0].device,"test_cancel",true);
    TEST_ASSERT_FALSE(ctx(0)->active); TEST_ASSERT_TRUE(radios[0].receiving);
  }
}

void test_mulTwrFollowerNewBroadcastPreemptsExchangeAndReacquiresAfterLoss(void) {
  scenario(5000,2131); modeFollower(); advance(630*MS);
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  transfer(0); // Follower response armed, still lower priority than a new broadcast.
  int64_t when=sendTime(1); advance(when+MS); radios[1].armed=false; dispatch(1,eventPacketSent);
  TEST_ASSERT_TRUE(ranging.active); TEST_ASSERT_FALSE(flags.tx);
  advance(SEC+flight+MS); injectBroadcast(1,0,6,clockAt(0,SEC),clockAt(1,SEC+flight));
  TEST_ASSERT_FALSE(ranging.active); TEST_ASSERT_TRUE(flags.tx); TEST_ASSERT_FALSE(ranging.delayValid);
  advance(sendTime(1)+MS); radios[1].armed=false; dispatch(1,eventPacketSent);
  advance(4*SEC+MS); dispatch(1,eventTimeout); TEST_ASSERT_FALSE(referenceAcquired);
  uint64_t oldEpoch=followerClock.epoch;
  advance(5*SEC+flight+MS); injectBroadcast(1,0,0,clockAt(0,5*SEC),clockAt(1,5*SEC+flight));
  TEST_ASSERT_FALSE(referenceAcquired);TEST_ASSERT_FALSE(flags.tx);
  advance(6*SEC+flight+MS);injectBroadcast(1,0,1,clockAt(0,6*SEC),clockAt(1,6*SEC+flight));
  TEST_ASSERT_TRUE(referenceAcquired); TEST_ASSERT_TRUE(flags.tx);
  TEST_ASSERT_EQUAL_INT(mulClockWarmup,followerClock.status);
  TEST_ASSERT_TRUE(followerClock.epoch != oldEpoch); // Confirmed restart owns a new epoch.
  TEST_ASSERT_FALSE(ranging.delayValid);
}

void test_mulTwrReferenceHardBroadcastPriorityCancelsMissingResponse(void) {
  useAlr[0]=true; uwbConfig_t configuration={0}; memcpy(configuration.address,identities[0],8);
  advance(-20*(int64_t)MS); mulAlrAlgorithm.init(&configuration,&radios[0].device);
  dispatch(0,eventTimeout); advance(sendTime(0)+MS); radios[0].armed=false; dispatch(0,eventPacketSent);
  uint64_t previous=pendingTimestamp;
  // Simulate a pending exchange whose IRQ/timeout was delayed until mandatory slot preparation.
  ranging.active=true; ranging.txPending=true; ranging.id=0; ranging.stage=mulTwrPoll;
  ranging.deadline=mulTimestampMask(nextRawTx+100*MS); radios[0].armed=true;
  advance(physical+979*MS); dispatch(0,eventTimeout);
  TEST_ASSERT_FALSE(ranging.active); TEST_ASSERT_TRUE(flags.tx);
  mulTestPacket_t next; TEST_ASSERT_TRUE(mulDecodeTestPacket(radios[0].tx,34,&next));
  TEST_ASSERT_EQUAL_INT64(SEC,mulTimestampDifference(next.txTimestamp,previous));
}

void test_mulTwrLiveFollowerDelayExpiresBetweenFiveSecondAttempts(void) {
  if (MUL_PROP_MAX_AGE_MS>1000) return; // Continuous-cycle expiry covered separately.
  scenario(-10000,1066); modeFollower(); advance(630*MS);
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  completeExchange(); TEST_ASSERT_TRUE(ranging.syncAvailable);
  advance(SEC+flight+MS); injectBroadcast(1,0,6,clockAt(0,SEC),clockAt(1,SEC+flight));
  advance(sendTime(1)+MS); radios[1].armed=false; uint32_t wait=dispatch(1,eventPacketSent);
  TEST_ASSERT_TRUE(wait < 700-MUL_FOLLOWER_SLOT_MS); // Delay expiry, not the next reference-loss watchdog.
  advance(physical+(int64_t)wait*MS); dispatch(1,eventTimeout);
  TEST_ASSERT_FALSE(ranging.syncAvailable); TEST_ASSERT_TRUE(referenceAcquired);
  TEST_ASSERT_FALSE(ranging.active); TEST_ASSERT_FALSE(flags.tx); TEST_ASSERT_TRUE(radios[1].receiving);
}

void test_mulTwrExchangeIdAndFollowerHardwareCounterWrap(void) {
  offsets[1]=MUL_TIMESTAMP_MASK-670*MS;
  scenario(-10000,2131); exchangeSequence=UINT32_MAX; modelAcquire();
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,exchangeSequence,origins[0]));
  completeExchange(); TEST_ASSERT_TRUE(ctx(1)->delayValid);
  TEST_ASSERT_UINT32_WITHIN(1,2131,ctx(1)->tof);
  TEST_ASSERT_TRUE(ctx(1)->stamps[2] > MUL_TIMESTAMP_MASK-10*MS);
  TEST_ASSERT_TRUE(ctx(1)->stamps[5] < 30*MS);
  mulClockDelay_t delay; TEST_ASSERT_TRUE(mulTwrDelay(ctx(1),&model,&delay));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,delay.referenceSequence);
  mulTestPacket_t next; memcpy(next.source,identities[0],8); next.sequence=0;
  next.txTimestamp=clockAt(0,SEC);
  TEST_ASSERT_EQUAL_INT(mulClockSample,mulClockObserve(&model,&next,clockAt(1,SEC+flight),6000,true));
  TEST_ASSERT_EQUAL_INT(mulClockDelayValid,mulClockDelayStatus(&model,&delay,6000));
}

void test_mulTwrReferenceLossWhileResponseArmedCancelsAndRequiresNewEpoch(void) {
  scenario(5000,2131); modeFollower(); advance(630*MS);
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  transfer(0); TEST_ASSERT_TRUE(ranging.active); TEST_ASSERT_TRUE(radios[1].armed);
  uint64_t epoch=followerClock.epoch;
  ticks=lastReferenceTick+pdMS_TO_TICKS(MUL_SOURCE_TIMEOUT_MS);
  dispatch(1,eventTimeout);
  TEST_ASSERT_FALSE(referenceAcquired); TEST_ASSERT_FALSE(ranging.active);
  TEST_ASSERT_FALSE(radios[1].armed); TEST_ASSERT_TRUE(radios[1].receiving);
  TEST_ASSERT_FALSE(ranging.delayValid); TEST_ASSERT_TRUE(epoch != followerClock.epoch);
}

// Analog path is independent of register compensation. TX_STAMP = clock at
// antenna emission - physical TX delay + TX_ANTD; RX_STAMP has the opposite sign.
static void analogMarkers(uint64_t t[6], int64_t tof, const uint16_t tx[2],
                          const uint16_t rx[2], uint16_t configuredTx, uint16_t configuredRx) {
  int64_t p1=650*MS, p3=p1+tof+20*MS, p5=p3+tof+20*MS;
  t[0]=mulTimestampMask(clockAt(0,p1)-tx[0]+configuredTx);
  t[1]=mulTimestampMask(clockAt(1,p1+tof)+rx[1]-configuredRx);
  t[2]=mulTimestampMask(clockAt(1,p3)-tx[1]+configuredTx);
  t[3]=mulTimestampMask(clockAt(0,p3+tof)+rx[0]-configuredRx);
  t[4]=mulTimestampMask(clockAt(0,p5)-tx[0]+configuredTx);
  t[5]=mulTimestampMask(clockAt(1,p5+tof)+rx[1]-configuredRx);
}

void test_mulTwrAnalogDelayRegressionReproduces33300WithoutCalibration(void) {
  const uint16_t tx[]={14498,14498}, rx[]={18453,18453};
  uint64_t markers[6]; uint32_t tof;
  analogMarkers(markers,349,tx,rx,0,0);
  TEST_ASSERT_TRUE(mulTwrCalculate(markers,&tof));
  TEST_ASSERT_EQUAL_UINT32(33300,tof); // 156.236 m, despite 1.637 m physical path.
  analogMarkers(markers,349,tx,rx,MUL_ANTENNA_TX_DTU,MUL_ANTENNA_RX_DTU);
  TEST_ASSERT_TRUE(mulTwrCalculate(markers,&tof));
  TEST_ASSERT_EQUAL_UINT32(349,tof); // No ToF subtraction: only register semantics changed.
}

void test_mulTwrAnalogSymmetricAndAsymmetricAllocationsSkewAndWrap(void) {
  const uint16_t tx[][2]={{16476,16476},{14498,14498},{14000,16000}};
  const uint16_t rx[][2]={{16476,16476},{18453,18453},{18951,16951}};
  const int32_t rates[]={0,-150000,150000};
  const uint32_t metres[]={0,1,5,10,30};
  for (unsigned int a=0;a<3;a++) for (unsigned int b=0;b<3;b++)
    for (unsigned int c=0;c<5;c++) {
      if (c == 0 && b != 0) continue;
      skew=rates[b]; int64_t expected=(metres[c]*SEC+149896229)/299792458;
      uint64_t markers[6]; uint32_t tof;
      analogMarkers(markers,expected,tx[a],rx[a],
        a == 0 ? 16476 : MUL_ANTENNA_TX_DTU, a == 0 ? 16476 : MUL_ANTENNA_RX_DTU);
      TEST_ASSERT_TRUE(mulTwrCalculate(markers,&tof));
      TEST_ASSERT_UINT32_WITHIN(1,expected,tof);
    }
}

void test_mulTwrChangedCalibrationDuringExchangeInvalidatesAndRecovers(void) {
  for (unsigned int i=0;i<3;i++) {
    setUp(); scenario(0,1000);
    TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
    transfer(0);
    ctx(1)->delayValid=true;
    if (i==0) radios[1].rxAntenna=0;
    if (i==1) radios[1].antenna++;
    if (i==2) radios[1].device.antennaDelay.full=0;
    dispatch(1,eventPacketSent);
    TEST_ASSERT_FALSE(ctx(1)->active); TEST_ASSERT_FALSE(ctx(1)->delayValid);
    TEST_ASSERT_FALSE(radios[1].armed); TEST_ASSERT_TRUE(radios[1].receiving);
    radios[1].antenna=MUL_ANTENNA_TX_DTU; radios[1].rxAntenna=MUL_ANTENNA_RX_DTU;
    radios[1].device.antennaDelay.full=MUL_ANTENNA_TX_DTU;
    TEST_ASSERT_TRUE(mulAntennaValid(&radios[1].device));
    mulTwrCancel(ctx(0),&radios[0].device,NULL,true);
    mulTwrInit(ctx(0)); mulTwrInit(ctx(1)); scenario(0,1000);
    TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
    completeExchange(); TEST_ASSERT_TRUE(ctx(1)->delayValid);
    TEST_ASSERT_UINT32_WITHIN(1,1000,ctx(1)->tof);
  }
}

void test_mulTwrFullExchangeWithUnequalPhysicalTxRxAllocation(void) {
  const int32_t rates[]={-150000,0,150000};
  for (unsigned int i=0;i<3;i++) {
    setUp(); scenario(rates[i],1066);
    // Each device has the same aggregate as the estimate but a distinct split.
    // TWR can validate aggregate delay; this cannot validate individual directions.
    radios[0].physicalTx=16475; radios[0].physicalRx=16476;
    radios[1].physicalTx=14000; radios[1].physicalRx=18951;
    TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
    completeExchange(); TEST_ASSERT_TRUE(ctx(1)->delayValid);
    TEST_ASSERT_UINT32_WITHIN(1,1066,ctx(1)->tof);
  }
}

void test_mulTwrReceiverOwnedPowerCorrectionPreservesRawAndNeverCorrectsTxOrEcho(void) {
  scenario(5000,2131);
  radios[1].powerDbm=-80; radios[1].powerDelta=-10; radios[1].rxBiasDtu=10;
  radios[0].powerDbm=-75; radios[0].powerDelta=5; radios[0].rxBiasDtu=-5;
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  transfer(0); // P2, follower ownership.
  TEST_ASSERT_EQUAL_INT(-10,ctx(1)->power[0].deltaDtu);
  transfer(1); // P4, reference ownership.
  TEST_ASSERT_EQUAL_INT(5,ctx(0)->power[1].deltaDtu);
  radios[1].powerDelta=-20; radios[1].rxBiasDtu=20;
  transfer(0); // P6, follower ownership, different correction from P2.
  transfer(1); transfer(0);
  TEST_ASSERT_TRUE(ctx(1)->delayValid); TEST_ASSERT_TRUE(ctx(1)->powerCorrected);
  TEST_ASSERT_EQUAL_UINT(1,radios[0].corrections); TEST_ASSERT_EQUAL_UINT(2,radios[1].corrections);
  TEST_ASSERT_UINT32_WITHIN(1,2131,ctx(1)->tof);
  TEST_ASSERT_TRUE(ctx(1)->rawTof != ctx(1)->tof);
  TEST_ASSERT_EQUAL_UINT32(ctx(0)->rawTof,ctx(1)->rawTof);
  TEST_ASSERT_EQUAL_INT(-20,ctx(0)->power[2].deltaDtu);
  uint64_t raw[6]; memcpy(raw,ctx(1)->stamps,sizeof(raw));
  for (unsigned int i=0;i<3;i++) raw[2*i+1]=mulTimestampMask(raw[2*i+1]-ctx(1)->power[i].deltaDtu);
  uint32_t check; TEST_ASSERT_TRUE(mulTwrCalculate(raw,&check));
  TEST_ASSERT_EQUAL_UINT32(check,ctx(1)->rawTof);
}

void test_mulTwrMissingPowerKeepsRawResultWithoutClaimingFullCorrection(void) {
  scenario(-10000,1066);
  radios[1].powerDbm=-80; radios[1].powerDelta=-10; radios[1].rxBiasDtu=10;
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  completeExchange();
  TEST_ASSERT_TRUE(ctx(1)->delayValid); TEST_ASSERT_FALSE(ctx(1)->powerCorrected);
  TEST_ASSERT_EQUAL_UINT32(ctx(1)->rawTof,ctx(1)->tof);
  TEST_ASSERT_EQUAL_INT(mulPowerInvalid,ctx(1)->power[1].status);
  TEST_ASSERT_EQUAL_INT(mulPowerApplied,ctx(1)->power[0].status);
}

void test_mulTwrPowerMetadataStrictVersionBoundsAndMissing(void) {
  mulTwrPacket_t p={.type=mulTwrReport,.id=5};
  uint8_t frame[MUL_TWR_FRAME_LENGTH]; mulTwrEncode(frame,&p);
  mulTwrPacket_t decoded; TEST_ASSERT_TRUE(mulTwrDecode(frame,sizeof(frame),&decoded));
  TEST_ASSERT_EQUAL_INT(mulPowerMissing,decoded.power[0].status);
  TEST_ASSERT_FALSE(mulTwrDecode(frame,68,&decoded)); // Legacy format is explicitly incompatible.
  const unsigned int bad[]={68,69,71,72,73};
  for (unsigned int i=0;i<5;i++) {
    uint8_t saved=frame[bad[i]]; frame[bad[i]]=255;
    TEST_ASSERT_FALSE(mulTwrDecode(frame,sizeof(frame),&decoded)); frame[bad[i]]=saved;
  }
  p.power[0]=(mulPower_t){.status=mulPowerApplied,.table=2,.powerCentiDbm=-8000,.deltaDtu=-5};
  mulTwrEncode(frame,&p); TEST_ASSERT_TRUE(mulTwrDecode(frame,sizeof(frame),&decoded));
  TEST_ASSERT_EQUAL_INT(-8000,decoded.power[0].powerCentiDbm);
  TEST_ASSERT_EQUAL_INT(-5,decoded.power[0].deltaDtu);
  p.type=mulTwrPoll; mulTwrEncode(frame,&p);
  TEST_ASSERT_FALSE(mulTwrDecode(frame,sizeof(frame),&decoded)); // Future/receiver metadata forbidden.
}

void test_mulTwrAlteredEchoPowerMetadataCancelsWithoutPublication(void) {
  scenario(0,1066); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  transfer(0); transfer(1); transfer(0);
  // REPORT carries altered P4 metadata despite identical six timestamps.
  radios[1].tx[69+5+3]=mulPowerMissing;
  radios[1].tx[69+5]=radios[1].tx[69+5+1]=radios[1].tx[69+5+4]=0;
  transfer(1); TEST_ASSERT_FALSE(ctx(0)->active); TEST_ASSERT_FALSE(ctx(1)->delayValid);
}

void test_mulTwrOptionalCorrectionCannotInvalidateValidRawNearZeroRange(void) {
  uint64_t t[]={0,1,20*MS+1,20*MS+2,40*MS+2,40*MS+3};
  mulPower_t power[3];
  for (unsigned int i=0;i<3;i++) {
    power[i]=(mulPower_t){.status=mulPowerApplied,.table=2,.powerCentiDbm=-8000,.deltaDtu=-5};
    t[2*i+1]=mulTimestampMask(t[2*i+1]-5);
  }
  uint32_t raw,corrected;
  TEST_ASSERT_TRUE(mulTwrResults(t,power,&raw,&corrected));
  TEST_ASSERT_EQUAL_UINT32(1,raw); TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,corrected);
}

void test_mulTwrStalledExchangeOverlappingFollowerSlotLosesToFreshBroadcast(void) {
  scenario(5000,2131); modeFollower();
  // Fault injection: an old exchange stalled across the next reference, with
  // its delayed message targeting the nominal follower slot. Normal TWR ends
  // by 800 ms; this deliberately violates that timing to test hard priority.
  ranging.active=true; ranging.txPending=true; ranging.stage=mulTwrResponse;
  ranging.id=5; radios[1].armed=true;
  ranging.expectedTx=clockAt(1,SEC+flight+MUL_FOLLOWER_SLOT_DTU);
  ranging.deadline=mulTimestampMask(ranging.expectedTx+50*MS);
  advance(SEC+flight+MS);
  injectBroadcast(1,0,6,clockAt(0,SEC),clockAt(1,SEC+flight));
  TEST_ASSERT_FALSE(ranging.active); TEST_ASSERT_TRUE(flags.tx);
  TEST_ASSERT_TRUE(radios[1].armed);
  TEST_ASSERT_EQUAL_UINT(MUL_RELATION_FRAME_LENGTH,radios[1].txLength);
  mulTestPacket_t packet; mulRelation_t metadata;
  TEST_ASSERT_TRUE(mulBroadcastDecode(radios[1].tx,radios[1].txLength,&packet,&metadata));
  int64_t slot=mulTimestampDifference(packet.txTimestamp,clockAt(1,SEC+flight));
  TEST_ASSERT_INT64_WITHIN(511,MUL_FOLLOWER_SLOT_DTU,slot);
  advance(sendTime(1)+MS); radios[1].armed=false; dispatch(1,eventPacketSent);
  TEST_ASSERT_EQUAL_UINT64(packet.txTimestamp,radios[1].actual);
  TEST_ASSERT_TRUE(radios[1].receiving);
}

static void beginRefresh(void) {
  framePhysical=5*SEC; exchangeSequence=10;
  origins[0]=clockAt(0,framePhysical); origins[1]=clockAt(1,framePhysical+flight);
  model.sequence=10; model.referenceTx=origins[0]; model.localRx=origins[1]; model.observedMs=10000;
  advance(framePhysical+630*MS);
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,10,origins[0]));
}
void test_mulTwrMissedRefreshRetainsOnlyOriginalCommittedEstimate(void) {
  scenario(0,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  completeExchange(); uint32_t original=ctx(1)->measuredMs;
  beginRefresh(); transfer(0); // New POLL must not overwrite committed time/id/tof.
  TEST_ASSERT_EQUAL_UINT32(original,ctx(1)->measuredMs); TEST_ASSERT_EQUAL_UINT32(5,ctx(1)->estimateId);
  TEST_ASSERT_TRUE(ctx(1)->delayValid);
  mulTwrCancel(ctx(1),&radios[1].device,"timeout",false);
  mulClockDelay_t old; TEST_ASSERT_TRUE(mulTwrDelay(ctx(1),&model,&old));
  TEST_ASSERT_EQUAL_UINT32(original,old.measuredMs); TEST_ASSERT_EQUAL_UINT32(5,old.referenceSequence);
  TEST_ASSERT_EQUAL_INT(MUL_PROP_MAX_AGE_MS>1000 ? mulPropagationReused : mulPropagationExpired,
    mulTwrPropagationState(ctx(1),&model,ticks));
}
void test_mulTwrMalformedRefreshInvalidatesPreviouslyConfirmedEstimate(void) {
  scenario(0,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  completeExchange(); beginRefresh();
  transfer(0); transfer(1); transfer(0); transfer(1);
  radios[0].tx[64]^=1; transfer(0);
  TEST_ASSERT_FALSE(ctx(1)->delayValid);
  TEST_ASSERT_EQUAL_INT(mulPropagationInvalidated,mulTwrPropagationState(ctx(1),&model,ticks));
}
void test_mulTwrSuccessfulRefreshCommitsNewAcquisitionTimeAndIdentifier(void) {
  scenario(0,1000); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  completeExchange(); uint32_t original=ctx(1)->measuredMs;
  beginRefresh(); completeExchange();
  TEST_ASSERT_TRUE(ctx(1)->delayValid); TEST_ASSERT_EQUAL_UINT32(10,ctx(1)->estimateId);
  TEST_ASSERT_UINT32_WITHIN(1,original+5000,ctx(1)->measuredMs);
  TEST_ASSERT_EQUAL_INT(mulPropagationFresh,mulTwrPropagationState(ctx(1),&model,ticks));
}

// Three virtual nodes use the real role handlers and wire frames. Only the
// mode-exclusive scratch is swapped as each node runs; no production hooks.
static mulRoleIo_t receiverIo;
static void receiveRlr(mulRoleScratch_t *receiver,const uint8_t *frame,unsigned int length,uint64_t stamp) {
  mulRoleScratch_t follower=mulRoleScratch;mulRoleIo_t followerIo=mulRoleIo;mulRoleIo=receiverIo; mulRoleScratch=*receiver;
  memcpy(radios[2].rx,frame,length); radios[2].rxLength=length; radios[2].arrival=stamp;
  radios[2].receiving=false; mulRlrAlgorithm.onEvent(&radios[2].device,eventPacketReceived);
  *receiver=mulRoleScratch;receiverIo=mulRoleIo;mulRoleIo=followerIo; mulRoleScratch=follower;
  TEST_ASSERT_TRUE(radios[2].receiving); TEST_ASSERT_EQUAL_UINT(0,radios[2].sends);
}
static void continuousCycle(bool moving) {
  scenario(5000,426); modeFollower(); nextSequence=900; // Independent counters.
  mulRoleScratch_t follower=mulRoleScratch;mulRoleIo_t followerIo=mulRoleIo;
  uwbConfig_t receiverConfig={.address={0x86,0,0,0,0,0,0xcf,0xbc}};
  mulRlrAlgorithm.init(&receiverConfig,&radios[2].device);
  mulRoleScratch_t receiver=mulRoleScratch;receiverIo=mulRoleIo;mulRoleIo=followerIo; mulRoleScratch=follower;
  uint32_t previousAge=0,previousMotion=0;
  for (uint32_t sequence=6;sequence<=23;sequence++) {
    int64_t when=(int64_t)(sequence-5)*SEC;
    // Reference stays fixed; follower moves away at ~98.5 mm/s when enabled.
    // Actual change is <= every tested positive bound (500 mm/s).
    if (moving) flight=426+(int64_t)(sequence-5)*21;
    uint8_t reference[34]; mulEncodeTestPacket(reference,identities[0],sequence,clockAt(0,when));
    advance(when+213+MS);
    receiveRlr(&receiver,reference,34,clockAt(2,when+213));
    advance(when+flight+MS);
    injectBroadcast(1,0,sequence,clockAt(0,when),clockAt(1,when+flight));
    TEST_ASSERT_TRUE(flags.tx);
    uint8_t broadcast[74]; memcpy(broadcast,radios[1].tx,74);
    int64_t sent=sendTime(1); advance(sent+MS); radios[1].armed=false; dispatch(1,eventPacketSent);
    TEST_ASSERT_EQUAL_UINT64(mulDecodeTimestamp(broadcast+29),radios[1].actual);
    unsigned int before=observationCount;
    advance(sent+flight-213+MS);
    receiveRlr(&receiver,broadcast,74,clockAt(2,sent+flight-213));
    if (sequence>=7) {
      TEST_ASSERT_EQUAL_UINT(before+1,observationCount);
      TEST_ASSERT_EQUAL_UINT32(sequence,capturedObservation.referenceSequence);
      TEST_ASSERT_EQUAL_UINT32(900+sequence-6,capturedObservation.followerSequence);
      bool expected=sequence>=11 && sequence<=21 &&
        (MUL_PROP_MAX_AGE_MS>1000 || sequence==11 || sequence==16) &&
        MUL_EXPERIMENTAL_STATIONARY_RLR_RATE;
      TEST_ASSERT_EQUAL_INT(expected,capturedObservation.status==mulDownlinkAvailable);
      if (sequence<=10) TEST_ASSERT_EQUAL_INT(mulPropagationNone,capturedObservation.delayState);
      if (expected) {
        if (!moving) TEST_ASSERT_INT32_WITHIN(3,0,capturedObservation.tdoaDtu);
        uint32_t actualAge=capturedObservation.observedMs-ranging.measuredMs;
        TEST_ASSERT_TRUE(capturedObservation.delayAgeMs>=actualAge && capturedObservation.delayAgeMs<=actualAge+4);
        TEST_ASSERT_EQUAL_UINT32(sequence<16 ? 10 : 15,ranging.estimateId);
        if (MUL_PROP_MAX_AGE_MS>1000) {
          TEST_ASSERT_EQUAL_INT(sequence==11 || sequence==16 ? mulPropagationFresh : mulPropagationReused,capturedObservation.delayState);
          if (sequence!=11 && sequence!=16) {
            TEST_ASSERT_UINT32_WITHIN(2,previousAge+1000,capturedObservation.delayAgeMs);
#if MUL_RELATIVE_SPEED_MM_S > 0
            TEST_ASSERT_TRUE(capturedObservation.motionBoundMm>previousMotion);
#endif
          }
          previousAge=capturedObservation.delayAgeMs; previousMotion=capturedObservation.motionBoundMm;
        }
        TEST_ASSERT_TRUE(capturedObservation.unknownComponents & mulQualityTwrUnknown);
      } else {
        TEST_ASSERT_EQUAL_INT32(INT32_MIN,capturedObservation.tdoaDtu);
      }
      if (sequence>=22 && MUL_PROP_MAX_AGE_MS>1000)
        TEST_ASSERT_EQUAL_INT(mulPropagationExpired,capturedObservation.delayState);
    }
    if (sequence==10 || sequence==15 || sequence==20) {
      framePhysical=when; exchangeSequence=sequence;
      origins[0]=clockAt(0,when); origins[1]=clockAt(1,when+flight);
      advance(when+630*MS); TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,sequence,origins[0]));
      if (sequence==20) {
        uint32_t old=ranging.measuredMs; transfer(0); // Missed response/result.
        advance(when+801*MS); dispatch(0,eventTimeout); dispatch(1,eventTimeout);
        TEST_ASSERT_TRUE(ranging.delayValid); TEST_ASSERT_EQUAL_UINT32(old,ranging.measuredMs);
      } else { completeExchange(); TEST_ASSERT_TRUE(ranging.delayValid); }
    }
  }
  TEST_ASSERT_EQUAL_UINT(17,observationCount);
  (void)previousMotion;
}
void test_mulContinuousThreeNodePipelineAcrossTwoTwrUpdatesMissingRefreshAndWrap(void) {
  continuousCycle(false);
}
void test_mulContinuousBoundedSeparationChangeRetainsExplicitAgingBound(void) {
#if MUL_RELATIVE_SPEED_MM_S >= 500
  continuousCycle(true);
#endif
}

/* Production radio adapters, independent clocks, FCS-reserve reads and real
 * multi codecs. Repeat every follower slot rather than assuming adjacent IDs. */
static uwbConfig_t multiConfiguration;
static void multiConfig(unsigned count,unsigned slot,unsigned spacing) {
 memset(&multiConfiguration,0,sizeof(multiConfiguration));
 mulSchedule_t *spec=&multiConfiguration.mulSchedule;simulationSchedule=spec;
 *spec=(mulSchedule_t){.revision=9,.profile=MUL_PROFILE_MULTI,.count=count,.slotMs=spacing,.periodMs=1000};
 for(unsigned i=0;i<count;i++)spec->ids[i]=132+3*i;
 mulNetwork_t pair;TEST_ASSERT_TRUE(mulSchedulePair(spec,slot?slot:1,&pair));
 multiConfiguration.mulNetwork=pair;multiConfiguration.mulConfigured=true;
 multiConfiguration.mulRole=slot?mulAlrFollower:mulAlrReference;
 memcpy(multiConfiguration.address,pair.addresses[slot?1:0],8);
}
static void injectMulti(unsigned receiver,unsigned slot,uint32_t cycle,uint64_t tx,uint64_t rx) {
 FakeRadio *r=&radios[receiver];mulRelation_t unavailable={.version=2,.maxAgeMs=MUL_PROP_MAX_AGE_MS};
 r->rxLength=multiConfiguration.mulPlan?mulPlanBroadcastEncode(r->rx,sizeof(r->rx),multiConfiguration.mulPlan,slot,cycle,tx,slot?&unavailable:NULL):mulMultiBroadcastEncode(r->rx,sizeof(r->rx),&multiConfiguration.mulSchedule,slot,cycle,tx,slot?&unavailable:NULL);
 TEST_ASSERT_EQUAL_UINT(slot?80:40,r->rxLength);r->arrival=rx;r->receiving=false;
 dispatch(receiver,eventPacketReceived);
}
void test_mulMultiFollowerUsesReferenceCycleAndEveryConfiguredSlot(void) {
 const unsigned counts[]={2,3,4,8}, spacings[]={10,50};
 for(unsigned c=0;c<4;c++)for(unsigned t=0;t<2;t++)for(unsigned slot=1;slot<counts[c];slot++) {
  setUp();multiConfig(counts[c],slot,spacings[t]);useAlr[1]=true;advance(MS);
  mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
  uint64_t tx=clockAt(0,0),rx=clockAt(1,500);advance(MS);
  injectMulti(1,0,UINT32_MAX,tx,rx);TEST_ASSERT_TRUE(flags.tx);
  TEST_ASSERT_EQUAL_UINT(80,radios[1].txLength);mulMultiBroadcast_t decoded;
  TEST_ASSERT_TRUE(mulMultiBroadcastDecode(&multiConfiguration.mulSchedule,radios[1].tx,80,&decoded));
  TEST_ASSERT_EQUAL_UINT(slot,decoded.slot);TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,decoded.packet.sequence);
  TEST_ASSERT_INT64_WITHIN(512,(int64_t)slot*spacings[t]*MS,mulTimestampDifference(radios[1].actual,rx));
  advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
  unsigned sends=radios[1].sends;injectMulti(1,0,UINT32_MAX,tx,rx);TEST_ASSERT_EQUAL_UINT(sends,radios[1].sends);
  advance(SEC+MS);injectMulti(1,0,0,clockAt(0,SEC),clockAt(1,SEC+500));TEST_ASSERT_TRUE(flags.tx);
  TEST_ASSERT_EQUAL_UINT32(0,pendingSequence);
 }
}
void test_mulMultiTwrCompletesBothWindowsForAllRosterSizes(void) {
 const unsigned counts[]={2,3,4,8};
 for(unsigned c=0;c<4;c++)for(unsigned cycle=1;cycle<=4;cycle++)for(unsigned window=0;window<2;window++) {
  setUp();multiConfig(counts[c],0,10);mulSchedule_t *spec=&multiConfiguration.mulSchedule;
  unsigned owner=mulScheduleSyncOwner(spec,cycle,window);if(!owner)continue;
  scenario(-10000,2131);exchangeSequence=cycle;
  advance((mulScheduleSyncPollMs(window)-20)*MS);
  TEST_ASSERT_TRUE(mulTwrConfigureSchedule(&contexts[0],spec,owner,window));
  TEST_ASSERT_TRUE(mulTwrConfigureSchedule(&contexts[1],spec,owner,window));
  TEST_ASSERT_TRUE(mulTwrStart(&contexts[0],&radios[0].device,cycle,origins[0]));
  completeExchange();TEST_ASSERT_FALSE(contexts[0].active);TEST_ASSERT_TRUE(contexts[1].delayValid);
  TEST_ASSERT_UINT32_WITHIN(1,flight,contexts[1].tof);
  TEST_ASSERT_EQUAL_UINT(90,radios[0].txLength);
 }
}
void test_mulMultiReferenceVisitsTwoOwnersDespiteFirstWindowFailure(void) {
 setUp();multiConfig(8,0,10);useAlr[0]=true;advance(-20*(int64_t)MS);
 mulAlrAlgorithm.init(&multiConfiguration,&radios[0].device);nextSequence=1;
 dispatch(0,eventTimeout);int64_t origin=sendTime(0);advance(origin+MS);
 radios[0].armed=false;dispatch(0,eventPacketSent);
 for(unsigned slot=1;slot<8;slot++) {
   advance(origin+slot*10*MS+MS);injectMulti(0,slot,1,clockAt(1,origin+slot*10*MS),clockAt(0,origin+slot*10*MS));
 }
 TEST_ASSERT_EQUAL_UINT8(0xfe,followerSeen);
 advance(origin+630*MS);dispatch(0,eventTimeout);TEST_ASSERT_TRUE(ranging.active);
 TEST_ASSERT_EQUAL_UINT(1,ranging.followerSlot);TEST_ASSERT_EQUAL_UINT(0,ranging.syncWindow);
 // No response or completion IRQ: second owner still obtains its own window.
 advance(origin+780*MS);dispatch(0,eventTimeout);TEST_ASSERT_TRUE(ranging.active);
 TEST_ASSERT_EQUAL_UINT(2,ranging.followerSlot);TEST_ASSERT_EQUAL_UINT(1,ranging.syncWindow);
 mulTwrPacket_t packet;unsigned slot;
 TEST_ASSERT_TRUE(mulMultiTwrDecode(&multiConfiguration.mulSchedule,1,radios[0].tx,90,&packet,&slot));
 TEST_ASSERT_EQUAL_UINT(2,slot);
 advance(origin+980*MS);dispatch(0,eventTimeout);TEST_ASSERT_TRUE(flags.tx);TEST_ASSERT_FALSE(ranging.active);
 TEST_ASSERT_EQUAL_UINT(40,radios[0].txLength);
 TEST_ASSERT_EQUAL_UINT32(2,pendingSequence);
 TEST_ASSERT_EQUAL_INT64(SEC,mulTimestampDifference(pendingTimestamp,clockAt(0,origin)));
}
void test_mulMultiFollowerKalmanAndOutlierNeverSuppressScheduledBroadcast(void) {
 multiConfig(4,1,10);mulClockFitDefaults(&multiConfiguration.mulClockConfig);multiConfiguration.mulClockConfig.estimator=2;
 useAlr[1]=true;skew=15000;flight=1000;advance(MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
 for(unsigned i=0;i<50;i++) {
  int64_t at=(int64_t)i*SEC;advance(at+flight+MS);
  uint64_t rx=clockAt(1,at+flight);if(i==35)rx=mulTimestampMask(rx+100000);
  injectMulti(1,0,100+i,clockAt(0,at),rx);
  TEST_ASSERT_TRUE(flags.tx);TEST_ASSERT_EQUAL_UINT(80,radios[1].txLength);
  mulMultiBroadcast_t decoded;TEST_ASSERT_TRUE(mulMultiBroadcastDecode(&multiConfiguration.mulSchedule,radios[1].tx,80,&decoded));
  TEST_ASSERT_EQUAL_UINT32(100+i,decoded.packet.sequence);
  if(i==35){TEST_ASSERT_FALSE(decoded.relation.available);}
  advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
  advance(at+40*MS);dispatch(1,eventTimeout); // Service deferred work after the other broadcast slots.
  TEST_ASSERT_FALSE(deferredClock.fit.active);
 }
 TEST_ASSERT_TRUE(mulClockWorkspace()->fit.ready);TEST_ASSERT_EQUAL(5,mulClockWorkspace()->fit.count);
 TEST_ASSERT_TRUE(mulClockWorkspace()->fit.rejected>=1);TEST_ASSERT_INT_WITHIN(1,15000,followerClock.skewPpb);
}
static void experimentalConfig(unsigned slot,unsigned spacing,unsigned period) {
 multiConfig(8,slot,10);mulScheduleExperimental(&multiConfiguration.mulSchedule);
 multiConfiguration.mulSchedule.slotMs=spacing;multiConfiguration.mulSchedule.periodMs=period;
 multiConfiguration.mulSchedule.txLeadUs=500;multiConfiguration.mulSchedule.rxBudgetUs=500;
 multiConfiguration.mulSchedule.airtimeUs=400;multiConfiguration.mulSchedule.rangingFlags=0;
}
void test_experimentalFollowerUsesNumericalSpacingAndActualCycleClockPeriod(void) {
 const unsigned spacings[]={2,3,5,7,17},periods[]={100,200,1000,2000};
 for(unsigned a=0;a<5;a++)for(unsigned b=0;b<4;b++) {
  setUp();experimentalConfig(7,spacings[a],periods[b]);if(mulScheduleValidate(&multiConfiguration.mulSchedule))continue;
  useAlr[1]=true;skew=15000;flight=1000;advance(MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
  for(unsigned i=0;i<10;i++) {
   int64_t at=(int64_t)i*periods[b]*MS;advance(at+flight+MS);uint64_t rx=clockAt(1,at+flight);
   injectMulti(1,0,100+i,clockAt(0,at),rx);TEST_ASSERT_TRUE(flags.tx);TEST_ASSERT_EQUAL_UINT(80,radios[1].txLength);
   TEST_ASSERT_INT64_WITHIN(512,(int64_t)7*spacings[a]*MS,mulTimestampDifference(radios[1].actual,rx));
   TEST_ASSERT_EQUAL_UINT8(3,radios[1].tx[34]);TEST_ASSERT_FALSE(ranging.attempted);
   advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
  }
  TEST_ASSERT_EQUAL(periods[b],followerClock.periodMs);TEST_ASSERT_EQUAL(mulClockValid,followerClock.status);
 }
}
void test_experimentalShortSlotStillSkipsInsufficientAfterSpiLead(void) {
 experimentalConfig(1,3,100);useAlr[1]=true;advance(MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
 radios[1].setupLag=3*MS;injectMulti(1,0,1,clockAt(0,0),clockAt(1,0));
 TEST_ASSERT_FALSE(flags.tx);TEST_ASSERT_EQUAL_UINT(0,radios[1].sends);TEST_ASSERT_TRUE(radios[1].receiving);
}
void test_experimentalReferencePeriodAndDisabledRangingDoNotUseFixedWindows(void) {
 experimentalConfig(0,3,2000);useAlr[0]=true;advance(-20*(int64_t)MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[0].device);
 uint64_t first=0;int64_t origin=0;
 for(unsigned i=0;i<5;i++) {
  if(i)advance(origin+2000*MS-20*MS);
  dispatch(0,eventTimeout);TEST_ASSERT_TRUE(flags.tx);TEST_ASSERT_FALSE(ranging.active);
  if(!i)first=pendingTimestamp;
  TEST_ASSERT_EQUAL_INT64((int64_t)i*2000*MS,mulTimestampDifference(pendingTimestamp,first));
  TEST_ASSERT_EQUAL_UINT(40,radios[0].txLength);TEST_ASSERT_EQUAL_UINT8(3,radios[0].tx[34]);
  origin=sendTime(0);advance(origin+MS);radios[0].armed=false;dispatch(0,eventPacketSent);
  unsigned sent=radios[0].sends;advance(origin+630*MS);dispatch(0,eventTimeout);advance(origin+780*MS);dispatch(0,eventTimeout);
  TEST_ASSERT_FALSE(ranging.active);TEST_ASSERT_EQUAL_UINT(sent,radios[0].sends);
 }
}

void test_experimentalTwrReplyCandidatesCompleteWithActualScheduledMarkers(void) {
 const unsigned replies[]={20,10,5},codes[]={0,2,4};
 const int32_t skews[]={0,150000,-150000};
 for(unsigned a=0;a<3;a++)for(unsigned b=0;b<3;b++) {
  setUp();scenario(skews[b],2131);multiConfig(4,0,10);
  mulSchedule_t *s=&multiConfiguration.mulSchedule;mulScheduleExperimental(s);
  s->txLeadUs=500;s->rxBudgetUs=2000;s->airtimeUs=400;s->rangingFlags=1|codes[a];
  TEST_ASSERT_NULL(mulScheduleValidate(s));TEST_ASSERT_EQUAL(replies[a],mulScheduleReplyMs(s));
  unsigned owner=mulScheduleSyncOwner(s,exchangeSequence,0);
  TEST_ASSERT_TRUE(mulTwrConfigureSchedule(ctx(0),s,owner,0));
  TEST_ASSERT_TRUE(mulTwrConfigureSchedule(ctx(1),s,owner,0));
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,exchangeSequence,origins[0]));
  completeExchange();TEST_ASSERT_TRUE(ctx(1)->delayValid);TEST_ASSERT_UINT32_WITHIN(1,flight,ctx(1)->tof);
  int64_t localReply=mulTimestampDifference(ctx(1)->stamps[2],ctx(1)->stamps[1]);
  TEST_ASSERT_INT64_WITHIN(511,replies[a]*MS,localReply);
  TEST_ASSERT_EQUAL(3,radios[0].sends);TEST_ASSERT_EQUAL(2,radios[1].sends);
  TEST_ASSERT_FALSE(ctx(0)->active);TEST_ASSERT_FALSE(ctx(1)->active);
 }
}
void test_experimentalTwrAfterSpiLeadFailureAndTimeoutRecoverNextCycle(void) {
 scenario(0,2131);multiConfig(4,0,10);mulSchedule_t *s=&multiConfiguration.mulSchedule;
 mulScheduleExperimental(s);s->txLeadUs=500;s->rxBudgetUs=2000;s->airtimeUs=400;s->rangingFlags=5;
 unsigned owner=mulScheduleSyncOwner(s,5,0);
 TEST_ASSERT_TRUE(mulTwrConfigureSchedule(ctx(0),s,owner,0));TEST_ASSERT_TRUE(mulTwrConfigureSchedule(ctx(1),s,owner,0));
 TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
 radios[1].setupLag=4*MS;transfer(0);
 TEST_ASSERT_FALSE(ctx(1)->active);TEST_ASSERT_EQUAL(0,radios[1].sends);TEST_ASSERT_TRUE(radios[1].receiving);
 advance(800*MS);dispatch(0,eventTimeout);TEST_ASSERT_FALSE(ctx(0)->active);
 // A failed exchange must not poison the subsequent configured owner/cycle.
 radios[1].setupLag=0;exchangeSequence=6;framePhysical=SEC;
 origins[0]=clockAt(0,SEC);origins[1]=clockAt(1,SEC+flight);advance(SEC+630*MS);
 owner=mulScheduleSyncOwner(s,6,0);
 TEST_ASSERT_TRUE(mulTwrConfigureSchedule(ctx(0),s,owner,0));TEST_ASSERT_TRUE(mulTwrConfigureSchedule(ctx(1),s,owner,0));
 TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,6,origins[0]));completeExchange();
 TEST_ASSERT_TRUE(ctx(1)->delayValid);TEST_ASSERT_EQUAL(6,ctx(1)->estimateId);
}

void test_shortReplyMathIsExplicitAndCannotWeakenLegacyTimingAcceptance(void) {
 const uint64_t t[6]={0,100,5*MS+100,5*MS+200,10*MS+200,10*MS+300};uint32_t tof=0;
 TEST_ASSERT_FALSE(mulTwrCalculate(t,&tof));
 TEST_ASSERT_TRUE(mulTwrCalculateReply(t,&tof,5));TEST_ASSERT_EQUAL(100,tof);
 TEST_ASSERT_FALSE(mulTwrCalculateReply(t,&tof,7));
}

void test_generatedReservationsDriveMultipleActualTwrExchangesInOneCycle(void) {
 setUp();scenario(150000,2131);
 mulRangingPlan_t p={.replyMs=5,.maxUtilization=70,.enabled=MUL_PLAN_AA,.aaMilliHz=5000};
 mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);
 p.broadcast.rangingFlags=0;p.broadcast.txLeadUs=500;p.broadcast.rxBudgetUs=2000;p.broadcast.airtimeUs=400;
 simulationPlan=&p;TEST_ASSERT_EQUAL(5,mulRangingPlanCount(&p,5));
 for(unsigned index=0;index<5;index++) {
  mulRangingOperation_t op;TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,5,index,&op));
  advance((int64_t)op.startUs*MS/1000);
  TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),&p,5,index));TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(1),&p,5,index));
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));completeExchange();
  TEST_ASSERT_TRUE(ctx(1)->delayValid);TEST_ASSERT_UINT32_WITHIN(1,flight,ctx(1)->tof);
  TEST_ASSERT_FALSE(mulTwrConfigurePlan(ctx(0),&p,5,index));
  TEST_ASSERT_FALSE(mulTwrConfigurePlan(ctx(1),&p,5,index));
  TEST_ASSERT_TRUE(mulTimestampDifference(ctx(0)->expectedTx,origins[0])<(int64_t)op.endUs*(int64_t)MS/1000);
 }
 TEST_ASSERT_EQUAL(15,radios[0].sends);TEST_ASSERT_EQUAL(10,radios[1].sends);
 TEST_ASSERT_FALSE(mulTwrConfigurePlan(ctx(0),&p,4,0));
}

void test_actualAlrReferenceUsesGeneratedOwnersAndNeverStartsFixedWindows(void) {
 setUp();scenario(15000,2131);multiConfig(2,0,10);
 mulRangingPlan_t p={.replyMs=5,.maxUtilization=70,.enabled=MUL_PLAN_AA,.aaMilliHz=5000};
 p.broadcast=multiConfiguration.mulSchedule;mulScheduleExperimental(&p.broadcast);
 p.broadcast.rangingFlags=0;p.broadcast.txLeadUs=500;p.broadcast.rxBudgetUs=2000;p.broadcast.airtimeUs=400;
 multiConfiguration.mulSchedule=p.broadcast;multiConfiguration.mulPlan=&p;simulationPlan=&p;
 useAlr[0]=true;advance(-20*(int64_t)MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[0].device);
 dispatch(0,eventTimeout);TEST_ASSERT_TRUE(flags.tx);TEST_ASSERT_EQUAL(5,radios[0].tx[34]);
 int64_t reference=sendTime(0);advance(reference+MS);radios[0].armed=false;dispatch(0,eventPacketSent);
 exchangeSequence=pendingSequence;origins[0]=pendingTimestamp;origins[1]=clockAt(1,reference+flight);
 mulRelation_t unavailable={.version=2,.maxAgeMs=MUL_PROP_MAX_AGE_MS};
 advance(reference+10*MS+MS);radios[0].rxLength=mulPlanBroadcastEncode(radios[0].rx,sizeof(radios[0].rx),&p,1,exchangeSequence,clockAt(1,reference+10*MS),&unavailable);
 TEST_ASSERT_EQUAL(80,radios[0].rxLength);radios[0].arrival=clockAt(0,reference+10*MS);radios[0].receiving=false;dispatch(0,eventPacketReceived);
 TEST_ASSERT_EQUAL(2,followerSeen);
 for(unsigned index=0;index<5;index++) {
  mulRangingOperation_t op;TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,exchangeSequence,index,&op));
  advance(reference+(int64_t)op.startUs*MS/1000);dispatch(0,eventTimeout);
  TEST_ASSERT_TRUE(ranging.active);TEST_ASSERT_TRUE(ranging.planned);TEST_ASSERT_EQUAL(index,ranging.operation);
  TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(1),&p,exchangeSequence,index));
  // Lose the first exchange, including its TX IRQ. The next reservation must
  // cancel its stale owner and make progress without replaying the missed slot.
  if(index==0)continue;
  completeExchange();TEST_ASSERT_TRUE(ctx(1)->delayValid);
  if(index==3){unsigned sends=radios[0].sends;advance(reference+650*MS);dispatch(0,eventTimeout);TEST_ASSERT_EQUAL(sends,radios[0].sends);TEST_ASSERT_FALSE(radios[0].armed);}
 }
 TEST_ASSERT_EQUAL(31,servedOperations);TEST_ASSERT_EQUAL(14,radios[0].sends);
}

void test_actualAlrFollowerUsesAssignedGeneratedOperationsAndRetainsAaDelay(void) {
 setUp();scenario(-150000,2131);multiConfig(2,1,10);
 mulRangingPlan_t p={.replyMs=5,.maxUtilization=70,.enabled=MUL_PLAN_AA,.aaMilliHz=5000};
 p.broadcast=multiConfiguration.mulSchedule;mulScheduleExperimental(&p.broadcast);
 p.broadcast.rangingFlags=0;p.broadcast.txLeadUs=500;p.broadcast.rxBudgetUs=2000;p.broadcast.airtimeUs=400;
 multiConfiguration.mulSchedule=p.broadcast;multiConfiguration.mulPlan=&p;simulationPlan=&p;
 useAlr[1]=true;advance(MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
 injectMulti(1,0,5,origins[0],origins[1]);TEST_ASSERT_TRUE(flags.tx);TEST_ASSERT_EQUAL(5,radios[1].tx[34]);
 advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 for(unsigned index=0;index<2;index++) {
  mulRangingOperation_t op;TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,5,index,&op));advance((int64_t)op.startUs*MS/1000);
  TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),&p,5,index));TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
  completeExchange();TEST_ASSERT_TRUE(ranging.delayValid);TEST_ASSERT_EQUAL(index,ranging.operation);TEST_ASSERT_UINT32_WITHIN(1,flight,ranging.tof);
  mulClockDelay_t delay;TEST_ASSERT_TRUE(mulTwrDelay(&ranging,&followerClock,&delay));TEST_ASSERT_EQUAL(132,delay.reference[0]);TEST_ASSERT_EQUAL(135,delay.follower[0]);
 }
 TEST_ASSERT_EQUAL(5,radios[1].sends); // One data broadcast plus two response/report pairs.
}

void test_committedAaDelaySurvivesArRoleChangeCompletionAndFaults(void) {
 setUp();scenario(-150000,2131);
 mulRangingPlan_t p={.replyMs=5,.maxUtilization=70,.enabled=MUL_PLAN_AA|MUL_PLAN_AR,.aaMilliHz=1000,.arMilliHz=1000,.rlrCount=1,.rlrs={141}};
 mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;
 p.broadcast.txLeadUs=500;p.broadcast.rxBudgetUs=2000;p.broadcast.airtimeUs=400;simulationPlan=&p;
 mulRangingOperation_t op;unsigned aa=99,ar=99;
 for(unsigned i=0;i<mulRangingPlanCount(&p,5);i++){TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,5,i,&op));if(op.kind==MUL_PLAN_AA)aa=i;}
 TEST_ASSERT_NOT_EQUAL(99,aa);TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,5,aa,&op));advance((int64_t)op.startUs*MS/1000);
 TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),&p,5,aa));TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(1),&p,5,aa));
 mulTwrInvalidateAa(ctx(1));TEST_ASSERT_EQUAL(mulPropagationInvalidated,mulTwrPropagationState(ctx(1),&model,ticks));
 TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));completeExchange();
 mulClockDelay_t before,after;TEST_ASSERT_TRUE(mulTwrDelay(ctx(1),&model,&before));
 TEST_ASSERT_EQUAL(132,before.reference[0]);TEST_ASSERT_EQUAL(133,before.follower[0]);TEST_ASSERT_EQUAL(5,before.referenceSequence);
 bool corrected=mulTwrDelayPowerCorrected(ctx(1));
 // The former follower now initiates an AR operation. Data role and exchange
 // role are independent; both radios still have different physical clocks.
 exchangeSequence=6;origins[0]=clockAt(0,SEC);origins[1]=clockAt(1,SEC+flight);
 protocolRole[0]=mulAlrFollower;protocolRole[1]=mulAlrReference;
 for(unsigned i=0;i<mulRangingPlanCount(&p,6);i++){TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,6,i,&op));if(op.kind==MUL_PLAN_AR&&op.initiator==133)ar=i;}
 TEST_ASSERT_NOT_EQUAL(99,ar);TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,6,ar,&op));advance(SEC+(int64_t)op.startUs*MS/1000);
 TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),&p,6,ar));TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(1),&p,6,ar));
 TEST_ASSERT_TRUE(mulTwrDelay(ctx(1),&model,&after));TEST_ASSERT_EQUAL(before.measuredMs,after.measuredMs);
 TEST_ASSERT_TRUE(mulTwrStart(ctx(1),&radios[1].device,6,origins[1]));
 transfer(1);transfer(0);transfer(1);transfer(0);transfer(1);
 TEST_ASSERT_FALSE(ctx(1)->active);TEST_ASSERT_FALSE(ctx(0)->active);
 TEST_ASSERT_TRUE(mulTwrDelay(ctx(1),&model,&after));TEST_ASSERT_EQUAL(before.epoch,after.epoch);
 TEST_ASSERT_EQUAL(before.measuredMs,after.measuredMs);TEST_ASSERT_EQUAL(before.oneWayDtu,after.oneWayDtu);
 TEST_ASSERT_EQUAL_MEMORY(before.reference,after.reference,8);TEST_ASSERT_EQUAL_MEMORY(before.follower,after.follower,8);
 TEST_ASSERT_EQUAL(5,after.referenceSequence);TEST_ASSERT_EQUAL(corrected,mulTwrDelayPowerCorrected(ctx(1)));
 mulTwrCancel(ctx(1),&radios[1].device,"AR_failure",true);
 TEST_ASSERT_TRUE(mulTwrDelay(ctx(1),&model,&after));
 TEST_ASSERT_EQUAL(mulPropagationExpired,mulTwrPropagationState(ctx(1),&model,before.measuredMs+MUL_PROP_MAX_AGE_MS+1));
 TEST_ASSERT_EQUAL(1,mulTwrDelayWait(ctx(1),before.measuredMs+MUL_PROP_MAX_AGE_MS,100));
 mulClockReset(&model);TEST_ASSERT_FALSE(mulTwrDelay(ctx(1),&model,&after));
 TEST_ASSERT_EQUAL(mulPropagationInvalidated,mulTwrPropagationState(ctx(1),&model,ticks));
 mulTwrInvalidateAa(ctx(1));TEST_ASSERT_FALSE(ctx(1)->aa.valid);TEST_ASSERT_FALSE(mulTwrDelay(ctx(1),&model,&after));
}

void test_actualAlrReferenceInitiatesAssignedArOnlyOperation(void) {
 setUp();scenario(150000,2131);multiConfig(2,0,10);
 mulRangingPlan_t p={.replyMs=5,.maxUtilization=70,.enabled=MUL_PLAN_AR,.arMilliHz=1000,.rlrCount=1,.rlrs={141}};
 p.broadcast=multiConfiguration.mulSchedule;mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;
 p.broadcast.txLeadUs=500;p.broadcast.rxBudgetUs=2000;p.broadcast.airtimeUs=400;
 multiConfiguration.mulSchedule=p.broadcast;multiConfiguration.mulPlan=&p;simulationPlan=&p;
 useAlr[0]=true;advance(-20*(int64_t)MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[0].device);dispatch(0,eventTimeout);
 int64_t reference=sendTime(0);advance(reference+MS);radios[0].armed=false;dispatch(0,eventPacketSent);
 exchangeSequence=pendingSequence;origins[0]=pendingTimestamp;origins[1]=clockAt(1,reference+flight);
 unsigned ar=99;mulRangingOperation_t op;
 for(unsigned i=0;i<mulRangingPlanCount(&p,exchangeSequence);i++){TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,exchangeSequence,i,&op));if(op.initiator==132)ar=i;}
 TEST_ASSERT_NOT_EQUAL(99,ar);TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,exchangeSequence,ar,&op));
 advance(reference+(int64_t)op.startUs*MS/1000);dispatch(0,eventTimeout);
 TEST_ASSERT_TRUE(ranging.active);TEST_ASSERT_EQUAL(ar,ranging.operation);TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(1),&p,exchangeSequence,ar));
 completeExchange();TEST_ASSERT_FALSE(ranging.active);TEST_ASSERT_UINT32_WITHIN(1,flight,ranging.candidateTof);
 TEST_ASSERT_FALSE(ranging.aa.valid);unsigned sends=radios[0].sends;dispatch(0,eventTimeout);TEST_ASSERT_EQUAL(sends,radios[0].sends);
}

void test_actualFollowerArInitiatorUsesMappedLocalTargetsWithoutAaDependency(void) {
 for(unsigned profile=0;profile<4;profile++) {
 unsigned compensated=profile&1;
 setUp();scenario(-150000,2131);multiConfig(2,1,10);
 mulRangingPlan_t p={.replyMs=5,.maxUtilization=70,.enabled=MUL_PLAN_AR|(compensated?MUL_PLAN_AA:0),.aaMilliHz=1000,.arMilliHz=1000,.rlrCount=1,.rlrs={141}};
 p.policy=profile>>1;p.aaPriority=p.policy?2:0;p.arPriority=p.policy?1:0;
 p.broadcast=multiConfiguration.mulSchedule;mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;
 p.broadcast.txLeadUs=500;p.broadcast.rxBudgetUs=2000;p.broadcast.airtimeUs=400;
 multiConfiguration.mulSchedule=p.broadcast;multiConfiguration.mulPlan=&p;simulationPlan=&p;
 useAlr[1]=true;protocolRole[0]=mulAlrFollower;advance(MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
 injectMulti(1,0,5,origins[0],origins[1]);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 // The six reference observations in scenario() establish the receiver's affine
 // clock. No AA propagation measurement is present in this AR-only plan.
 followerClock=model;TEST_ASSERT_FALSE(ranging.aa.valid);
 if(compensated) {
  // Seed the previous cycle's completed AA context, not a new AR-only context.
  mulRangingOperation_t previous;
  for(unsigned i=0;i<mulRangingPlanCount(&p,4);i++) {
   TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,4,i,&previous));
   if(previous.kind==MUL_PLAN_AA){TEST_ASSERT_TRUE(mulTwrConfigurePlan(&ranging,&p,4,i));ranging.attempted=true;break;}
  }
  ranging.aa=(mulTwrAa_t){.epoch=model.epoch,.measuredMs=model.observedMs-1000,.sequence=4,.tof=(uint16_t)flight,.reference=132,.follower=135,.valid=true};
 }
 unsigned ar=99;mulRangingOperation_t op;
 for(unsigned i=0;i<mulRangingPlanCount(&p,5);i++){TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,5,i,&op));if(op.initiator==135)ar=i;}
 TEST_ASSERT_NOT_EQUAL(99,ar);TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,5,ar,&op));
 advance((int64_t)op.startUs*MS/1000+MS);
 // Previous-cycle AA has expired under the default 1s policy, but not at 6s.
 bool delayFresh=compensated&&(uint32_t)(ticks-ranging.aa.measuredMs)<=MUL_PROP_MAX_AGE_MS;
 if(compensated)TEST_ASSERT_EQUAL(delayFresh?mulPropagationFresh:mulPropagationExpired,mulTwrPropagationState(&ranging,&followerClock,ticks));
 dispatch(1,eventTimeout);TEST_ASSERT_TRUE(ranging.active);TEST_ASSERT_EQUAL(ar,ranging.operation);
 uint64_t expected=clockAt(1,(int64_t)op.pollUs*MS/1000+(delayFresh?0:flight));
 TEST_ASSERT_INT_WITHIN(1024,0,mulTimestampDifference(ranging.expectedTx,expected));
 TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),&p,5,ar));transfer(1);transfer(0);transfer(1);transfer(0);transfer(1);
 TEST_ASSERT_FALSE(ranging.active);TEST_ASSERT_UINT32_WITHIN(1,flight,ranging.candidateTof);TEST_ASSERT_EQUAL(compensated,ranging.aa.valid);
 unsigned sends=radios[1].sends;dispatch(1,eventTimeout);TEST_ASSERT_EQUAL(sends,radios[1].sends);
}
}

void test_explicitLocalArTargetsRejectForeignClockAndAfterSpiLateness(void) {
 setUp();scenario(150000,2131);
 mulRangingPlan_t p={.replyMs=5,.maxUtilization=70,.enabled=MUL_PLAN_AR,.arMilliHz=1000,.rlrCount=1,.rlrs={141}};
 mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;
 p.broadcast.txLeadUs=500;p.broadcast.rxBudgetUs=2000;p.broadcast.airtimeUs=400;
 simulationPlan=&p;mulRangingOperation_t op;TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,5,0,&op));
 advance((int64_t)op.startUs*MS/1000);TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),&p,5,0));
 uint64_t poll=mulTimestampMask(origins[0]+(uint64_t)op.pollUs*MS/1000),end=mulTimestampMask(origins[0]+(uint64_t)op.endUs*MS/1000);
 TEST_ASSERT_FALSE(mulTwrStartTargets(ctx(0),&radios[0].device,5,mulTimestampMask(origins[1]+op.pollUs*MS/1000),mulTimestampMask(origins[1]+op.endUs*MS/1000)));
 TEST_ASSERT_FALSE(ctx(0)->attempted);TEST_ASSERT_EQUAL(0,radios[0].sends);
 radios[0].setupLag=4*MS;TEST_ASSERT_FALSE(mulTwrStartTargets(ctx(0),&radios[0].device,5,poll,end));
 TEST_ASSERT_TRUE(ctx(0)->attempted);TEST_ASSERT_FALSE(ctx(0)->active);TEST_ASSERT_EQUAL(0,radios[0].sends);
 radios[0].setupLag=0;TEST_ASSERT_FALSE(mulTwrStartTargets(ctx(0),&radios[0].device,5,poll,end));TEST_ASSERT_EQUAL(0,radios[0].sends);
}

void vPortEnterCritical(void){}
void vPortExitCritical(void){}

static void tracePoseRadio(FILE *out,unsigned n,const uint8_t *bytes,int64_t at,int64_t distance) {
 if(!out)return;
 fprintf(out,"%u %lu %010llX ",n,(unsigned long)(10000+at/(int64_t)MS),(unsigned long long)clockAt(2,at+distance));
 for(unsigned i=0;i<n;i++)fprintf(out,"%02X",bytes[i]);
 fputc('\n',out);
}
void test_actualFollowerUsesExternalGeometryAndNeverFallsBackToTwr(void) {
 scenario(-150000,2131);multiConfig(2,1,10);mulGeometrySelect(mulDelayExternal);
 useAlr[1]=true;advance(MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
 FILE *trace=NULL;const char *path=getenv("MUL_POSE_RADIO_TRACE");if(path){trace=fopen(path,"w");TEST_ASSERT_NOT_NULL(trace);}
 for(unsigned seq=1;seq<=5;seq++) {
  int64_t at=((int64_t)seq-5)*SEC;uint8_t bytes[40];
  TEST_ASSERT_EQUAL(40,mulMultiBroadcastEncode(bytes,sizeof(bytes),&multiConfiguration.mulSchedule,0,seq,clockAt(0,at),NULL));
  tracePoseRadio(trace,40,bytes,at,3131);
 }
 injectMulti(1,0,5,origins[0],origins[1]);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 followerClock=model;advance(SEC+MS);
 TEST_ASSERT_TRUE(mulGeometryBegin(1,1,1,0));
 mulPose_t a={.clock=1,.epoch=1,.frame=1,.sequence=1,.sourceUs=1000000,.node=132,.flags=7,.ttlMs=1000,
  .receivedMs=ticks,.covarianceMm2={100,0,0,100,0,100}};
 mulPose_t b=a;b.node=135;b.positionMm[0]=10000;
 TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&a));TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&b));mulGeometryPoll(ticks);
 injectMulti(1,0,6,clockAt(0,SEC),clockAt(1,SEC+flight));TEST_ASSERT_TRUE(flags.tx);
 tracePoseRadio(trace,40,radios[1].rx,SEC,3131);
 mulMultiBroadcast_t decoded;TEST_ASSERT_TRUE(mulMultiBroadcastDecode(&multiConfiguration.mulSchedule,radios[1].tx,80,&decoded));
 TEST_ASSERT_EQUAL(3,decoded.relation.version);TEST_ASSERT_EQUAL(mulDelayExternal,mulRelationSource(&decoded.relation));
 TEST_ASSERT_TRUE(decoded.relation.available);TEST_ASSERT_EQUAL(2131,decoded.relation.delayDtu);TEST_ASSERT_FALSE(decoded.relation.powerCorrectedTwr);
 TEST_ASSERT_FALSE(ranging.delayValid);int64_t sent=sendTime(1);advance(sent+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 tracePoseRadio(trace,80,radios[1].tx,sent,1000);
 // A valid old TWR result must not override source none or missing external poses.
 ranging.delayValid=true;ranging.tof=900;ranging.epoch=followerClock.epoch;ranging.measuredMs=ticks;ranging.estimateId=6;
 mulGeometrySelect(mulDelayNone);advance(2*SEC+MS);injectMulti(1,0,7,clockAt(0,2*SEC),clockAt(1,2*SEC+flight));
 tracePoseRadio(trace,40,radios[1].rx,2*SEC,3131);
 TEST_ASSERT_TRUE(mulMultiBroadcastDecode(&multiConfiguration.mulSchedule,radios[1].tx,80,&decoded));
 TEST_ASSERT_FALSE(decoded.relation.available);TEST_ASSERT_EQUAL(mulDelayNone,mulRelationSource(&decoded.relation));
 sent=sendTime(1);advance(sent+MS);radios[1].armed=false;dispatch(1,eventPacketSent);tracePoseRadio(trace,80,radios[1].tx,sent,1000);
 mulGeometrySelect(mulDelayExternal);advance(3*SEC+MS);injectMulti(1,0,8,clockAt(0,3*SEC),clockAt(1,3*SEC+flight));
 tracePoseRadio(trace,40,radios[1].rx,3*SEC,3131);
 TEST_ASSERT_TRUE(mulMultiBroadcastDecode(&multiConfiguration.mulSchedule,radios[1].tx,80,&decoded));
 TEST_ASSERT_FALSE(decoded.relation.available);TEST_ASSERT_EQUAL(mulDelayExternal,mulRelationSource(&decoded.relation));
 sent=sendTime(1);advance(sent+MS);radios[1].armed=false;dispatch(1,eventPacketSent);tracePoseRadio(trace,80,radios[1].tx,sent,1000);
 if(trace)fclose(trace);
 mulGeometrySelect(mulDelayTwr);
}

#if MUL_TWR_TRACE
/* Real production USB writer and CDC admission/packetization. Only the RTOS
 * FIFO and USB controller are simulated; no duplicate TryWrite implementation. */
#include "../src/usbd_cdc_if.c"
#include "../src/usbcomm.c"
USBD_HandleTypeDef hUsbDeviceFS;
static USBD_CDC_HandleTypeDef aaCdc;
static unsigned aaUsbHead,aaUsbTail,aaUsbCount,aaHostLength;
static char aaUsbFifo[512],aaHost[2048];
static bool aaConnected;
QueueHandle_t xQueueGenericCreateStatic(UBaseType_t n,UBaseType_t width,uint8_t *storage,StaticQueue_t *q,uint8_t type) {
 (void)storage;(void)q;(void)type;
 if(n==512&&width==1){aaUsbHead=aaUsbTail=aaUsbCount=0;return (QueueHandle_t)(uintptr_t)1;}
 TEST_ASSERT_TRUE(n==1&&width==0);return (QueueHandle_t)(uintptr_t)2;
}
BaseType_t xQueueGenericSend(QueueHandle_t q,const void *item,TickType_t ticks,BaseType_t position) {
 (void)position;TEST_ASSERT_EQUAL(0,ticks);
 if((uintptr_t)q==2)return pdTRUE;
 TEST_ASSERT_EQUAL(1,(uintptr_t)q);if(aaUsbCount==512)return pdFALSE;
 aaUsbFifo[aaUsbHead]=*(const char *)item;aaUsbHead=(aaUsbHead+1)%512;aaUsbCount++;return pdTRUE;
}
UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t q){TEST_ASSERT_EQUAL(1,(uintptr_t)q);return 512-aaUsbCount;}
BaseType_t xQueueReceiveFromISR(QueueHandle_t q,void *item,BaseType_t *woken) {
 (void)woken;TEST_ASSERT_EQUAL(1,(uintptr_t)q);if(!aaUsbCount)return pdFALSE;
 *(char *)item=aaUsbFifo[aaUsbTail];aaUsbTail=(aaUsbTail+1)%512;aaUsbCount--;return pdTRUE;
}
uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *d,uint8_t *p,uint16_t n){(void)d;aaCdc.TxBuffer=p;aaCdc.TxLength=n;return USBD_OK;}
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *d,uint8_t *p){(void)d;(void)p;return USBD_OK;}
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *d) {
 (void)d;TEST_ASSERT_TRUE(aaCdc.TxLength<=64);TEST_ASSERT_TRUE(aaHostLength+aaCdc.TxLength<sizeof(aaHost));
 memcpy(aaHost+aaHostLength,aaCdc.TxBuffer,aaCdc.TxLength);aaHostLength+=aaCdc.TxLength;aaHost[aaHostLength]=0;return USBD_OK;
}
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *d){(void)d;return USBD_OK;}
bool USBD_IsSerialConnected(void){return aaConnected;}
static void drainAaUsb(void){while(aaUsbCount)CDC_SOF_FS();}
static mulAaTrace_t aaRecords[96];static unsigned aaRecordsCount,aaWrites;
static uint32_t aaLost;
static bool aaAccept;
static char aaLine[144],aaPrevious[144];
static void drainAa(void) {
 mulAaTrace_t r;
 while(mulTwrTraceTake(&r,&aaLost)) {
  TEST_ASSERT_TRUE(aaRecordsCount<96);aaRecords[aaRecordsCount++]=r;
 }
}
static void clearAa(void) {aaRecordsCount=0;drainAa();aaRecordsCount=aaWrites=0;}
static bool writeAa(const char *line,unsigned n) {
 TEST_ASSERT_TRUE(n<sizeof(aaLine));TEST_ASSERT_EQUAL('\n',line[n-1]);
 memcpy(aaLine,line,n);aaLine[n]=0;aaWrites++;return aaAccept;
}
static void prepareAaDelay(mulRangingPlan_t *p,uint32_t delay) {
 setUp();scenario(-150000,2131);clearAa();
 memset(p,0,sizeof(*p));p->replyMs=20;p->maxUtilization=70;p->enabled=MUL_PLAN_AA|MUL_PLAN_AR;
 p->aaMilliHz=1000;p->arMilliHz=50;p->rlrCount=1;p->rlrs[0]=141;
 mulScheduleLegacy(&p->broadcast,132,133,10);mulScheduleExperimental(&p->broadcast);p->broadcast.rangingFlags=0;
 p->broadcast.txLeadUs=1000;p->broadcast.rxBudgetUs=5000;p->broadcast.airtimeUs=1000;p->broadcast.guard100us=1;
 TEST_ASSERT_EQUAL_HEX32(0x856AB19D,mulRangingPlanFingerprint(p));
 p->aaStartDelayUs=delay;
 mulRangingOperation_t op;TEST_ASSERT_TRUE(mulRangingPlanOperation(p,5,0,&op));TEST_ASSERT_EQUAL(MUL_PLAN_AA,op.kind);
 TEST_ASSERT_EQUAL(23100+delay,op.pollUs);simulationPlan=p;
 advance((int64_t)op.startUs*MS/1000);
 TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),p,5,0));TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(1),p,5,0));
}
static void prepareAa(mulRangingPlan_t *p){prepareAaDelay(p,0);}
static bool sawAa(unsigned local,unsigned event,unsigned stage) {
 for(unsigned i=0;i<aaRecordsCount;i++)if(aaRecords[i].local==local&&aaRecords[i].event==event&&aaRecords[i].stage==stage)return true;
 return false;
}
#endif
void test_generatedAaTraceCapturesBothRolesVerifiedStagesAndLocalDeadlines(void) {
#if MUL_TWR_TRACE
 mulRangingPlan_t p;prepareAa(&p);uint32_t before=aaLost;
 dispatch(1,eventTimeout);drainAa(); // Assigned responder window, even before a poll.
 TEST_ASSERT_TRUE(sawAa(133,mulAaWindow,mulTwrPoll));
 TEST_ASSERT_FALSE(sawAa(133,mulAaRx,mulTwrPoll));
 dispatch(1,eventReceiveTimeout);drainAa();
 TEST_ASSERT_EQUAL(mulAaWaitEvent,aaRecords[aaRecordsCount-1].event);
 TEST_ASSERT_EQUAL(eventReceiveTimeout,(aaRecords[aaRecordsCount-1].flags>>2)&7);
 dispatch(1,eventReceiveFailed);drainAa();
 TEST_ASSERT_EQUAL(mulAaWaitEvent,aaRecords[aaRecordsCount-1].event);
 TEST_ASSERT_EQUAL(eventReceiveFailed,(aaRecords[aaRecordsCount-1].flags>>2)&7);
 TEST_ASSERT_FALSE(ctx(1)->active);TEST_ASSERT_EQUAL(0,radios[1].sends);
 TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));drainAa();
 for(unsigned i=0;i<5;i++){transfer(i%2);drainAa();TEST_ASSERT_EQUAL(0,aaWrites);}
 TEST_ASSERT_TRUE(ctx(1)->aa.valid);TEST_ASSERT_EQUAL(before,aaLost);
 for(unsigned i=0;i<aaRecordsCount;i++) {
  const mulAaTrace_t *r=&aaRecords[i];TEST_ASSERT_EQUAL(5,r->cycle);TEST_ASSERT_EQUAL(0,r->operation);
  TEST_ASSERT_EQUAL(132,r->initiator);TEST_ASSERT_EQUAL(133,r->responder);
  if(r->event==mulAaArm)TEST_ASSERT_EQUAL_INT64(27*(int64_t)MS,mulTimestampDifference(r->deadline,r->stamp));
  if(r->event==mulAaTx){TEST_ASSERT_TRUE(r->flags&1);} // Actual TXFRS, not the scheduled marker alone.
 }
 TEST_ASSERT_TRUE(sawAa(132,mulAaTx,mulTwrPoll));TEST_ASSERT_TRUE(sawAa(132,mulAaListen,mulTwrResponse));
 TEST_ASSERT_TRUE(sawAa(132,mulAaRx,mulTwrResponse));TEST_ASSERT_TRUE(sawAa(132,mulAaTx,mulTwrFinal));
 TEST_ASSERT_TRUE(sawAa(133,mulAaRx,mulTwrPoll));TEST_ASSERT_TRUE(sawAa(133,mulAaTx,mulTwrResponse));
 TEST_ASSERT_TRUE(sawAa(133,mulAaListen,mulTwrFinal));TEST_ASSERT_TRUE(sawAa(133,mulAaRx,mulTwrFinal));
 TEST_ASSERT_TRUE(sawAa(132,mulAaDone,mulTwrResult));TEST_ASSERT_TRUE(sawAa(133,mulAaDone,mulTwrResult));
#endif
}
void test_generatedAaTraceSeparatesDeadlineRxTimeoutRadioFailureAndLateSetup(void) {
#if MUL_TWR_TRACE
 for(unsigned which=0;which<4;which++) {
  mulRangingPlan_t p;prepareAa(&p);
  if(which==3)radios[0].setupLag=20*MS;
  bool started=mulTwrStart(ctx(0),&radios[0].device,5,origins[0]);TEST_ASSERT_EQUAL(which!=3,started);
  if(started) {
   int64_t sent=sendTime(0);advance(sent+MS);radios[0].armed=false;dispatch(0,eventPacketSent);
   advance(sent+(which?2:28)*MS);
   dispatch(0,which==0?eventTimeout:which==1?eventReceiveTimeout:eventReceiveFailed);
  }
  TEST_ASSERT_FALSE(ctx(0)->active);TEST_ASSERT_TRUE(radios[0].receiving);drainAa();
  const mulAaTrace_t *last=&aaRecords[aaRecordsCount-1];TEST_ASSERT_EQUAL(mulAaFail,last->event);
  TEST_ASSERT_EQUAL(which==3?mulTwrPoll:mulTwrResponse,last->stage);
  TEST_ASSERT_EQUAL(which==0?1:which==3?4:2,last->why);
  if(which==0){TEST_ASSERT_TRUE(mulTimestampDifference(last->now,last->deadline)>=0);}
  if(which==1||which==2)TEST_ASSERT_EQUAL(which==1?eventReceiveTimeout:eventReceiveFailed,(last->flags>>2)&7);
 }
 mulRangingPlan_t p;prepareAa(&p);radios[1].setupLag=20*MS;
 TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));transfer(0);drainAa();
 const mulAaTrace_t *last=&aaRecords[aaRecordsCount-1];
 TEST_ASSERT_EQUAL(mulAaFail,last->event);TEST_ASSERT_EQUAL(133,last->local);
 TEST_ASSERT_EQUAL(mulTwrResponse,last->stage);TEST_ASSERT_EQUAL(4,last->why);
 TEST_ASSERT_TRUE(ctx(0)->active);TEST_ASSERT_FALSE(ctx(1)->active);TEST_ASSERT_EQUAL(0,radios[1].sends);
#endif
}
void test_generatedAaTraceOverflowAndUsbBackpressureNeverChangeRadioState(void) {
#if MUL_TWR_TRACE
 mulRangingPlan_t p;prepareAa(&p);uint32_t before=aaLost;
 TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
 aaAccept=false;mulTwrTracePoll(writeAa);TEST_ASSERT_EQUAL(1,aaWrites);strcpy(aaPrevious,aaLine);
 mulTwrTracePoll(writeAa);TEST_ASSERT_EQUAL_STRING(aaPrevious,aaLine);
 aaAccept=true;mulTwrTracePoll(writeAa);TEST_ASSERT_EQUAL_STRING(aaPrevious,aaLine);
 for(unsigned i=0;i<8;i++) {
  ctx(0)->active=true;mulTwrCancel(ctx(0),&radios[0].device,"timeout",false);
  ctx(0)->attempted=false;
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));
 }
 TEST_ASSERT_TRUE(ctx(0)->active);TEST_ASSERT_TRUE(radios[0].armed);drainAa();TEST_ASSERT_TRUE(aaLost>before);
 TEST_ASSERT_TRUE(aaRecordsCount<=16);TEST_ASSERT_TRUE(strstr(aaLine,"AA1 c=5 o=0 p=84>85 l=84"));
#endif
}

void test_generatedAaTraceReachesActualUsbQueueAndRetriesAfterBackpressure(void) {
#if MUL_TWR_TRACE
 mulRangingPlan_t p;prepareAa(&p);
 memset(&hUsbDeviceFS,0,sizeof(hUsbDeviceFS));memset(&aaCdc,0,sizeof(aaCdc));hUsbDeviceFS.pClassData=&aaCdc;
 TEST_ASSERT_EQUAL(USBD_OK,CDC_Init_FS());aaHostLength=0;aaHost[0]=0;
 aaConnected=false;usbcommSetSystemStarted(false);
 TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));uint64_t target=ctx(0)->expectedTx;
 mulTwrTracePoll(usbcommTryWrite);TEST_ASSERT_EQUAL(0,aaUsbCount);
 usbcommSetSystemStarted(true);mulTwrTracePoll(usbcommTryWrite);TEST_ASSERT_EQUAL(0,aaUsbCount);
 aaConnected=true;
 char fill[512];memset(fill,'X',sizeof(fill));CDC_Write(fill,sizeof(fill));TEST_ASSERT_EQUAL(512,aaUsbCount);
 mulTwrTracePoll(usbcommTryWrite);TEST_ASSERT_EQUAL(512,aaUsbCount); // Atomic refusal, no partial output.
 drainAaUsb();TEST_ASSERT_EQUAL(512,aaHostLength);aaHostLength=0;aaHost[0]=0;
 mulTwrTracePoll(usbcommTryWrite);
 TEST_ASSERT_TRUE_MESSAGE(aaUsbCount>48,"AA1 must cross the real CDC admission path, not a permissive writer stub");
 drainAaUsb();TEST_ASSERT_NOT_NULL(strstr(aaHost,"AA1 c=5 o=0 p=84>85 l=84 e=1"));
 for(unsigned i=0;i<2;i++){mulTwrTracePoll(usbcommTryWrite);drainAaUsb();}
 TEST_ASSERT_NOT_NULL(strstr(aaHost,"l=84 e=9"));TEST_ASSERT_NOT_NULL(strstr(aaHost,"l=84 e=2"));
 unsigned lines=0;for(unsigned i=0;i<aaHostLength;i++)lines+=aaHost[i]=='\n';TEST_ASSERT_EQUAL(3,lines);
 unsigned length=aaHostLength;mulTwrTracePoll(usbcommTryWrite);drainAaUsb();TEST_ASSERT_EQUAL(length,aaHostLength);
 TEST_ASSERT_TRUE(ctx(0)->active);TEST_ASSERT_TRUE(radios[0].armed);TEST_ASSERT_EQUAL_UINT64(target,ctx(0)->expectedTx);
 CDC_DeInit_FS();mulTwrCancel(ctx(0),&radios[0].device,"timeout",false);
 mulTwrTracePoll(usbcommTryWrite);TEST_ASSERT_EQUAL(0,aaUsbCount);
 TEST_ASSERT_EQUAL(USBD_OK,CDC_Init_FS());mulTwrTracePoll(usbcommTryWrite);drainAaUsb();
 TEST_ASSERT_NOT_NULL(strstr(aaHost,"l=84 e=7"));CDC_DeInit_FS();usbcommSetSystemStarted(false);aaConnected=false;
#endif
}

void test_generatedAaDelayDrivesActualPollAndResponderAcceptance(void) {
#if MUL_TWR_TRACE
 mulRangingPlan_t p;prepareAaDelay(&p,50000);
 dispatch(1,eventTimeout);drainAa();
 TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,5,origins[0]));drainAa();
 // Delayed-TX quantization is512 DTU; marker includes TX antenna delay.
 TEST_ASSERT_INT64_WITHIN(512,73100*(int64_t)MS/1000,
                         mulTimestampDifference(ctx(0)->expectedTx,origins[0]));
 for(unsigned i=0;i<5;i++){transfer(i%2);drainAa();}
 TEST_ASSERT_TRUE(sawAa(133,mulAaRx,mulTwrPoll));
 TEST_ASSERT_TRUE(sawAa(132,mulAaDone,mulTwrResult));TEST_ASSERT_TRUE(sawAa(133,mulAaDone,mulTwrResult));
#endif
}

#if MUL_RX_DIAG
static char rxLines[4096],rxRetry[129];static bool rxAccept;
static unsigned rxCalls;
static bool rxWrite(const char *line,unsigned n) {
 TEST_ASSERT_TRUE(n<=128);TEST_ASSERT_TRUE(n<sizeof(rxRetry));TEST_ASSERT_EQUAL('\n',line[n-1]);
 rxCalls++;memcpy(rxRetry,line,n);rxRetry[n]=0;
 if(!rxAccept)return false;
 TEST_ASSERT_TRUE(strlen(rxLines)+n<sizeof(rxLines));strncat(rxLines,line,n);return true;
}
static void rxDrain(void){rxAccept=true;for(unsigned i=0;i<12;i++)mulRxDiagPoll(rxWrite);}
#endif
void test_rxDiagnosticsBoundsBackpressureWindowCorrelationAndPreclearStatus(void) {
#if MUL_RX_DIAG
 rxLines[0]=0;rxDrain();rxLines[0]=0;
 mulRangingPlan_t p;memset(&p,0,sizeof(p));p.replyMs=20;p.maxUtilization=70;p.enabled=1;p.aaMilliHz=1000;p.aaStartDelayUs=50000;
 mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;p.broadcast.txLeadUs=1000;p.broadcast.rxBudgetUs=5000;
 mulRxInit(&radios[1].device);mulRxDiagBeacon(0x123456789a);
 unsigned sends=radios[1].sends,rearms=radios[1].rearms;
 uint64_t origin=mulTimestampMask(MUL_TIMESTAMP_MASK-70*MS);
 radios[1].now=mulTimestampMask(origin+67*MS);mulRxDiagService(&radios[1].device,&p,UINT32_MAX,133,origin);
 radios[1].device.sysstatus[1]=0x80; // RXFCE captured by driver before its reset.
 mulRxDiagEvent(&radios[1].device,eventReceiveFailed);
 memset(radios[1].device.sysstatus,0,5); // Simulate subsequent status/cache overwrite.
 radios[1].device.deviceMode=RX_MODE;radios[1].receiving=false; // Driver request is not hardware truth.
 radios[1].now=mulTimestampMask(origin+72*MS);mulRxDiagService(&radios[1].device,&p,UINT32_MAX,133,origin);
 rxAccept=false;rxCalls=0;mulRxDiagPoll(rxWrite);TEST_ASSERT_EQUAL(1,rxCalls);
 char retry[129];strcpy(retry,rxRetry);mulRxDiagPoll(rxWrite);TEST_ASSERT_EQUAL_STRING(retry,rxRetry);
 TEST_ASSERT_NOT_NULL(strstr(retry,"RX1 c=4294967295 o=0 l=85 k=1"));
 TEST_ASSERT_NOT_NULL(strstr(retry,"h=00010000 s=0000000000 i=0000008000"));
 rxAccept=true;mulRxDiagPoll(rxWrite);mulRxDiagPoll(rxWrite);
 TEST_ASSERT_NOT_NULL(strstr(rxLines,"RX2 c=4294967295 o=0 l=85 k=1"));
 TEST_ASSERT_NOT_NULL(strstr(rxLines,"b=123456789A"));
 mulRxDiagDecision(0,0);mulRxDiagDecision(4,0x10);mulRxDiagDecision(8,0x10);
 radios[1].now=mulTimestampMask(origin+160*MS);mulRxDiagService(&radios[1].device,&p,UINT32_MAX,133,origin);rxDrain();
 TEST_ASSERT_NOT_NULL(strstr(rxLines,"k=2"));TEST_ASSERT_NOT_NULL(strstr(rxLines,"a=0E"));
 TEST_ASSERT_EQUAL(sends,radios[1].sends);TEST_ASSERT_EQUAL(rearms,radios[1].rearms);TEST_ASSERT_FALSE(radios[1].receiving);
 // Ten snapshots without USB service exceed the bounded four-record ring.
 rxLines[0]=0;
 for(unsigned c=0;c<5;c++) {
  uint64_t base=(uint64_t)c*SEC;
  radios[1].now=mulTimestampMask(base+72*MS);mulRxDiagService(&radios[1].device,&p,c,133,base);
  radios[1].now=mulTimestampMask(base+160*MS);mulRxDiagService(&radios[1].device,&p,c,133,base);
 }
 rxDrain();TEST_ASSERT_NOT_NULL(strstr(rxLines,"z=6"));
#else
 TEST_ASSERT_EQUAL(0,MUL_RX_DIAG);
 unsigned sideEffects=0;mulRxDiagEvent(sideEffects++,sideEffects++);mulRxDiagPoll(sideEffects++);
 TEST_ASSERT_EQUAL(0,sideEffects);
#endif
}

static void deferredConfiguration(mulRangingPlan_t *p,unsigned period,unsigned first) {
 multiConfig(3,1,10);mulScheduleExperimental(&multiConfiguration.mulSchedule);
 multiConfiguration.mulSchedule.slotMs=5;
 *p=(mulRangingPlan_t){.broadcast=multiConfiguration.mulSchedule,.replyMs=20,
   .maxUtilization=70,.enabled=3,.aaMilliHz=1000,.arMilliHz=1000,
   .rlrCount=1,.rlrs={141},.aaStartDelayUs=50000};
 p->broadcast.ids[0]=132;p->broadcast.ids[1]=first;p->broadcast.ids[2]=first==133?134:133;
 p->broadcast.periodMs=period;p->broadcast.txLeadUs=1000;p->broadcast.rxBudgetUs=2000;p->broadcast.rangingFlags=0;
 TEST_ASSERT_NULL(mulRangingPlanValidate(p,NULL));
 multiConfiguration.mulSchedule=p->broadcast;multiConfiguration.mulPlan=p;
 TEST_ASSERT_TRUE(mulSchedulePair(&p->broadcast,1,&multiConfiguration.mulNetwork));
 memcpy(multiConfiguration.address,multiConfiguration.mulNetwork.addresses[1],8);
 mulClockFitDefaults(&multiConfiguration.mulClockConfig);multiConfiguration.mulClockConfig.estimator=2;
 multiConfiguration.mulClockConfig.compensate=1;
 simulationSchedule=&multiConfiguration.mulSchedule;simulationPlan=p;useAlr[1]=true;skew=15000;flight=400;
 advance(MS);mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
}
static void deferredReference(const mulRangingPlan_t *p,unsigned cycle) {
 int64_t at=(int64_t)cycle*p->broadcast.periodMs*MS;
 advance(at+flight+MS);
 // Synthetic committed propagation observation, not a fabricated RF exchange.
 ranging.delayValid=true;ranging.epoch=followerClock.epoch;ranging.tof=flight;
 ranging.measuredMs=ticks-80;ranging.estimateId=cycle?cycle-1:0;
 ranging.aa=(mulTwrAa_t){.epoch=followerClock.epoch,.measuredMs=ticks-80,
   .sequence=cycle?cycle-1:0,.tof=flight,.reference=p->broadcast.ids[0],.follower=multiConfiguration.address[0],.valid=true};
 injectMulti(1,0,cycle,clockAt(0,at),clockAt(1,at+flight));
 TEST_ASSERT_TRUE(flags.tx);
}
static void deferredQuiet(const mulRangingPlan_t *p,unsigned cycle) {
 advance(((int64_t)cycle*p->broadcast.periodMs+p->broadcast.periodMs-19)*MS);
 dispatch(1,eventTimeout);
 TEST_ASSERT_FALSE(deferredClock.fit.active);
}
void test_deferredKalmanWaitsForConfirmedTxAndPreservesCurrentOriginsAt200And1000ms(void) {
 for(unsigned period=200;period<=1000;period+=800)for(unsigned first=133;first<=134;first++) {
  setUp();mulRangingPlan_t p;deferredConfiguration(&p,period,first);
  for(unsigned cycle=0;cycle<60;cycle++) {
   mulClockFit_t *fit=&mulClockWorkspace()->fit;unsigned before=fit->accepted;
   deferredReference(&p,cycle);
   TEST_ASSERT_EQUAL(before,fit->accepted); // Merely arming cannot run the update.
   if(cycle>10) {
    TEST_ASSERT_TRUE(deferredClock.fit.active);
    mulMultiBroadcast_t packet;TEST_ASSERT_TRUE(mulPlanBroadcastDecode(&p,radios[1].tx,80,&packet));
    TEST_ASSERT_TRUE(packet.relation.available);TEST_ASSERT_EQUAL(cycle,packet.relation.referenceSequence);
    TEST_ASSERT_EQUAL_UINT64(followerClock.referenceTx,packet.relation.referenceOrigin);
    TEST_ASSERT_INT32_WITHIN(2,15000,packet.relation.skewPpb);
    TEST_ASSERT_TRUE(packet.relation.originAgeMs<10); // Fresh origin, not the previous cycle.
   }
   uint32_t observed=followerClock.observedMs;
   advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
   if(cycle>10){TEST_ASSERT_TRUE(deferredClock.fit.active);TEST_ASSERT_EQUAL(before,fit->accepted);}
   // The other follower's imminent10ms slot postpones computation even after TX.
   deferredQuiet(&p,cycle);
   if(cycle>10){TEST_ASSERT_EQUAL(before+1,fit->accepted);TEST_ASSERT_TRUE(fit->applied);}
   TEST_ASSERT_EQUAL(observed,followerClock.observedMs); // Saved RX time, never worker time.
   unsigned completed=fit->accepted;
   advance(((int64_t)cycle*period+period-18)*MS);dispatch(1,eventTimeout);
   TEST_ASSERT_EQUAL(completed,fit->accepted); // No duplicate drain.
  }
 }
}
void test_deferredKalmanAdmissionRespectsActiveTwrAndTwelveMsDeadlineMargin(void) {
 setUp();mulRangingPlan_t p;deferredConfiguration(&p,200,133);
 for(unsigned cycle=0;cycle<15;cycle++) {
  deferredReference(&p,cycle);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);deferredQuiet(&p,cycle);
 }
 deferredReference(&p,15);unsigned accepted=mulClockWorkspace()->fit.accepted;
 advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 TEST_ASSERT_TRUE(deferredClock.fit.active);
 advance((15*200+11)*MS);
 ranging.active=true;ranging.deadline=clockAt(1,physical+5*MS);ranging.windowEnd=clockAt(1,physical+40*MS);
 (void)followerWait(&radios[1].device);
 TEST_ASSERT_TRUE(deferredClock.fit.active);TEST_ASSERT_EQUAL(accepted,mulClockWorkspace()->fit.accepted);
 ranging.active=false;
 // Last10ms before the next reference is too short for the10ms+margin budget.
 advance((15*200+190)*MS);(void)followerWait(&radios[1].device);
 TEST_ASSERT_TRUE(deferredClock.fit.active);TEST_ASSERT_EQUAL(accepted,mulClockWorkspace()->fit.accepted);
 // Starvation has consumed this cycle; retain the work until the next safe interval.
 deferredReference(&p,16);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 deferredQuiet(&p,16);
 TEST_ASSERT_FALSE(deferredClock.fit.active);TEST_ASSERT_EQUAL(accepted+1,mulClockWorkspace()->fit.accepted);
}
void test_deferredKalmanBacklogNeverOverwritesAcceptedObservationOrLabelsOldOriginCurrent(void) {
 setUp();mulRangingPlan_t p;deferredConfiguration(&p,200,134);
 for(unsigned cycle=0;cycle<15;cycle++) {
  deferredReference(&p,cycle);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);deferredQuiet(&p,cycle);
 }
 deferredReference(&p,15);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 TEST_ASSERT_TRUE(deferredClock.fit.active);uint32_t queued=deferredClock.packet.sequence;
 // Deliberately omit all worker wakeups until the next reference (scheduler fault).
 deferredReference(&p,16);TEST_ASSERT_EQUAL(queued,deferredClock.packet.sequence);TEST_ASSERT_EQUAL(10,slotReason);
 mulMultiBroadcast_t packet;TEST_ASSERT_TRUE(mulPlanBroadcastDecode(&p,radios[1].tx,80,&packet));
 TEST_ASSERT_FALSE(packet.relation.available);TEST_ASSERT_EQUAL(16,packet.relation.referenceSequence);
 advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);deferredQuiet(&p,16);
 TEST_ASSERT_EQUAL_UINT64(clockAt(0,15*200*MS),mulClockWorkspace()->fit.lastTx);
 TEST_ASSERT_EQUAL_INT(mulClockWarmup,followerClock.status);
 deferredReference(&p,17);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);deferredQuiet(&p,17);
 TEST_ASSERT_EQUAL_UINT64(clockAt(0,17*200*MS),mulClockWorkspace()->fit.lastTx);
}
void test_deferredKalmanEpochLossAndReinitializationCancelQueuedWorkSafely(void) {
 setUp();mulRangingPlan_t p;deferredConfiguration(&p,200,133);
 for(unsigned cycle=0;cycle<15;cycle++) {
  deferredReference(&p,cycle);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);deferredQuiet(&p,cycle);
 }
 deferredReference(&p,15);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 TEST_ASSERT_TRUE(deferredClock.fit.active);uint64_t epoch=followerClock.epoch;
 advance((15*200+7000)*MS);dispatch(1,eventTimeout);
 TEST_ASSERT_FALSE(deferredClock.fit.active);TEST_ASSERT_NOT_EQUAL(epoch,followerClock.epoch);
 TEST_ASSERT_FALSE(referenceAcquired);
 deferredReference(&p,51);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);deferredQuiet(&p,51);
 deferredReference(&p,52);TEST_ASSERT_TRUE(flags.tx);
 mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
 TEST_ASSERT_FALSE(flags.tx);TEST_ASSERT_FALSE(deferredClock.fit.active);TEST_ASSERT_FALSE(mulClockWorkspace()->fit.ready);
}
void test_deferredKalmanStaleCompletionAbortsHardwareBeforeReleasingObservation(void) {
 setUp();mulRangingPlan_t p;deferredConfiguration(&p,200,133);
 for(unsigned cycle=0;cycle<15;cycle++) {
  deferredReference(&p,cycle);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);deferredQuiet(&p,cycle);
 }
 deferredReference(&p,15);TEST_ASSERT_TRUE(deferredClock.fit.active);
 unsigned accepted=mulClockWorkspace()->fit.accepted;
 radios[1].actual=clockAt(1,14*200*MS); // A stale TX marker while the new slot is still armed.
 advance((15*200+2)*MS);dispatch(1,eventPacketSent);
 TEST_ASSERT_FALSE(flags.tx);TEST_ASSERT_FALSE(radios[1].armed);TEST_ASSERT_TRUE(radios[1].receiving);
 TEST_ASSERT_TRUE(deferredClock.fit.active);TEST_ASSERT_EQUAL(accepted,mulClockWorkspace()->fit.accepted);
 deferredQuiet(&p,15);TEST_ASSERT_EQUAL(accepted+1,mulClockWorkspace()->fit.accepted);
}

void test_tdmaDeferredWorkerStartsAtOwnConfirmedTxOverlappingOtherFollower(void) {
 const unsigned periods[]={200,250,500,1000};
 const unsigned budgets[]={5,10,20};
 for(unsigned k=0;k<4;k++)for(unsigned first=133;first<=134;first++)for(unsigned b=0;b<3;b++) {
  setUp();mulRangingPlan_t p;deferredConfiguration(&p,1000,first);
  mulRangingPlanTdma(&p);p.kalmanBudgetMs=budgets[b];p.broadcast.periodMs=periods[k];multiConfiguration.mulSchedule=p.broadcast;TEST_ASSERT_NULL(mulRangingPlanValidate(&p,NULL));
  mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
  for(unsigned cycle=0;cycle<30;cycle++) {
   unsigned before=mulClockWorkspace()->fit.accepted;deferredReference(&p,cycle);
   TEST_ASSERT_EQUAL(before,mulClockWorkspace()->fit.accepted);
   advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
   // Own slot is5ms, next follower broadcasts at10ms: drain independently now.
   TEST_ASSERT_TRUE(physical<((int64_t)cycle*periods[k]+10)*MS);
   TEST_ASSERT_FALSE(deferredClock.fit.active);
   if(cycle>10)TEST_ASSERT_EQUAL(before+1,mulClockWorkspace()->fit.accepted);
   unsigned done=mulClockWorkspace()->fit.accepted;(void)followerWait(&radios[1].device);
   TEST_ASSERT_EQUAL(done,mulClockWorkspace()->fit.accepted);
  }
 }
}
void test_tdmaDeferredWorkerHonorsBudgetAndGlobalTwrEvenWhenNotInPair(void) {
 setUp();mulRangingPlan_t p;deferredConfiguration(&p,200,134);mulRangingPlanTdma(&p);p.kalmanBudgetMs=20;
 mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
 for(unsigned cycle=0;cycle<15;cycle++) {
  deferredReference(&p,cycle);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 }
 deferredReference(&p,15);unsigned before=mulClockWorkspace()->fit.accepted;
 // Delay servicing completion until too close to the global34.1ms TWR start.
 advance((15*200+30)*MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 TEST_ASSERT_TRUE(deferredClock.fit.active);TEST_ASSERT_EQUAL(before,mulClockWorkspace()->fit.accepted);
 advance((15*200+40)*MS);unsigned wake=followerWait(&radios[1].device);
 TEST_ASSERT_TRUE(deferredClock.fit.active);TEST_ASSERT_EQUAL(before,mulClockWorkspace()->fit.accepted);
 TEST_ASSERT_TRUE(wake<=86); // Wake immediately after124.1ms reservation end, not180ms tail.
 advance((15*200+40+wake)*MS);(void)followerWait(&radios[1].device);
 TEST_ASSERT_FALSE(deferredClock.fit.active);TEST_ASSERT_EQUAL(before+1,mulClockWorkspace()->fit.accepted);
}

void test_tdmaActualTwrOneExchangePerCycleRotatesDespiteLossAndKeepsCalibration(void) {
 for(unsigned cycle=0;cycle<30;cycle++) {
  setUp();scenario(-150000,2131);exchangeSequence=cycle;model.sequence=cycle;
  mulRangingPlan_t p={.replyMs=20,.rlrCount=1,.rlrs={141}};
  mulScheduleLegacy(&p.broadcast,132,133,5);mulScheduleExperimental(&p.broadcast);
  p.broadcast.periodMs=200;p.broadcast.rangingFlags=0;p.broadcast.txLeadUs=1000;p.broadcast.rxBudgetUs=2000;
  mulRangingPlanTdma(&p);simulationPlan=&p;
  TEST_ASSERT_EQUAL(1,mulRangingPlanCount(&p,cycle));
  mulRangingOperation_t op;TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,cycle,0,&op));
  TEST_ASSERT_EQUAL(cycle%3==2?133:132,op.initiator);TEST_ASSERT_EQUAL(cycle%3==0?133:141,op.responder);
  advance((int64_t)op.startUs*MS/1000);
  TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),&p,cycle,0));TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(1),&p,cycle,0));
  TEST_ASSERT_TRUE(mulTwrStart(ctx(0),&radios[0].device,cycle,origins[0]));
  if(cycle%7==0) {
   advance((int64_t)op.endUs*MS/1000);dispatch(0,eventReceiveTimeout);
   TEST_ASSERT_FALSE(ctx(0)->active);TEST_ASSERT_FALSE(mulTwrStart(ctx(0),&radios[0].device,cycle,origins[0]));
  }else {
   completeExchange();TEST_ASSERT_FALSE(ctx(0)->active);TEST_ASSERT_FALSE(ctx(1)->active);
   TEST_ASSERT_UINT32_WITHIN(1,flight,ctx(1)->tof);
   TEST_ASSERT_EQUAL(op.kind==MUL_PLAN_AA,ctx(1)->aa.valid);
   TEST_ASSERT_TRUE(physical<(int64_t)op.endUs*MS/1000);
  }
  TEST_ASSERT_FALSE(mulTwrConfigurePlan(ctx(0),&p,cycle,0));
 }
}

static void prepConfiguration(mulRangingPlan_t *p,unsigned node) {
 deferredConfiguration(p,200,134);mulRangingPlanTdma(p);p->broadcast.revision=1;
 unsigned slot=node==134?1:2;TEST_ASSERT_TRUE(mulSchedulePair(&p->broadcast,slot,&multiConfiguration.mulNetwork));
 multiConfiguration.mulSchedule=p->broadcast;
 // mulNetwork_t has pair addresses0/1 even when the roster slot is2.
 memcpy(multiConfiguration.address,multiConfiguration.mulNetwork.addresses[1],8);
 mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
 for(unsigned cycle=0;cycle<15;cycle++) {
  deferredReference(p,cycle);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 }
}
void test_tdmaAdvanceMappingAvoidsOldLateSetupAtBothFollowerSlotsWithoutMovingPoll(void) {
 for(unsigned node=133;node<=134;node++)for(unsigned slow=0;slow<2;slow++)for(unsigned spi=0;spi<2;spi++) {
  setUp();mulRangingPlan_t p;prepConfiguration(&p,node);
  unsigned cycle=node==134?19:18;int64_t base=(int64_t)cycle*200*MS;
  deferredReference(&p,cycle);fitCost=(slow?9000:4400)*MS/1000;
  advance(sendTime(1)+1200*MS/1000);radios[1].armed=false;dispatch(1,eventPacketSent);
  TEST_ASSERT_FALSE(deferredClock.fit.active);fitCost=0;
  // Same timed mapping and1.188ms SPI setup in old/new models. All coordinates
  // come from the actual production mapper and exact55ADA235 plan.
  TEST_ASSERT_EQUAL_HEX32(0x55ADA235,mulRangingPlanFingerprint(&p));
  mapCost=300*MS/1000;mapCalls=0;advance(physical+MS);
  uint64_t began=readHardwareTime(&radios[1].device);int64_t oldPhysical=physical;
  mulRangingOperation_t op;TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,cycle,0,&op));
  mulLocalReservation_t local;TEST_ASSERT_TRUE(mapReservation(&op,ticks,&local,NULL));
  // Old wait used its pre-mapping SYS_TIME, then repeated mapping at wakeup.
  int64_t until=mulTimestampDifference(local.start,began);
  unsigned oldWait=(unsigned)((until+MS-1)/MS);advance(physical+oldWait*MS);
  TEST_ASSERT_TRUE(mapReservation(&op,ticks,&local,NULL));
  TEST_ASSERT_TRUE(mulTwrConfigurePlan(&ranging,&p,cycle,0));
  radios[1].setupLag=1188*MS/1000;unsigned sent=radios[1].sends;
  TEST_ASSERT_FALSE(mulTwrStartTargets(&ranging,&radios[1].device,cycle,local.poll,local.end));
  TEST_ASSERT_EQUAL(sent,radios[1].sends);TEST_ASSERT_TRUE(ranging.attempted);
  int64_t oldLead=mulTimestampDifference(radios[1].raw,radios[1].now);
  // Recreate only the old-op retirement state so the same reference/mathematics
  // can exercise the new production owner at its post-Kalman point.
  ranging.attempted=ranging.active=false;servedOperations=0;followerPrep.ready=false;
  advance(oldPhysical);mapCalls=0;radios[1].setupLag=0;
  unsigned wait=followerWait(&radios[1].device);TEST_ASSERT_TRUE(followerPrep.ready);TEST_ASSERT_EQUAL(4,mapCalls);
  TEST_ASSERT_TRUE(physical<base+24*MS);TEST_ASSERT_EQUAL(sent,radios[1].sends);
  unsigned newSetup=spi?2000:1188;radios[1].setupLag=newSetup*MS/1000;
  for(unsigned wake=0;wake<4&&!ranging.active;wake++) {
   advance(physical+(int64_t)wait*MS);wait=dispatch(1,eventTimeout);
  }
  if(!ranging.active)printf("PREPFAIL node=%u slow=%u spi=%u time=%lld lead=%lld served=%lu epoch=%lu/%lu status=%u\n",node,slow,spi,(long long)(physical-base)/MS,(long long)mulTimestampDifference(radios[1].raw,radios[1].now)/MS,(unsigned long)servedOperations,(unsigned long)followerPrep.epoch,(unsigned long)followerClock.epoch,followerClock.status);
  TEST_ASSERT_TRUE(ranging.active);TEST_ASSERT_EQUAL(sent+1,radios[1].sends);TEST_ASSERT_EQUAL(4,mapCalls);
  int64_t newLead=mulTimestampDifference(radios[1].raw,radios[1].now);
  TEST_ASSERT_TRUE(newLead>1000*(int64_t)MS/1000);
  printf("PREP MODEL node=%u fit_us=%u setup_us=%u old_lead_us=%.3f new_lead_us=%.3f\n",node,slow?9000:4400,newSetup,(double)oldLead/63897.6,(double)newLead/63897.6);
  mulTwrPacket_t poll;TEST_ASSERT_TRUE(mulPlanTwrDecode(&p,cycle,0,radios[1].tx,90,&poll));
  TEST_ASSERT_EQUAL_UINT64(ranging.expectedTx,poll.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(local.poll,followerPrep.local.poll);
  TEST_ASSERT_INT64_WITHIN(511,local.poll,poll.txTimestamp);
  // Confirm actual radio exchange and current-cycle retirement, not just arming.
  advance(physical+newSetup*MS/1000);radios[1].setupLag=0;exchangeSequence=cycle;
  origins[0]=clockAt(0,base);origins[1]=clockAt(1,base+flight);protocolRole[0]=mulAlrFollower;
  TEST_ASSERT_TRUE(mulTwrConfigurePlan(ctx(0),&p,cycle,0));mulRxInit(&radios[0].device);
  transfer(1);transfer(0);transfer(1);transfer(0);transfer(1);
  TEST_ASSERT_FALSE(ranging.active);TEST_ASSERT_TRUE(ranging.aa.valid);TEST_ASSERT_EQUAL(4,mapCalls);
  sent=radios[1].sends;dispatch(1,eventTimeout);TEST_ASSERT_EQUAL(sent,radios[1].sends);
 }
}

void test_tdmaPreparedOpportunityRejectsImpossibleHardwareAndChangedEpochOnce(void) {
 for(unsigned fault=0;fault<3;fault++) {
  setUp();mulRangingPlan_t p;prepConfiguration(&p,134);unsigned cycle=19;
  deferredReference(&p,cycle);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
  advance(physical+MS);(void)followerWait(&radios[1].device);TEST_ASSERT_TRUE(followerPrep.ready);
  unsigned sent=radios[1].sends;
  if(fault==0)radios[1].setupLag=3*MS;
  if(fault==1)followerClock.epoch++;
  advance(((int64_t)cycle*200+(fault==2?29:25))*MS);dispatch(1,eventTimeout);
  TEST_ASSERT_EQUAL(sent,radios[1].sends);TEST_ASSERT_FALSE(ranging.active);TEST_ASSERT_EQUAL(1,servedOperations);
  radios[1].setupLag=0;advance(physical+MS);dispatch(1,eventTimeout);TEST_ASSERT_EQUAL(sent,radios[1].sends);
  mulRangingOperation_t next;TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,cycle+1,0,&next));
  TEST_ASSERT_EQUAL(132,next.initiator);TEST_ASSERT_EQUAL(133,next.responder);
 }
}
void test_tdmaAdvancePreparationUsesEveryFollowerSlotAndRosterSize(void) {
 for(unsigned count=2;count<=8;count++)for(unsigned slot=1;slot<count;slot++)for(unsigned spacing=5;spacing<=10;spacing+=5) {
  setUp();mulRangingPlan_t p;deferredConfiguration(&p,200,134);mulRangingPlanTdma(&p);
  p.broadcast.count=count;p.broadcast.slotMs=spacing;
  for(unsigned i=0;i<8;i++)p.broadcast.ids[i]=i<count?100+i:0;
  TEST_ASSERT_NULL(mulRangingPlanValidate(&p,NULL));
  multiConfiguration.mulSchedule=p.broadcast;TEST_ASSERT_TRUE(mulSchedulePair(&p.broadcast,slot,&multiConfiguration.mulNetwork));
  memcpy(multiConfiguration.address,multiConfiguration.mulNetwork.addresses[1],8);
  mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
  unsigned cycle=16;while(cycle%(2*count-1)!=count-1+slot)cycle++;
  for(unsigned warm=0;warm<cycle;warm++) {
   deferredReference(&p,warm);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
  }
  deferredReference(&p,cycle);fitCost=4400*MS/1000;
  advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);TEST_ASSERT_FALSE(deferredClock.fit.active);fitCost=0;
  mapCost=300*MS/1000;mapCalls=0;advance(physical+MS);unsigned wait=followerWait(&radios[1].device);
  if(!followerPrep.ready)printf("PREPGEN n=%u slot=%u spacing=%u c=%u status=%u served=%lu calls=%u fit=%u/%u\n",count,slot,spacing,cycle,followerClock.status,(unsigned long)servedOperations,mapCalls,mulClockWorkspace()->fit.ready,mulClockWorkspace()->fit.accepted);
  TEST_ASSERT_TRUE(followerPrep.ready);TEST_ASSERT_EQUAL(4,mapCalls);unsigned sent=radios[1].sends;radios[1].setupLag=2*MS;
  for(unsigned wake=0;wake<4&&!ranging.active;wake++){advance(physical+(int64_t)wait*MS);wait=dispatch(1,eventTimeout);}
  TEST_ASSERT_TRUE(ranging.active);TEST_ASSERT_EQUAL(sent+1,radios[1].sends);TEST_ASSERT_EQUAL(4,mapCalls);
 }
}

void test_tdmaPreparedExternalGeometryAllowsFreshPosesButRejectsWithdrawalAndDomainReset(void) {
 for(unsigned change=0;change<3;change++) {
  setUp();mulRangingPlan_t p;prepConfiguration(&p,134);unsigned cycle=19;
  deferredReference(&p,cycle);advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
  mulGeometrySelect(mulDelayExternal);TEST_ASSERT_TRUE(mulGeometryBegin(1,1,1,0));
  mulPose_t a={.clock=1,.epoch=1,.frame=1,.sequence=1,.sourceUs=1000000,.node=132,.flags=7,.ttlMs=1000,.receivedMs=ticks};
  a.covarianceMm2[0]=a.covarianceMm2[3]=a.covarianceMm2[5]=1;
  mulPose_t b=a;b.node=134;b.positionMm[0]=1000;
  TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&a));TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&b));mulGeometryPoll(ticks);
  advance(physical+MS);(void)followerWait(&radios[1].device);TEST_ASSERT_TRUE(followerPrep.ready);
  unsigned sent=radios[1].sends;
  if(change==2){TEST_ASSERT_TRUE(mulGeometryBegin(1,1,1,0));}
  a.sequence=b.sequence=2;a.sourceUs=b.sourceUs=1001000;
  if(change==1)a.flags=6;
  TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&a));TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&b));mulGeometryPoll(ticks);
  advance(((int64_t)cycle*200+25)*MS);dispatch(1,eventTimeout);
  TEST_ASSERT_EQUAL(sent+(change==0),radios[1].sends);
  TEST_ASSERT_EQUAL(1,servedOperations);
 }
}

void test_liveVerbosityPreservesDelayedBroadcastBytesAndKalmanEvolution(void) {
 uint8_t expectedFrame[80];mulClock_t expectedClock;mulClockFit_t expectedFit;
 for(unsigned level=0;level<3;level++) {
  setUp();mulRangingPlan_t p;prepConfiguration(&p,134);
  followerClock.epoch=1234; // Identical synthetic boot token in independent runs.
  mulClock_t clock=followerClock;mulClockFit_t fit=mulClockWorkspace()->fit;
  unsigned attempted=servedOperations,sends=radios[1].sends;bool pending=flags.tx;
  mulLogSetLevel(level); // The actual live API, without reinit or a new epoch.
  TEST_ASSERT_EQUAL_MEMORY(&clock,&followerClock,sizeof(clock));
  TEST_ASSERT_EQUAL_MEMORY(&fit,&mulClockWorkspace()->fit,sizeof(fit));
  TEST_ASSERT_EQUAL(attempted,servedOperations);TEST_ASSERT_EQUAL(pending,flags.tx);
  TEST_ASSERT_EQUAL(sends,radios[1].sends);
  deferredReference(&p,17);
  if(level)TEST_ASSERT_EQUAL_MEMORY(expectedFrame,radios[1].tx,80);
  else memcpy(expectedFrame,radios[1].tx,80);
  advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);deferredQuiet(&p,17);
  if(level){TEST_ASSERT_EQUAL_MEMORY(&expectedClock,&followerClock,sizeof(expectedClock));
   TEST_ASSERT_EQUAL_MEMORY(&expectedFit,&mulClockWorkspace()->fit,sizeof(expectedFit));}
  else {expectedClock=followerClock;expectedFit=mulClockWorkspace()->fit;}
 }
}

static void restartConfiguration(mulRangingPlan_t *p,unsigned node,unsigned method) {
 deferredConfiguration(p,1000,134);mulRangingPlanTdma(p);
 p->broadcast.revision=1;p->broadcast.periodMs=100;p->replyMs=10;p->twrWindowMs=50;
 TEST_ASSERT_NULL(mulRangingPlanValidate(p,NULL));
 TEST_ASSERT_EQUAL_HEX32(0x2359421F,mulRangingPlanFingerprint(p));
 multiConfiguration.mulSchedule=p->broadcast;
 TEST_ASSERT_TRUE(mulSchedulePair(&p->broadcast,node==134?1:2,&multiConfiguration.mulNetwork));
 memcpy(multiConfiguration.address,multiConfiguration.mulNetwork.addresses[1],8);
 multiConfiguration.mulClockConfig.estimator=method;multiConfiguration.mulClockConfig.compensate=0;
 mulAlrAlgorithm.init(&multiConfiguration,&radios[1].device);
}
static bool restartReference(const mulRangingPlan_t *p,uint32_t cycle,int64_t at,uint64_t remote) {
 advance(at+flight+MS);injectMulti(1,0,cycle,remote,clockAt(1,at+flight));
 if(!flags.tx)return false;
 mulMultiBroadcast_t out;TEST_ASSERT_TRUE(mulPlanBroadcastDecode(p,radios[1].tx,80,&out));
 int64_t slot=mulTimestampDifference(out.packet.txTimestamp,clockAt(1,at+flight));
 TEST_ASSERT_TRUE(slot>=(int64_t)localSlot*5*MS&&slot<(int64_t)localSlot*5*MS+512);
 advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);
 advance(at+81*MS);dispatch(1,eventTimeout);TEST_ASSERT_FALSE(deferredClock.fit.active);
 return true;
}
void test_tdma100msReferenceRestartsAndContinuousLossRecoverBothFollowersAndEstimators(void) {
 const unsigned gaps[]={1000,4000,10000},methods[]={0,2};
 for(unsigned node=133;node<=134;node++)for(unsigned method=0;method<2;method++)
 for(unsigned gap=0;gap<3;gap++)for(unsigned kind=0;kind<3;kind++) {
  setUp();mulRangingPlan_t p;restartConfiguration(&p,node,methods[method]);
  for(unsigned i=0;i<40;i++)TEST_ASSERT_TRUE(restartReference(&p,100+i,(int64_t)i*100*MS,clockAt(0,(int64_t)i*100*MS)));
  TEST_ASSERT_EQUAL_INT(mulClockMapValid,mulClockMapStatus(&followerClock,ticks));
  uint64_t epoch=followerClock.epoch;unsigned accepted=mulClockWorkspace()->fit.accepted;
  int64_t start=(3900+gaps[gap])*MS;
  for(unsigned repeat=0;repeat<(kind?3u:1u);repeat++) {
   for(unsigned i=0;i<35;i++) {
    int64_t at=start+(int64_t)i*100*MS;
    uint32_t seq=kind?i:139+gaps[gap]/100+i;
    uint64_t remote=clockAt(0,kind==2?(int64_t)i*100*MS:at);
    bool sent=restartReference(&p,seq,at,remote);
    TEST_ASSERT_TRUE(sent||i==0); // Only the unconfirmed restart candidate may be withheld.
    if(kind&&i==1) {
     TEST_ASSERT_NOT_EQUAL(epoch,followerClock.epoch);
     TEST_ASSERT_FALSE(ranging.delayValid);TEST_ASSERT_FALSE(ranging.aa.valid);
     TEST_ASSERT_FALSE(followerPrep.ready);TEST_ASSERT_FALSE(ranging.active);
    }
    if(i>10) {
     TEST_ASSERT_EQUAL_INT(mulClockMapValid,mulClockMapStatus(&followerClock,ticks));
     mulRangingOperation_t op;TEST_ASSERT_EQUAL(1,mulRangingPlanCount(&p,seq));
     TEST_ASSERT_TRUE(mulRangingPlanOperation(&p,seq,0,&op));
     const unsigned initiators[]={132,132,132,133,134},responders[]={133,134,141,141,141};
     TEST_ASSERT_EQUAL(initiators[seq%5],op.initiator);TEST_ASSERT_EQUAL(responders[seq%5],op.responder);
     if(op.initiator==node) {
      mulLocalReservation_t mapped;TEST_ASSERT_TRUE(mapReservation(&op,ticks,&mapped,NULL));
     }
    }
   }
   if(kind){TEST_ASSERT_NOT_EQUAL(epoch,followerClock.epoch);epoch=followerClock.epoch;}
   start+=4500*MS;
  }
  if(!kind&&(gaps[gap]<3000||(methods[method]==2&&gaps[gap]<=6000))) {
   TEST_ASSERT_EQUAL_UINT64(epoch,followerClock.epoch);
   if(methods[method]==2){TEST_ASSERT_TRUE(mulClockWorkspace()->fit.accepted>accepted);}
  }
 }
}
void test_tdmaReferenceRestartCancelsPendingWorkerArmedSlotAndOldReservation(void) {
 for(unsigned fault=0;fault<3;fault++) {
  setUp();mulRangingPlan_t p;restartConfiguration(&p,134,2);
  for(unsigned i=0;i<40;i++)TEST_ASSERT_TRUE(restartReference(&p,100+i,(int64_t)i*100*MS,clockAt(0,(int64_t)i*100*MS)));
  // Leave accepted work deliberately undrained, with no intervening worker wake.
  advance(4000*MS+flight+MS);injectMulti(1,0,140,clockAt(0,4000*MS),clockAt(1,4000*MS+flight));
  TEST_ASSERT_TRUE(flags.tx);TEST_ASSERT_TRUE(deferredClock.fit.active);
  mulClockFitPending_t stale=deferredClock.fit;mulTestPacket_t old=deferredClock.packet;
  uint64_t oldRx=deferredClock.rx;uint32_t oldMs=deferredClock.now;
  uint64_t epoch=followerClock.epoch;
  if(fault!=0){advance(sendTime(1)+MS);radios[1].armed=false;dispatch(1,eventPacketSent);}
  if(fault==1){followerPrep.ready=true;followerPrep.epoch=epoch;}
  if(fault==2){ranging.active=true;ranging.delayValid=true;ranging.aa.valid=true;ranging.aa.epoch=epoch;}
  for(unsigned i=0;i<12;i++) {
   int64_t at=(4100+(int64_t)i*100)*MS;
   (void)restartReference(&p,i,at,clockAt(0,at));
  }
  TEST_ASSERT_NOT_EQUAL(epoch,followerClock.epoch);TEST_ASSERT_FALSE(deferredClock.fit.active);
  TEST_ASSERT_FALSE(ranging.delayValid);TEST_ASSERT_FALSE(ranging.aa.valid);
  TEST_ASSERT_EQUAL_INT(mulClockMapValid,mulClockMapStatus(&followerClock,ticks));
  mulClock_t before=followerClock;mulClockFit_t saved=mulClockWorkspace()->fit;
  deferredClock.fit=stale;deferredClock.packet=old;deferredClock.rx=oldRx;deferredClock.now=oldMs;
  deferredClock.rawSkew=12345;deferredClock.rawStatus=mulClockValid;
  finishClock(&radios[1].device,readHardwareTime(&radios[1].device));
  TEST_ASSERT_FALSE(deferredClock.fit.active);
  TEST_ASSERT_EQUAL_MEMORY(&before,&followerClock,sizeof(before));
  mulClockFitFinish(alrNetwork,&followerClock,&mulClockWorkspace()->fit,&mulClockWorkspace()->config,
    &old,oldRx,oldMs,&stale);
  TEST_ASSERT_EQUAL_MEMORY(&before,&followerClock,sizeof(before));
  TEST_ASSERT_EQUAL_MEMORY(&saved,&mulClockWorkspace()->fit,sizeof(saved));
 }
}
