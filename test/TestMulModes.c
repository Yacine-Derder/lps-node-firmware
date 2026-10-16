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
static bool receiving, configuring, delayedArmed;
static uint64_t hardwareNow, delayedRaw, actualTx;
static uint64_t setupDelay;
static uint16_t antennaDelay, txError;
static unsigned int actualReads, aborts;

#define DTU_PER_MS UINT64_C(63897600)
#define PERIOD_DTU (1000 * DTU_PER_MS)

void dwGetSystemTimestamp(dwDevice_t *device, dwTime_t *time) {
  (void)device;
  time->full = hardwareNow;
}
void dwGetTransmitTimestamp(dwDevice_t *device, dwTime_t *time) {
  (void)device;
  actualReads++;
  time->full = actualTx;
}
void dwSetTxRxTime(dwDevice_t *device, const dwTime_t time) {
  (void)device;
  TEST_ASSERT_EQUAL_UINT64(0, time.full & 511);
  delayedRaw = time.full;
  delayedArmed = true;
  hardwareNow = mulTimestampMask(hardwareNow + setupDelay);
}
uint16_t dwSpiRead16(dwDevice_t *device, uint8_t reg, uint32_t offset) {
  (void)device;
  if (reg == TX_ANTD) {
    TEST_ASSERT_EQUAL_UINT32(NO_SUB, offset);
    return antennaDelay;
  }
  TEST_ASSERT_EQUAL_UINT8(SYS_STATUS, reg);
  TEST_ASSERT_EQUAL_UINT32(3, offset);
  return txError;
}
void dwSpiWrite(dwDevice_t *device, uint8_t reg, uint32_t offset,
                const void *data, size_t length) {
  (void)device;
  const uint8_t expected[] = {0x08, 0x04};
  TEST_ASSERT_EQUAL_UINT8(SYS_STATUS, reg);
  TEST_ASSERT_EQUAL_UINT32(3, offset);
  TEST_ASSERT_EQUAL_UINT(sizeof(expected), length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, length);
}


void dwIdle(dwDevice_t *device) {
  TEST_ASSERT_EQUAL_PTR(&dev, device);
  receiving = false;
  if (delayedArmed) aborts++;
  delayedArmed = false;
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
void dwStartTransmit(dwDevice_t *device) {
  (void)device;
  TEST_ASSERT_TRUE(delayedArmed);
  txCount++;
}
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
  receiving = configuring = delayedArmed = false;
  hardwareNow = setupDelay = actualTx = delayedRaw = 0;
  antennaDelay = 0x4321;
  txError = 0;
  actualReads = aborts = 0;
  rxLength = sizeof(rxFrame);
  mulEncodeTestPacket(rxFrame, config.address, 42, UINT64_C(0x0123456789));
}
void tearDown(void) {}

static uint32_t elapseAndTimeout(uint32_t ms) {
  hardwareNow = mulTimestampMask(hardwareNow + ms * DTU_PER_MS);
  return mulAlrAlgorithm.onEvent(&dev, eventTimeout);
}

static uint32_t completeTx(int64_t delta) {
  actualTx = mulTimestampMask(delayedRaw + antennaDelay + delta);
  hardwareNow = mulTimestampMask(delayedRaw + DTU_PER_MS);
  delayedArmed = false; // hardware TX has completed
  return mulAlrAlgorithm.onEvent(&dev, eventPacketSent);
}

void test_mulAlrMaintainsHardwareCadenceAcrossWrapWithConfiguredAntennaDelay(void) {
  hardwareNow = MUL_TIMESTAMP_MASK - 10 * DTU_PER_MS + 123;
  mulAlrAlgorithm.init(&config, &dev);
  uint64_t previousRaw = 0;
  uint32_t wait = 0;
  for (uint32_t sequence = 0; sequence < 1000; sequence++) {
    TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
    mulTestPacket_t packet;
    TEST_ASSERT_TRUE(mulDecodeTestPacket(txFrame, MUL_TEST_FRAME_LENGTH, &packet));
    TEST_ASSERT_EQUAL_UINT32(sequence, packet.sequence);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(config.address, packet.source, sizeof(packet.source));
    TEST_ASSERT_EQUAL_UINT64(mulTimestampMask(delayedRaw + antennaDelay), packet.txTimestamp);
    if (sequence != 0) {
      TEST_ASSERT_EQUAL_INT64(PERIOD_DTU, mulTimestampDifference(delayedRaw, previousRaw));
    }
    previousRaw = delayedRaw;
    wait = completeTx(sequence == 0 ? 7 : 0);
    TEST_ASSERT_TRUE(wait >= 978 && wait <= 980);
  }
  TEST_ASSERT_EQUAL_UINT(1000, txCount);
  TEST_ASSERT_EQUAL_UINT(1000, actualReads);
  TEST_ASSERT_EQUAL_UINT(0, rxCount);
  TEST_ASSERT_EQUAL_UINT(0, aborts);
  mulAlrAlgorithm.onEvent(&dev, eventPacketSent); // duplicate completion is ignored
  TEST_ASSERT_EQUAL_UINT(1000, actualReads);
}

void test_mulAlrAbortsBothDelayedStartErrorsAndContinues(void) {
  mulAlrAlgorithm.init(&config, &dev);
  const uint16_t errors[] = {0x0008, 0x0400}; // HPDWARN, TXPUTE
  uint32_t wait = 0;
  for (unsigned int i = 0; i < 2; i++) {
    txError = errors[i];
    wait = elapseAndTimeout(wait);
    TEST_ASSERT_FALSE(delayedArmed);
    TEST_ASSERT_EQUAL_UINT(i + 1, aborts);
    mulAlrAlgorithm.onEvent(&dev, eventPacketSent); // stale event is not success
    TEST_ASSERT_EQUAL_UINT(0, actualReads);
  }
  txError = 0;
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  completeTx(-7);
  TEST_ASSERT_EQUAL_UINT(1, actualReads);
}

void test_mulAlrRecoversFromMissingCompletionAndRadioErrors(void) {
  mulAlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(0));
  uint32_t wait = elapseAndTimeout(100);
  TEST_ASSERT_FALSE(delayedArmed);
  TEST_ASSERT_EQUAL_UINT(0, actualReads);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  wait = mulAlrAlgorithm.onEvent(&dev, eventReceiveFailed);
  TEST_ASSERT_FALSE(delayedArmed);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  wait = mulAlrAlgorithm.onEvent(&dev, eventReceiveTimeout);
  TEST_ASSERT_FALSE(delayedArmed);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  completeTx(0);
  TEST_ASSERT_EQUAL_UINT(1, actualReads);
  TEST_ASSERT_EQUAL_UINT(3, aborts);
}

void test_mulAlrSkipsLateSetupWithoutStartingInvalidTransmission(void) {
  mulAlrAlgorithm.init(&config, &dev);
  setupDelay = 18 * DTU_PER_MS; // SPI setup consumed the safe lead time
  uint32_t wait = elapseAndTimeout(0);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_FALSE(delayedArmed);
  setupDelay = 0;
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  completeTx(0);
  TEST_ASSERT_EQUAL_UINT(1, actualReads);
}

void test_mulAlrSkipsMissedPeriodsWithoutLosingPhaseOrBursting(void) {
  mulAlrAlgorithm.init(&config, &dev);
  uint32_t wait = elapseAndTimeout(2500);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_TRUE(wait > 0 && wait <= 1000);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  TEST_ASSERT_EQUAL_UINT64(20 * DTU_PER_MS + 3 * PERIOD_DTU, delayedRaw);
  completeTx(0);
}

void test_mulAlrAntennaTimestampWrapsAndZeroDelayNeedsNoCorrection(void) {
  hardwareNow = (MUL_TIMESTAMP_MASK & ~UINT64_C(511)) - 20 * DTU_PER_MS;
  mulAlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(0));
  mulTestPacket_t packet;
  TEST_ASSERT_TRUE(mulDecodeTestPacket(txFrame, MUL_TEST_FRAME_LENGTH, &packet));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x4121), packet.txTimestamp);
  completeTx(-7);

  antennaDelay = 0;
  mulAlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(0));
  TEST_ASSERT_TRUE(mulDecodeTestPacket(txFrame, MUL_TEST_FRAME_LENGTH, &packet));
  TEST_ASSERT_EQUAL_UINT64(delayedRaw, packet.txTimestamp);
  completeTx(0);
}

void test_mulAlrReportsFullWidthSignedDeltaWithoutTruncation(void) {
  mulAlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(0));
  uint32_t wait = completeTx((INT64_C(1) << 39) - 1);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  completeTx(-(INT64_C(1) << 39));
  TEST_ASSERT_EQUAL_UINT(2, actualReads);
}

void test_mulAlrRebasesAfterAnAmbiguousLongStall(void) {
  mulAlrAlgorithm.init(&config, &dev);
  uint32_t wait = elapseAndTimeout(9000);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_EQUAL_UINT32(1, wait);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  TEST_ASSERT_TRUE(mulTimestampDifference(delayedRaw, hardwareNow) >=
                   (int64_t)(5 * DTU_PER_MS));
  completeTx(0);
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
