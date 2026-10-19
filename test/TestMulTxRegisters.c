/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_alr.h"
#include "mul_rx.h"
#include "task.h"
#include "mul_common.h"
#include "mul_clock.h"
#include "mul_twr.h"
#include "mul_power.h"

#include <string.h>

// Exercise the real driver and its SPI header encoding, not a fake that assumes
// dwSetTxRxTime() and packet timestamps agree. Only the SPI device is simulated.
#include "../vendor/libdw1000/src/libdw1000.c"
#include "../vendor/libdw1000/src/libdw1000Spi.c"

// Include the LDE sub-register address range used by normal RX configuration.
static uint8_t registers[64][0x3000];
static uint8_t transmitted[MUL_TWR_FRAME_LENGTH];
static dwDevice_t radio;
static uwbConfig_t nodeConfig;
static uint64_t systemTime;
TickType_t xTaskGetTickCount(void) { return 0; }

static bool started;
static unsigned int completionCount;
static unsigned int receiveCount;
static mulTwr_t *directTwr;
static uint64_t directOrigin;
static uint64_t directReferenceOrigin;
static uint64_t directEpoch;
static mulAlrRole_t directRole;

static unsigned int spiOffset(const uint8_t *header, size_t length) {
  unsigned int offset = length > 1 ? header[1] & 0x7f : 0;
  if (length > 2) offset |= (unsigned int)header[2] << 7;
  return offset;
}

static void radioRead(dwDevice_t *device, const void *header, size_t headerLength,
                      void *data, size_t length) {
  (void)device;
  const uint8_t *bytes = header;
  unsigned int reg = bytes[0] & 0x3f;
  unsigned int offset = spiOffset(bytes, headerLength);
  TEST_ASSERT_TRUE(offset + length <= sizeof(registers[reg]));
  if (reg == SYS_TIME) mulEncodeTimestamp(registers[reg], systemTime);
  memcpy(data, registers[reg] + offset, length);
}

static void radioWrite(dwDevice_t *device, const void *header, size_t headerLength,
                       const void *data, size_t length) {
  (void)device;
  const uint8_t *bytes = header;
  unsigned int reg = bytes[0] & 0x3f;
  unsigned int offset = spiOffset(bytes, headerLength);
  TEST_ASSERT_TRUE(offset + length <= sizeof(registers[reg]));
  if (reg == SYS_STATUS) {
    for (size_t i = 0; i < length; i++)
      registers[reg][offset + i] &= ~((const uint8_t *)data)[i];
    return;
  }
  memcpy(registers[reg] + offset, data, length);
  if (reg == SYS_CTRL && (registers[reg][0] & (1 << TXSTRT_BIT))) {
    TEST_ASSERT_BITS_HIGH(1 << TXDLYS_BIT, registers[reg][0]);
    uint64_t raw = mulDecodeTimestamp(registers[DX_TIME]) & ~UINT64_C(511);
    uint16_t antenna = registers[TX_ANTD][0] | (uint16_t)registers[TX_ANTD][1] << 8;
    // The DW1000 reports the RMARKER raw target plus TX_ANTD; no preamble offset.
    mulEncodeTimestamp(registers[TX_TIME], raw + antenna);
    memcpy(transmitted, registers[TX_BUFFER], sizeof(transmitted));
    started = true;
  }
}

static void sentCallback(dwDevice_t *device) {
  completionCount++;
  if (directTwr) mulTwrOnEvent(directTwr,device,eventPacketSent,directRole,5,directOrigin,directReferenceOrigin,directEpoch,0);
  else mulAlrAlgorithm.onEvent(device, eventPacketSent);
}

static void receivedCallback(dwDevice_t *device) {
  receiveCount++;
  if (directTwr) mulTwrOnEvent(directTwr,device,eventPacketReceived,directRole,5,directOrigin,directReferenceOrigin,directEpoch,0);
  else mulAlrAlgorithm.onEvent(device, eventPacketReceived);
}

static dwOps_t radioOps = {.spiRead = radioRead, .spiWrite = radioWrite};

void setUp(void) {
  memset(registers, 0, sizeof(registers));
  memset(&radio, 0, sizeof(radio));
  memset(&nodeConfig, 0, sizeof(nodeConfig));
  nodeConfig.address[0] = 0x84;
  nodeConfig.address[6] = 0xcf;
  nodeConfig.address[7] = 0xbc;
  systemTime = 0;
  started = false;
  completionCount = 0;
  receiveCount = 0;
  directTwr = NULL;
  directRole = mulAlrReference; directEpoch = 0; directReferenceOrigin = 0;
  dwInit(&radio, &radioOps);
  dwAttachSentHandler(&radio, sentCallback);
  dwAttachReceivedHandler(&radio, receivedCallback);
}
void tearDown(void) {}

static void checkTimestamp(uint64_t expectedRaw, uint16_t antenna) {
  registers[TX_ANTD][0] = antenna;
  registers[TX_ANTD][1] = antenna >> 8;
  systemTime = mulTimestampMask(expectedRaw - UINT64_C(1277952000)); // 20 ms
  mulAlrAlgorithm.init(&nodeConfig, &radio);
  TEST_ASSERT_EQUAL_UINT32(100, mulAlrAlgorithm.onEvent(&radio, eventTimeout));
  TEST_ASSERT_TRUE(started);
  mulTestPacket_t decoded;
  TEST_ASSERT_TRUE(mulDecodeTestPacket(transmitted, MUL_TEST_FRAME_LENGTH, &decoded));
  TEST_ASSERT_EQUAL_UINT32(0, decoded.sequence);
  TEST_ASSERT_EQUAL_UINT8(0x84, decoded.source[0]);
  TEST_ASSERT_EQUAL_UINT64(expectedRaw, mulDecodeTimestamp(registers[DX_TIME]));
  dwTime_t actual = {.full = 0};
  dwGetTransmitTimestamp(&radio, &actual);
  TEST_ASSERT_EQUAL_UINT64(mulTimestampMask(expectedRaw + antenna), actual.full);
  TEST_ASSERT_EQUAL_UINT64(actual.full, decoded.txTimestamp);
  registers[SYS_STATUS][0] |= 1 << TXFRS_BIT;
  dwHandleInterrupt(&radio);
  TEST_ASSERT_EQUAL_UINT(1, completionCount);
}

void test_mulPacketThroughRealDriverMatchesReportedHardwareTimestamp(void) {
  checkTimestamp(UINT64_C(0x07eb1d0200), 0);
}

void test_mulPacketThroughRealDriverIncludesAntennaDelayAcrossWrap(void) {
  checkTimestamp(MUL_TIMESTAMP_MASK & ~UINT64_C(511), 0x4321);
}

void test_mulFollowerRealDriverTurnsRxIntoDelayedTxAndBack(void) {
  nodeConfig.address[0] = 0x85;
  mulAlrAlgorithm.init(&nodeConfig, &radio);
  TEST_ASSERT_EQUAL_UINT(RX_MODE, radio.deviceMode);
  TEST_ASSERT_FALSE(started);
  const uint8_t reference[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  mulEncodeTestPacket(registers[RX_BUFFER], reference, 42, UINT64_C(0x0123456789));
  registers[RX_FINFO][0] = MUL_TEST_FRAME_LENGTH + 2; // Hardware length includes FCS.
  const uint64_t rx = UINT64_C(0xfedcba9876);
  mulEncodeTimestamp(registers[RX_TIME], rx);
  systemTime = mulTimestampMask(rx + 2 * UINT64_C(63897600));
  registers[SYS_STATUS][1] = 1 << (RXFCG_BIT - 8);
  dwHandleInterrupt(&radio);
  TEST_ASSERT_EQUAL_UINT(1, receiveCount);
  TEST_ASSERT_EQUAL_UINT(IDLE_MODE, radio.deviceMode); // Driver caches IDLE while delayed TX waits.
  TEST_ASSERT_TRUE(started);
  mulTestPacket_t packet;
  TEST_ASSERT_TRUE(mulDecodeTestPacket(transmitted, MUL_TEST_FRAME_LENGTH, &packet));
  TEST_ASSERT_EQUAL_UINT8(0x85, packet.source[0]);
  TEST_ASSERT_EQUAL_UINT32(0, packet.sequence);
  int64_t slot = mulTimestampDifference(packet.txTimestamp, rx);
  TEST_ASSERT_TRUE(slot >= INT64_C(31948800000) && slot < INT64_C(31948800512));
  TEST_ASSERT_EQUAL_UINT64(mulDecodeTimestamp(registers[TX_TIME]), packet.txTimestamp);
  TEST_ASSERT_EQUAL_UINT(0, completionCount);
  systemTime = mulTimestampMask(packet.txTimestamp + UINT64_C(63897600));
  registers[SYS_STATUS][0] = 1 << TXFRS_BIT;
  dwHandleInterrupt(&radio);
  TEST_ASSERT_EQUAL_UINT(1, completionCount);
  TEST_ASSERT_EQUAL_UINT(RX_MODE, radio.deviceMode);
  started = false;
  // A valid interrupt with an oversized frame must also return to RX, never TX.
  registers[RX_FINFO][0] = 0xff;
  registers[RX_FINFO][1] = 3;
  registers[SYS_STATUS][1] = 1 << (RXFCG_BIT - 8);
  dwHandleInterrupt(&radio);
  TEST_ASSERT_EQUAL_UINT(2, receiveCount);
  TEST_ASSERT_EQUAL_UINT(RX_MODE, radio.deviceMode);
  TEST_ASSERT_FALSE(started);
}

void test_mulRxSetupPreservesLiveAntennaCalibrationRegisters(void) {
  registers[TX_ANTD][0]=0x34; registers[TX_ANTD][1]=0x12;
  registers[LDE_IF][LDE_RXANTD_SUB]=0x78; registers[LDE_IF][LDE_RXANTD_SUB+1]=0x56;
  mulRxInit(&radio);
  TEST_ASSERT_EQUAL_HEX16(0x1234,dwSpiRead16(&radio,TX_ANTD,NO_SUB));
  TEST_ASSERT_EQUAL_HEX16(0x5678,dwSpiRead16(&radio,LDE_IF,LDE_RXANTD_SUB));
  TEST_ASSERT_EQUAL_UINT(RX_MODE,radio.deviceMode);
}

static void twrRealTxComplete(void) {
  mulTwrPacket_t packet;
  TEST_ASSERT_TRUE(mulTwrDecode(transmitted,MUL_TWR_FRAME_LENGTH,&packet));
  TEST_ASSERT_EQUAL_UINT64(mulDecodeTimestamp(registers[TX_TIME]),packet.txTimestamp);
  systemTime=mulTimestampMask(packet.txTimestamp+UINT64_C(63897600));
  registers[SYS_STATUS][0] |= 1 << TXFRS_BIT;
  dwHandleInterrupt(&radio);
  TEST_ASSERT_EQUAL_UINT(RX_MODE,radio.deviceMode);
}

static void twrRealRx(mulTwrPacket_t *packet, uint64_t arrival) {
  mulTwrEncode(registers[RX_BUFFER],packet);
  registers[RX_FINFO][0]=MUL_TWR_FRAME_LENGTH+2;
  mulEncodeTimestamp(registers[RX_TIME],arrival);
  systemTime=mulTimestampMask(arrival+UINT64_C(63897600));
  registers[SYS_STATUS][1] |= 1 << (RXFCG_BIT-8);
  dwHandleInterrupt(&radio);
}

void test_mulTwrRealDriverPollFinalResultTimestampsAndRxTransitions(void) {
  const uint64_t ms=UINT64_C(63897600);
  const uint64_t tof=1000;
  mulTwr_t context; mulTwrInit(&context); directTwr=&context;
  directOrigin=MUL_TIMESTAMP_MASK-640*ms;
  directReferenceOrigin=directOrigin;
  systemTime=mulTimestampMask(directOrigin+630*ms);
  mulAntennaConfigure(&radio);
  mulRxInit(&radio);
  TEST_ASSERT_TRUE(mulTwrStart(&context,&radio,5,directOrigin));
  uint64_t p1=context.stamps[0]; twrRealTxComplete();
  mulTwrPacket_t response={.type=mulTwrResponse,.id=5};
  response.stamps[0]=p1; response.stamps[1]=UINT64_C(0xfedcba9876);
  response.stamps[2]=mulTimestampMask(response.stamps[1]+20*ms);
  response.txTimestamp=response.stamps[2];
  twrRealRx(&response,mulTimestampMask(p1+20*ms+2*tof));
  TEST_ASSERT_TRUE(context.txPending); TEST_ASSERT_EQUAL_INT(mulTwrFinal,context.stage);
  twrRealTxComplete();
  mulTwrPacket_t report={.type=mulTwrReport,.id=5};
  memcpy(report.stamps,context.stamps,sizeof(report.stamps));
  report.stamps[5]=mulTimestampMask(response.stamps[1]+mulTimestampDifference(context.stamps[4],p1));
  report.txTimestamp=mulTimestampMask(report.stamps[5]+20*ms);
  twrRealRx(&report,mulTimestampMask(context.stamps[4]+20*ms+2*tof));
  TEST_ASSERT_TRUE(context.txPending); TEST_ASSERT_EQUAL_INT(mulTwrResult,context.stage);
  TEST_ASSERT_EQUAL_UINT32(tof,context.tof);
  twrRealTxComplete();
  TEST_ASSERT_FALSE(context.active); TEST_ASSERT_EQUAL_UINT(3,completionCount);
  TEST_ASSERT_EQUAL_UINT(2,receiveCount); directTwr=NULL;
}

void test_mulTwrRealDriverResponseReportAndResultAcceptance(void) {
  const uint64_t ms=UINT64_C(63897600), tof=2131;
  mulTwr_t context; mulTwrInit(&context); directTwr=&context;
  directRole=mulAlrFollower; directEpoch=7;
  directOrigin=MUL_TIMESTAMP_MASK-680*ms;
  directReferenceOrigin=UINT64_C(0x123456789a);
  mulAntennaConfigure(&radio);
  mulRxInit(&radio);
  mulTwrPacket_t poll={.type=mulTwrPoll,.id=5};
  poll.stamps[0]=mulTimestampMask(directReferenceOrigin+650*ms); poll.txTimestamp=poll.stamps[0];
  uint64_t p2=mulTimestampMask(directOrigin+650*ms);
  twrRealRx(&poll,p2);
  TEST_ASSERT_TRUE(context.txPending); TEST_ASSERT_EQUAL_INT(mulTwrResponse,context.stage);
  twrRealTxComplete();
  mulTwrPacket_t final={.type=mulTwrFinal,.id=5};
  memcpy(final.stamps,context.stamps,sizeof(final.stamps));
  uint64_t db=(uint64_t)mulTimestampDifference(context.stamps[2],p2);
  final.stamps[3]=mulTimestampMask(final.stamps[0]+db+2*tof);
  final.stamps[4]=mulTimestampMask(final.stamps[3]+20*ms); final.txTimestamp=final.stamps[4];
  twrRealRx(&final,mulTimestampMask(context.stamps[2]+20*ms+2*tof));
  TEST_ASSERT_TRUE(context.txPending); TEST_ASSERT_EQUAL_INT(mulTwrReport,context.stage);
  twrRealTxComplete(); TEST_ASSERT_FALSE(context.delayValid);
  mulTwrPacket_t result={.type=mulTwrResult,.id=5,.tof=tof};
  memcpy(result.stamps,context.stamps,sizeof(result.stamps));
  result.txTimestamp=mulTimestampMask(context.stamps[4]+40*ms);
  twrRealRx(&result,mulTimestampMask(context.stamps[5]+40*ms));
  TEST_ASSERT_TRUE(context.delayValid); TEST_ASSERT_FALSE(context.active);
  TEST_ASSERT_EQUAL_UINT32(tof,context.tof); TEST_ASSERT_EQUAL_UINT64(7,context.epoch);
  TEST_ASSERT_EQUAL_UINT(2,completionCount); TEST_ASSERT_EQUAL_UINT(3,receiveCount);
  TEST_ASSERT_EQUAL_UINT(RX_MODE,radio.deviceMode); directTwr=NULL;
}

void test_mulAntennaInitializesMissingZeroAndMismatchedRegistersWithoutPhyChange(void) {
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(154.6 * 63897600000.0 / 299792458.0 + 0.5),
                          MUL_ANTENNA_TX_DTU + MUL_ANTENNA_RX_DTU);
  // Reproduce uwbInit: the legacy zero cache is committed to both directions.
  radio.antennaDelay.full=0;
  dwNewConfiguration(&radio); dwSetDefaults(&radio); dwCommitConfiguration(&radio);
  TEST_ASSERT_FALSE(mulAntennaValid(&radio));
  uint8_t channel[LEN_CHAN_CTRL]; memcpy(channel,registers[CHAN_CTRL],sizeof(channel));
  const uint16_t bad[]={0,1,0xffff};
  for (unsigned int i=0;i<3;i++) {
    const uint8_t tx[]={bad[i] & 0xff,bad[i] >> 8};
    const uint8_t rx[]={bad[2-i] & 0xff,bad[2-i] >> 8};
    dwSpiWrite(&radio,TX_ANTD,NO_SUB,tx,sizeof(tx));
    dwSpiWrite(&radio,LDE_IF,LDE_RXANTD_SUB,rx,sizeof(rx));
    mulAntennaConfigure(&radio);
    TEST_ASSERT_TRUE(mulAntennaValid(&radio));
    TEST_ASSERT_EQUAL_UINT16(14498,dwSpiRead16(&radio,TX_ANTD,NO_SUB));
    TEST_ASSERT_EQUAL_UINT16(18453,dwSpiRead16(&radio,LDE_IF,LDE_RXANTD_SUB));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(channel,registers[CHAN_CTRL],sizeof(channel));
    mulRxInit(&radio); dwNewTransmit(&radio); dwSetDefaults(&radio); mulRxRearm(&radio);
    TEST_ASSERT_TRUE(mulAntennaValid(&radio));
  }
}

void test_mulAntennaDetectsCacheMismatchAndFullCommitOverwrite(void) {
  mulAntennaConfigure(&radio);
  radio.antennaDelay.full=0;
  TEST_ASSERT_FALSE(mulAntennaValid(&radio));
  mulAntennaConfigure(&radio);
  dwNewConfiguration(&radio); dwCommitConfiguration(&radio);
  // This driver only has one cached delay; full commit writes TX into RX.
  TEST_ASSERT_FALSE(mulAntennaValid(&radio));
  mulTwr_t twr; mulTwrInit(&twr);
  TEST_ASSERT_FALSE(mulTwrStart(&twr,&radio,5,systemTime));
  TEST_ASSERT_FALSE(twr.active); TEST_ASSERT_FALSE(twr.delayValid); TEST_ASSERT_FALSE(started);
  mulAntennaConfigure(&radio); TEST_ASSERT_TRUE(mulAntennaValid(&radio));
}

// Set frozen DW1000 diagnostic registers for a chosen RSL, through the REAL
// power estimator. Quantization is deliberate; reference correction uses that RSL.
static void powerAt(float dbm, uint8_t prf) {
  radio.channel=CHANNEL_2; radio.pulseFrequency=prf;
  const unsigned int n=dbm > -78 ? 100 : 1000;
  float a=prf==TX_PULSE_FREQ_16MHZ ? 113.77f : 121.74f;
  float fac=prf==TX_PULSE_FREQ_16MHZ ? 2.3334f : 1.1667f;
  float estimated=dbm <= -88 ? dbm : (dbm-88*fac)/(1+fac);
  uint16_t c=(uint16_t)lroundf(powf(10,(estimated+a)/10)*n*n/131072);
  registers[RX_FINFO][2]=(n & 15)<<4; registers[RX_FINFO][3]=n>>4;
  registers[RX_FQUAL][CIR_PWR_SUB]=c; registers[RX_FQUAL][CIR_PWR_SUB+1]=c>>8;
}

void test_mulPowerReusesBitcrazeInterpolationSignAndKeepsRawAtWrap(void) {
  const float powers[]={-94,-90,-80,-79,-78,-76,-75,-64,-62};
  for (unsigned int prf=1;prf<=2;prf++) for (unsigned int i=0;i<9;i++) {
    powerAt(powers[i],prf);
    dwTime_t reference={.full=0}; dwCorrectTimestamp(&radio,&reference);
    mulPowerRx_t result=mulPowerCapture(&radio,0);
    TEST_ASSERT_EQUAL_INT(mulPowerApplied,result.power.status);
    TEST_ASSERT_EQUAL_UINT(prf,result.power.table);
    TEST_ASSERT_EQUAL_UINT64(0,result.raw);
    TEST_ASSERT_EQUAL_UINT64(mulTimestampMask(reference.full),result.corrected);
    TEST_ASSERT_EQUAL_INT64(mulTimestampDifference(reference.full,0),result.power.deltaDtu);
    if (powers[i] <= -83 || (prf == 2 && powers[i] <= -80)) TEST_ASSERT_TRUE(result.power.deltaDtu<0);
    if (powers[i] >= -75) TEST_ASSERT_TRUE(result.power.deltaDtu>0);
  }
  powerAt(-80,TX_PULSE_FREQ_64MHZ);
  float base=-(dwGetReceivePower(&radio)+61)*0.5f;
  int expected=-(int)((21+(base-9)*14)*DISTANCE_OF_RADIO_INV*0.001f);
  TEST_ASSERT_EQUAL_INT(expected,mulPowerCapture(&radio,0).power.deltaDtu);
  TEST_ASSERT_EQUAL_INT(-5,expected); // About +28 mm range bias: RX moves earlier.
}

void test_mulPowerUnavailableDoesNotAlterTimestamp(void) {
  const float outside[]={-100,-96,-60,-50};
  for (unsigned int i=0;i<4;i++) {
    powerAt(outside[i],TX_PULSE_FREQ_64MHZ);
    mulPowerRx_t result=mulPowerCapture(&radio,MUL_TIMESTAMP_MASK);
    TEST_ASSERT_EQUAL_INT(mulPowerOutOfRange,result.power.status);
    TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK,result.raw);
    TEST_ASSERT_EQUAL_UINT64(result.raw,result.corrected);
    TEST_ASSERT_EQUAL_INT(0,result.power.deltaDtu);
  }
  memset(registers[RX_FINFO],0,LEN_RX_FINFO);
  memset(registers[RX_FQUAL],0,LEN_RX_FQUAL);
  TEST_ASSERT_EQUAL_INT(mulPowerInvalid,mulPowerCapture(&radio,123).power.status);
  powerAt(-80,TX_PULSE_FREQ_64MHZ);
  radio.channel=CHANNEL_4;
  TEST_ASSERT_EQUAL_INT(mulPowerUnsupported,mulPowerCapture(&radio,123).power.status);
  radio.channel=0;
  TEST_ASSERT_EQUAL_INT(mulPowerUnsupported,mulPowerCapture(&radio,123).power.status);
  radio.channel=CHANNEL_2; radio.pulseFrequency=0;
  TEST_ASSERT_EQUAL_INT(mulPowerUnsupported,mulPowerCapture(&radio,123).power.status);
}
