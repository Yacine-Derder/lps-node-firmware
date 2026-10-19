/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_alr.h"
#include "mul_rx.h"
#include "task.h"
#include "mul_common.h"
#include "mul_clock.h"

#include <string.h>

// Exercise the real driver and its SPI header encoding, not a fake that assumes
// dwSetTxRxTime() and packet timestamps agree. Only the SPI device is simulated.
#include "../vendor/libdw1000/src/libdw1000.c"
#include "../vendor/libdw1000/src/libdw1000Spi.c"

// Include the LDE sub-register address range used by normal RX configuration.
static uint8_t registers[64][0x3000];
static uint8_t transmitted[MUL_TEST_FRAME_LENGTH];
static dwDevice_t radio;
static uwbConfig_t nodeConfig;
static uint64_t systemTime;
TickType_t xTaskGetTickCount(void) { return 0; }

static bool started;
static unsigned int completionCount;
static unsigned int receiveCount;

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
  mulAlrAlgorithm.onEvent(device, eventPacketSent);
}

static void receivedCallback(dwDevice_t *device) {
  receiveCount++;
  mulAlrAlgorithm.onEvent(device, eventPacketReceived);
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
  TEST_ASSERT_TRUE(mulDecodeTestPacket(transmitted, sizeof(transmitted), &decoded));
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
  TEST_ASSERT_TRUE(mulDecodeTestPacket(transmitted, sizeof(transmitted), &packet));
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
