/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_rx.h"
#include "mul_common.h"

#include <string.h>

// As in the TDoA2 tests, include the algorithms to inspect their bounded state.
#include "../src/mul_alr.c"
#include "../src/mul_rlr.c"

/* Small fake radio: linking only these functions also guards against accidental
 * new radio operations (in particular TX from the RLR). */
static dwDevice_t dev;
static uwbConfig_t config;
static unsigned int txCount, rxCount, idleCount, readCount, timestampCount;
static unsigned int rxLength;
static uint8_t capturedTxFrame[MUL_TEST_FRAME_LENGTH + 2];
static uint8_t rxFrame[MUL_TEST_FRAME_LENGTH];
static bool receiving, configuring, delayedArmed;
static uint64_t hardwareNow, delayedRaw, actualTx;
static uint64_t setupDelay;
static uint16_t antennaDelay, txError;
static unsigned int actualReads, aborts;
static uint64_t programmedTimeError;
static uint64_t packetTimestampError;
static TickType_t testTick;
static uint64_t incomingTimestamp;

TickType_t xTaskGetTickCount(void) { return testTick; }

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
void dwSpiRead(dwDevice_t *device, uint8_t reg, uint32_t offset,
               void *data, size_t length) {
  (void)device;
  TEST_ASSERT_EQUAL_UINT8(DX_TIME, reg);
  TEST_ASSERT_EQUAL_UINT32(NO_SUB, offset);
  TEST_ASSERT_EQUAL_UINT(MUL_TIMESTAMP_BYTES, length);
  mulEncodeTimestamp(data, delayedRaw + programmedTimeError);
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
  if (packetTimestampError != 0) {
    mulEncodeTimestamp(data + MUL_TX_TIMESTAMP_OFFSET,
      mulDecodeTimestamp(data + MUL_TX_TIMESTAMP_OFFSET) - packetTimestampError);
  }
  // Match the real driver's extra FCS read; sanitizers catch an undersized buffer.
  memcpy(capturedTxFrame, data, length + 2);
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
  time->full = incomingTimestamp;
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
  programmedTimeError = 0;
  packetTimestampError = 0;
  testTick = 0;
  incomingTimestamp = UINT64_C(0xfedcba9876);
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
    TEST_ASSERT_TRUE(mulDecodeTestPacket(capturedTxFrame, MUL_TEST_FRAME_LENGTH, &packet));
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
  TEST_ASSERT_TRUE(mulDecodeTestPacket(capturedTxFrame, MUL_TEST_FRAME_LENGTH, &packet));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x4121), packet.txTimestamp);
  completeTx(-7);

  antennaDelay = 0;
  mulAlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(0));
  TEST_ASSERT_TRUE(mulDecodeTestPacket(capturedTxFrame, MUL_TEST_FRAME_LENGTH, &packet));
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

void test_mulAlrRejectsDisagreementWithProgrammedDelayedTxRegister(void) {
  mulAlrAlgorithm.init(&config, &dev);
  programmedTimeError = UINT64_C(0x809e00);
  uint32_t wait = elapseAndTimeout(0);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_FALSE(delayedArmed);
  programmedTimeError = 0;
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(wait));
  completeTx(0);
}

void test_mulAlrReadsAntennaDelayForEachEncodedPacket(void) {
  mulAlrAlgorithm.init(&config, &dev);
  antennaDelay = 0x1234; // The configured value changed after initialization.
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(0));
  mulTestPacket_t decoded;
  TEST_ASSERT_TRUE(mulDecodeTestPacket(capturedTxFrame, MUL_TEST_FRAME_LENGTH, &decoded));
  TEST_ASSERT_EQUAL_UINT64(mulTimestampMask(delayedRaw + antennaDelay), decoded.txTimestamp);
  completeTx(0);
}

void test_mulAlrCompletionDeltaUsesPacketBytesInsteadOfSchedulingCache(void) {
  mulAlrAlgorithm.init(&config, &dev);
  // Inject the reported discrepancy before the fake radio copies the bytes. The output
  // check must see a nonzero delta even when scheduled == hardware actual.
  packetTimestampError = UINT64_C(0x809e00);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(0));
  mulTestPacket_t decoded;
  TEST_ASSERT_TRUE(mulDecodeTestPacket(capturedTxFrame, MUL_TEST_FRAME_LENGTH, &decoded));
  TEST_ASSERT_EQUAL_UINT64(mulTimestampMask(delayedRaw + antennaDelay - packetTimestampError),
                         decoded.txTimestamp);
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

static uint32_t deliver(const uint8_t address[8], uint32_t sequence,
                        uint64_t tx, uint64_t rx, bool follower) {
  mulEncodeTestPacket(rxFrame, address, sequence, tx);
  rxLength = sizeof(rxFrame);
  incomingTimestamp = rx;
  receiving = false; // Hardware auto-reenable is off.
  return follower ? mulAlrAlgorithm.onEvent(&dev, eventPacketReceived) :
                    mulRlrAlgorithm.onEvent(&dev, eventPacketReceived);
}

static void initFollower(void) {
  config.address[0] = 0x85;
  mulAlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_EQUAL_INT(mulAlrFollower, alrRole);
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_FALSE(referenceAcquired);
}

void test_mulFollowerAcquiresTracksSequenceAndPreservesBothClockDomains(void) {
  initFollower();
  const uint8_t reference[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  TEST_ASSERT_EQUAL_UINT32(3000, deliver(reference, 10, MUL_TIMESTAMP_MASK, 2, true));
  TEST_ASSERT_TRUE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK, followerReference.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(2, followerReference.rxTimestamp);
  testTick = 1000;
  deliver(reference, 13, 5, 9, true);
  TEST_ASSERT_EQUAL_UINT32(2, followerReference.gaps);
  testTick = 1100;
  TEST_ASSERT_EQUAL_UINT32(2900, deliver(reference, 13, 99, 100, true));
  testTick = 1200;
  TEST_ASSERT_EQUAL_UINT32(2800, deliver(reference, 12, 99, 100, true));
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.duplicates);
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.outOfOrder);
  TEST_ASSERT_EQUAL_UINT32(13, followerReference.sequence);
  TEST_ASSERT_EQUAL_UINT64(5, followerReference.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(9, followerReference.rxTimestamp);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
}

void test_mulFollowerLosesReferenceDespiteTrafficAndReacquiresAfterRestart(void) {
  initFollower();
  const uint8_t reference[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  deliver(reference, 42, 7, 10, true);
  testTick = 2999;
  TEST_ASSERT_EQUAL_UINT32(1, deliver(reference, 42, 7, 10, true));
  TEST_ASSERT_TRUE(referenceAcquired);
  testTick = 3000;
  deliver(reference, 42, 7, 10, true);
  TEST_ASSERT_FALSE(referenceAcquired); // A replay cannot reacquire.
  testTick = 4000;
  deliver(reference, 0, 1, 20, true); // Reference reboot after loss.
  TEST_ASSERT_TRUE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT32(0, followerReference.sequence);
  TEST_ASSERT_EQUAL_UINT32(0, followerReference.gaps); // No enormous false gap.
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
}

void test_mulFollowerLossDeadlineSurvivesRtosWrapAndSilence(void) {
  initFollower();
  const uint8_t reference[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  testTick = UINT32_MAX - 1000;
  deliver(reference, 0, MUL_TIMESTAMP_MASK - 1, MUL_TIMESTAMP_MASK, true);
  testTick += 1000;
  deliver(reference, 1, 1, 2, true);
  TEST_ASSERT_TRUE(referenceAcquired);
  testTick += 2999;
  TEST_ASSERT_EQUAL_UINT32(1, mulAlrAlgorithm.onEvent(&dev, eventTimeout));
  testTick++;
  TEST_ASSERT_EQUAL_UINT32(3000, mulAlrAlgorithm.onEvent(&dev, eventTimeout));
  TEST_ASSERT_FALSE(referenceAcquired);
  testTick++;
  deliver(reference, 2, 9, 12, true);
  TEST_ASSERT_TRUE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT64(9, followerReference.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(12, followerReference.rxTimestamp);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
}

void test_mulFollowerRearmsAndNeverTransmitsForEveryEventAndWrongSource(void) {
  initFollower();
  uint8_t other[8] = {0x84,0,0,0,0,0,0xcf,0xbd}; // Same low ID, different identity.
  deliver(other, 0, 0, 0, true);
  other[0] = 0x85; other[7] = 0xbc;
  deliver(other, 0, 0, 0, true);
  TEST_ASSERT_FALSE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT32(0, followerReference.received);
  for (uwbEvent_t event = eventTimeout; event <= eventReceiveFailed; event++) {
    rxFrame[21] = 0; // Malformed packet for RX event.
    TEST_ASSERT_EQUAL_UINT32(3000, mulAlrAlgorithm.onEvent(&dev, event));
    TEST_ASSERT_TRUE(receiving);
  }
  for (unsigned int length = 0; length <= 1024; length++) {
    if (length != sizeof(rxFrame)) {
      rxLength = length;
      mulAlrAlgorithm.onEvent(&dev, eventPacketReceived);
      TEST_ASSERT_TRUE(receiving);
    }
  }
  TEST_ASSERT_FALSE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_EQUAL_UINT(0, actualReads);
}

void test_mulUnsupportedAddressNeverTransmitsOrStartsReceive(void) {
  for (unsigned int byte = 0; byte < 8; byte++) {
    const uint8_t reference[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
    memcpy(config.address, reference, 8);
    config.address[byte] ^= 2; // Never accidentally select the supported follower.
    mulAlrAlgorithm.init(&config, &dev);
    TEST_ASSERT_EQUAL_INT(mulAlrUnsupported, alrRole);
    for (uwbEvent_t event = eventTimeout; event <= eventReceiveFailed; event++) {
      TEST_ASSERT_EQUAL_UINT32(MAX_TIMEOUT, mulAlrAlgorithm.onEvent(&dev, event));
      TEST_ASSERT_FALSE(receiving);
    }
  }
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_EQUAL_UINT(0, rxCount);
  TEST_ASSERT_EQUAL_UINT(0, actualReads);
}

void test_mulRlrMaintainsIndependentFullIdentitySourceState(void) {
  mulRlrAlgorithm.init(&config, &dev);
  uint8_t first[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  const uint8_t second[8] = {0x85,0,0,0,0,0,0xcf,0xbc};
  deliver(first, UINT32_MAX, MUL_TIMESTAMP_MASK, 10, false);
  deliver(second, 40, 100, MUL_TIMESTAMP_MASK, false);
  deliver(first, 0, 3, 12, false); // 32-bit sequence and 40-bit TX wrap.
  deliver(second, 43, 200, 2, false); // 40-bit RX wrap and two sequence gaps.
  deliver(first, 0, 999, 999, false);
  deliver(second, 42, 999, 999, false);
  TEST_ASSERT_EQUAL_UINT32(0, rlrSources[0].sequence);
  TEST_ASSERT_EQUAL_UINT32(0, rlrSources[0].gaps);
  TEST_ASSERT_EQUAL_UINT32(1, rlrSources[0].duplicates);
  TEST_ASSERT_EQUAL_UINT32(0, rlrSources[0].outOfOrder);
  TEST_ASSERT_EQUAL_UINT64(3, rlrSources[0].txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(12, rlrSources[0].rxTimestamp);
  TEST_ASSERT_EQUAL_UINT32(43, rlrSources[1].sequence);
  TEST_ASSERT_EQUAL_UINT32(2, rlrSources[1].gaps);
  TEST_ASSERT_EQUAL_UINT32(0, rlrSources[1].duplicates);
  TEST_ASSERT_EQUAL_UINT32(1, rlrSources[1].outOfOrder);
  TEST_ASSERT_EQUAL_UINT64(200, rlrSources[1].txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(2, rlrSources[1].rxTimestamp);
  first[7] ^= 1;
  deliver(first, 20, 1, 1, false);
  TEST_ASSERT_EQUAL_UINT32(3, rlrSources[0].received);
  TEST_ASSERT_EQUAL_UINT32(3, rlrSources[1].received);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_TRUE(receiving);
}

void test_mulRlrRebasesOnlyTheStaleSourceAfterReferenceRestart(void) {
  mulRlrAlgorithm.init(&config, &dev);
  const uint8_t first[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  const uint8_t second[8] = {0x85,0,0,0,0,0,0xcf,0xbc};
  deliver(first, 42, 100, 200, false);
  deliver(second, 42, 500, 600, false);
  testTick = 2500;
  deliver(second, 43, 501, 601, false);
  testTick = 3000;
  deliver(first, 42, 999, 999, false); // Duplicate does not reset a stale stream.
  TEST_ASSERT_EQUAL_UINT64(100, rlrSources[0].txTimestamp);
  deliver(first, 0, 1, 202, false);
  TEST_ASSERT_EQUAL_UINT32(0, rlrSources[0].sequence);
  TEST_ASSERT_EQUAL_UINT32(43, rlrSources[1].sequence);
  TEST_ASSERT_EQUAL_UINT64(501, rlrSources[1].txTimestamp);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
}

void test_mulMalformedAndUnknownPacketsCannotRefreshAcquiredReference(void) {
  initFollower();
  const uint8_t reference[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  deliver(reference, 10, 100, 200, true);
  testTick = 2000;
  rxFrame[24] = 99;
  TEST_ASSERT_EQUAL_UINT32(1000, mulAlrAlgorithm.onEvent(&dev, eventPacketReceived));
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.received);
  const uint8_t other[8] = {0x85,0,0,0,0,0,0xcf,0xbc};
  testTick = 2999;
  TEST_ASSERT_EQUAL_UINT32(1, deliver(other, 20, 7, 8, true));
  testTick = 3000;
  mulAlrAlgorithm.onEvent(&dev, eventReceiveFailed);
  TEST_ASSERT_FALSE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.received);
  TEST_ASSERT_EQUAL_UINT64(100, followerReference.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(200, followerReference.rxTimestamp);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_TRUE(receiving);
}
