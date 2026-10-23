/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_twr.h"
#include "mul_schedule.h"
#include "mul_multi_protocol.h"
#include "mul_relation.h"
#include "mul_role_state.h"
#include "mul_downlink.h"
#include "mul_localization.h"
#include "mul_power.h"
#include "mul_observation.h"
#include <math.h>
#include "mul_common.h"
#include "mul_clock.h"
#include "mul_rx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mul_alr.c"
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
static const mulSchedule_t *simulationSchedule;
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
  TEST_ASSERT_EQUAL_UINT8(DX_TIME,reg); TEST_ASSERT_EQUAL_UINT32(NO_SUB,sub); TEST_ASSERT_EQUAL_UINT(5,size);
  mulEncodeTimestamp(data, radio(dev)->raw+radio(dev)->dxError);
}
uint16_t dwSpiRead16(dwDevice_t *dev, uint8_t reg, uint32_t sub) {
  if (reg == TX_ANTD) { TEST_ASSERT_EQUAL_UINT32(NO_SUB,sub); return radio(dev)->antenna; }
  if (reg == LDE_IF) { TEST_ASSERT_EQUAL_UINT32(LDE_RXANTD_SUB,sub); return radio(dev)->rxAntenna; }
  TEST_ASSERT_EQUAL_UINT8(SYS_STATUS,reg); TEST_ASSERT_EQUAL_UINT32(3,sub); return radio(dev)->status;
}
void dwSpiWrite(dwDevice_t *dev, uint8_t reg, uint32_t sub, const void *data, size_t size) {
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
static int64_t sendTime(int node) {
  return physical+llroundl((mulTimestampDifference(radios[node].raw,radios[node].now)+radios[node].physicalTx)/rate(node));
}
static mulTwr_t *ctx(int node) { return useAlr[node] ? &ranging : &contexts[node]; }
static mulClock_t *followerModel(void) { return useAlr[1] ? &followerClock : &model; }
static uint32_t dispatch(int node, uwbEvent_t event) {
  if (useAlr[node]) return mulAlrAlgorithm.onEvent(&radios[node].device,event);
  mulTwrOnEvent(ctx(node), &radios[node].device,event,(mulAlrRole_t)node,exchangeSequence,
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
  skew=0; flight=0; physical=framePhysical=0; ticks=5000;
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
  if(ctx(from)->multiProfile) {unsigned slot;TEST_ASSERT_TRUE(mulMultiTwrDecode(simulationSchedule,ctx(from)->syncWindow,payload,length,&packet,&slot));}
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
  TEST_ASSERT_TRUE(referenceAcquired); TEST_ASSERT_TRUE(flags.tx);
  TEST_ASSERT_EQUAL_INT(mulClockWarmup,followerClock.status);
  TEST_ASSERT_TRUE(followerClock.epoch == oldEpoch); // Loss already issued the clean epoch.
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
 r->rxLength=mulMultiBroadcastEncode(r->rx,sizeof(r->rx),&multiConfiguration.mulSchedule,slot,cycle,tx,slot?&unavailable:NULL);
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
