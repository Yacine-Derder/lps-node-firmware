/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_alr.h"
#include "mul_rlr.h"
#include "mul_common.h"

#include <string.h>

/* Small fake radio: linking only these functions also guards against accidental
 * new radio operations (in particular TX from the RLR). */
static dwDevice_t dev;
static uwbConfig_t config;
static unsigned int txCount, rxCount, idleCount, readCount, timestampCount;
static unsigned int rxLength;
static uint8_t txFrame[MUL_TEST_FRAME_LENGTH + 2];
static uint8_t rxFrame[MUL_TEST_FRAME_LENGTH];
static bool receiving, configuring;

void dwIdle(dwDevice_t *device) {
  TEST_ASSERT_EQUAL_PTR(&dev, device);
  receiving = false;
  idleCount++;
}
void dwReceivePermanently(dwDevice_t *device, bool enabled) {
  (void)device;
  TEST_ASSERT_FALSE(enabled);
}
void dwNewTransmit(dwDevice_t *device) { dwIdle(device); }
void dwSetDefaults(dwDevice_t *device) { (void)device; }
void dwSetData(dwDevice_t *device, uint8_t *data, unsigned int length) {
  (void)device;
  TEST_ASSERT_EQUAL_UINT(MUL_TEST_FRAME_LENGTH, length);
  // Match the real driver's extra FCS read; sanitizers catch an undersized buffer.
  memcpy(txFrame, data, length + 2);
}
void dwWaitForResponse(dwDevice_t *device, bool enabled) {
  (void)device;
  TEST_ASSERT_FALSE(enabled);
}
void dwStartTransmit(dwDevice_t *device) { (void)device; txCount++; }
void dwNewConfiguration(dwDevice_t *device) { dwIdle(device); configuring = true; }
void dwSetFrameFilter(dwDevice_t *device, bool enabled) {
  (void)device;
  TEST_ASSERT_TRUE(configuring);
  TEST_ASSERT_FALSE(enabled);
}
void dwSetReceiverAutoReenable(dwDevice_t *device, bool enabled) {
  (void)device;
  TEST_ASSERT_FALSE(enabled);
}
void dwSetReceiveWaitTimeout(dwDevice_t *device, uint16_t timeout) {
  (void)device;
  TEST_ASSERT_EQUAL_UINT16(0, timeout);
}
void dwInterruptOnReceiveFailed(dwDevice_t *device, bool enabled) {
  (void)device;
  TEST_ASSERT_TRUE(enabled);
}
void dwCommitConfiguration(dwDevice_t *device) {
  (void)device;
  TEST_ASSERT_TRUE(configuring);
  configuring = false;
}
void dwNewReceive(dwDevice_t *device) { dwIdle(device); }
void dwStartReceive(dwDevice_t *device) {
  (void)device;
  TEST_ASSERT_FALSE(configuring);
  receiving = true;
  rxCount++;
}
unsigned int dwGetDataLength(dwDevice_t *device) { (void)device; return rxLength; }
void dwGetData(dwDevice_t *device, uint8_t *data, unsigned int length) {
  (void)device;
  TEST_ASSERT_EQUAL_UINT(sizeof(rxFrame), length);
  memcpy(data, rxFrame, length);
  readCount++;
}
void dwGetRawReceiveTimestamp(dwDevice_t *device, dwTime_t *time) {
  (void)device;
  TEST_ASSERT_FALSE(receiving);
  time->full = UINT64_C(0xfedcba9876);
  timestampCount++;
}

void setUp(void) {
  memset(&config, 0, sizeof(config));
  const uint8_t address[] = {0x84, 0, 0, 0, 0, 0, 0xcf, 0xbc};
  memcpy(config.address, address, sizeof(address));
  txCount = rxCount = idleCount = readCount = timestampCount = 0;
  receiving = configuring = false;
  rxLength = sizeof(rxFrame);
  mulEncodeTestPacket(rxFrame, config.address, 42);
}
void tearDown(void) {}

void test_mulAlrRepeatsTransmissionAndRecoversFromMissingCompletion(void) {
  mulAlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  for (uint32_t sequence = 0; sequence < 1000; sequence++) {
    TEST_ASSERT_EQUAL_UINT32(1000, mulAlrAlgorithm.onEvent(&dev, eventTimeout));
    mulTestPacket_t packet;
    TEST_ASSERT_TRUE(mulDecodeTestPacket(txFrame, MUL_TEST_FRAME_LENGTH, &packet));
    TEST_ASSERT_EQUAL_UINT32(sequence, packet.sequence);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(config.address, packet.source, sizeof(packet.source));
    // Drop one completion: the next timeout must abort and send a new sequence.
    if (sequence != 500) {
      TEST_ASSERT_EQUAL_UINT32(1000, mulAlrAlgorithm.onEvent(&dev, eventPacketSent));
    }
  }
  TEST_ASSERT_EQUAL_UINT(1000, txCount);
  TEST_ASSERT_EQUAL_UINT(0, rxCount);
  TEST_ASSERT_EQUAL_UINT(1002, idleCount); // init + sends + recovery
  TEST_ASSERT_EQUAL_UINT32(1000, mulAlrAlgorithm.onEvent(&dev, eventReceiveFailed));
  TEST_ASSERT_EQUAL_UINT32(1000, mulAlrAlgorithm.onEvent(&dev, eventReceiveTimeout));
  TEST_ASSERT_EQUAL_UINT32(1000, mulAlrAlgorithm.onEvent(&dev, eventTimeout));
  TEST_ASSERT_EQUAL_UINT(1001, txCount);
}

void test_mulRlrRearmsAfterValidMalformedAndUnrelatedPackets(void) {
  mulRlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_TRUE(receiving);
  receiving = false; // hardware stops RX before delivering the event
  TEST_ASSERT_EQUAL_UINT32(MAX_TIMEOUT, mulRlrAlgorithm.onEvent(&dev, eventPacketReceived));
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_EQUAL_UINT(1, timestampCount);
  rxFrame[21] ^= 1;
  mulRlrAlgorithm.onEvent(&dev, eventPacketReceived);
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_EQUAL_UINT(1, timestampCount);
  for (unsigned int length = 0; length <= 1024; length++) {
    if (length != sizeof(rxFrame)) {
      rxLength = length;
      TEST_ASSERT_EQUAL_UINT32(MAX_TIMEOUT, mulRlrAlgorithm.onEvent(&dev, eventPacketReceived));
      TEST_ASSERT_TRUE(receiving);
    }
  }
  TEST_ASSERT_EQUAL_UINT(2, readCount);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
}

void test_mulRlrRearmsOnTimeoutsAndReceiveFailuresWithoutTransmitting(void) {
  mulRlrAlgorithm.init(&config, &dev);
  const uwbEvent_t events[] = {eventTimeout, eventReceiveTimeout, eventReceiveFailed, eventPacketSent};
  for (unsigned int i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
    TEST_ASSERT_EQUAL_UINT32(MAX_TIMEOUT, mulRlrAlgorithm.onEvent(&dev, events[i]));
    TEST_ASSERT_TRUE(receiving);
  }
  TEST_ASSERT_EQUAL_UINT(5, rxCount);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_EQUAL_UINT(0, readCount);
}
