/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_rx.h"
#include "mul_common.h"
#include "mul_clock.h"
#include "mul_twr.h"
#include "mul_relation.h"
#include "mul_role_state.h"
#include "mul_downlink.h"
#include "mul_localization.h"
#include "mul_power.h"
#include "mul_observation.h"
#include <math.h>

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
static uint8_t capturedTxFrame[MUL_RELATION_FRAME_LENGTH + 2];
static uint8_t rxFrame[MUL_RELATION_FRAME_LENGTH];
static bool receiving, configuring, delayedArmed;
static uint64_t hardwareNow, delayedRaw, actualTx;
static uint64_t setupDelay;
static uint16_t antennaDelay, txError;
static unsigned int actualReads, aborts;
static uint64_t programmedTimeError;
static uint64_t packetTimestampError;
static TickType_t testTick;
static float simulatedPower;
static int8_t simulatedCorrection;
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
  if (reg == LDE_IF) { TEST_ASSERT_EQUAL_UINT32(LDE_RXANTD_SUB, offset); return MUL_ANTENNA_RX_DTU; }
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
  TEST_ASSERT_EQUAL_UINT(alrRole==mulAlrFollower ? MUL_RELATION_FRAME_LENGTH : MUL_TEST_FRAME_LENGTH,length);
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
void dwWriteSystemConfigurationRegister(dwDevice_t *device) { (void)device; }
void dwWriteSystemEventMaskRegister(dwDevice_t *device) {
  (void)device;
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
  TEST_ASSERT_EQUAL_UINT(rxLength, length);
  memcpy(data, rxFrame, length);
  readCount++;
}
void dwGetRawReceiveTimestamp(dwDevice_t *device, dwTime_t *time) {
  (void)device;
  TEST_ASSERT_FALSE(receiving);
  time->full = incomingTimestamp;
  timestampCount++;
}

static bool decodeCaptured(mulTestPacket_t *packet) {
  mulRelation_t relation;
  return mulBroadcastDecode(capturedTxFrame,alrRole==mulAlrFollower ? MUL_RELATION_FRAME_LENGTH : MUL_TEST_FRAME_LENGTH,packet,&relation);
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
  simulatedPower=NAN; simulatedCorrection=0;
  incomingTimestamp = UINT64_C(0xfedcba9876);
  rxLength = MUL_TEST_FRAME_LENGTH;
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
    TEST_ASSERT_TRUE(decodeCaptured(&packet));
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
  TEST_ASSERT_EQUAL_UINT(1001, rxCount); // Reference now listens between unchanged broadcasts.
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
  TEST_ASSERT_TRUE(decodeCaptured(&packet));
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x4121), packet.txTimestamp);
  completeTx(-7);

  antennaDelay = 0;
  mulAlrAlgorithm.init(&config, &dev);
  TEST_ASSERT_EQUAL_UINT32(100, elapseAndTimeout(0));
  TEST_ASSERT_TRUE(decodeCaptured(&packet));
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
  TEST_ASSERT_TRUE(decodeCaptured(&decoded));
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
  TEST_ASSERT_TRUE(decodeCaptured(&decoded));
  TEST_ASSERT_EQUAL_UINT64(mulTimestampMask(delayedRaw + antennaDelay - packetTimestampError),
                         decoded.txTimestamp);
  completeTx(0);
}

void test_mulRlrRearmsAfterValidMalformedAndUnrelatedPackets(void) {
  config.address[0]=0x86; // Receiver is a distinct clock identity.
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
    if (length != MUL_TEST_FRAME_LENGTH) {
      rxLength = length;
      TEST_ASSERT_EQUAL_UINT32(MAX_TIMEOUT, mulRlrAlgorithm.onEvent(&dev, eventPacketReceived));
      TEST_ASSERT_TRUE(receiving);
    }
  }
  TEST_ASSERT_EQUAL_UINT(3, readCount); // 34-byte valid/malformed plus malformed 74-byte version.
  TEST_ASSERT_EQUAL_UINT(0, txCount);
}

void test_mulRlrRearmsOnTimeoutsAndReceiveFailuresWithoutTransmitting(void) {
  config.address[0]=0x86; // Receiver is a distinct clock identity.
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
  rxLength = MUL_TEST_FRAME_LENGTH;
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

static const uint8_t referenceAddress[8] = {0x84,0,0,0,0,0,0xcf,0xbc};

static uint32_t receiveReference(uint32_t sequence, uint64_t tx, uint64_t rx) {
  // A real callback takes time, but the slot must stay anchored to RX_TIME.
  hardwareNow = mulTimestampMask(rx + DTU_PER_MS);
  return deliver(referenceAddress, sequence, tx, rx, true);
}

static void finishFollowerSlot(void) {
  TEST_ASSERT_TRUE(txPending);
  TEST_ASSERT_FALSE(receiving);
  completeTx(0);
  TEST_ASSERT_FALSE(txPending);
  TEST_ASSERT_TRUE(receiving);
}

void test_mulFollowerAcquiresTracksSequenceAndPreservesBothClockDomains(void) {
  initFollower();
  uint32_t wait = receiveReference(10, MUL_TIMESTAMP_MASK, 2);
  TEST_ASSERT_TRUE(wait >= MUL_FOLLOWER_SLOT_MS+98 && wait <= MUL_FOLLOWER_SLOT_MS+100);
  TEST_ASSERT_TRUE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK, followerReference.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(2, followerReference.rxTimestamp);
  finishFollowerSlot();
  testTick = 1000;
  uint64_t rx = 1000 * DTU_PER_MS + 9;
  receiveReference(13, 5, rx);
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT32(2, followerReference.gaps);
  testTick = 1600;
  TEST_ASSERT_EQUAL_UINT32(2400, deliver(referenceAddress, 13, 99, 100, true));
  testTick = 1700;
  TEST_ASSERT_EQUAL_UINT32(2300, deliver(referenceAddress, 12, 99, 100, true));
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.duplicates);
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.outOfOrder);
  TEST_ASSERT_EQUAL_UINT32(13, followerReference.sequence);
  TEST_ASSERT_EQUAL_UINT64(5, followerReference.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(rx, followerReference.rxTimestamp);
  TEST_ASSERT_EQUAL_UINT(2, txCount);
}

void test_mulFollowerLosesReferenceDespiteTrafficAndReacquiresAfterRestart(void) {
  initFollower();
  receiveReference(42, 7, 10);
  finishFollowerSlot();
  testTick = 2999;
  TEST_ASSERT_EQUAL_UINT32(1, deliver(referenceAddress, 42, 7, 10, true));
  TEST_ASSERT_TRUE(referenceAcquired);
  testTick = 3000;
  deliver(referenceAddress, 42, 7, 10, true);
  TEST_ASSERT_FALSE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT(1, txCount); // Replaying a reference cannot create a slot.
  testTick = 4000;
  receiveReference(0, 1, 4000 * DTU_PER_MS);
  TEST_ASSERT_TRUE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT32(0, followerReference.sequence);
  TEST_ASSERT_EQUAL_UINT32(0, followerReference.gaps);
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT(2, txCount);
  TEST_ASSERT_EQUAL_UINT32(1, pendingSequence); // Follower sequence does not reset.
}

void test_mulFollowerLossDeadlineSurvivesRtosWrapAndSilence(void) {
  initFollower();
  TickType_t firstTick = UINT32_MAX - 1000;
  testTick = firstTick;
  receiveReference(0, MUL_TIMESTAMP_MASK - 1, MUL_TIMESTAMP_MASK);
  finishFollowerSlot();
  testTick = firstTick + 1000;
  receiveReference(1, 1, mulTimestampMask(MUL_TIMESTAMP_MASK + 1000 * DTU_PER_MS));
  finishFollowerSlot();
  TEST_ASSERT_TRUE(referenceAcquired);
  testTick += 2999;
  TEST_ASSERT_EQUAL_UINT32(1, mulAlrAlgorithm.onEvent(&dev, eventTimeout));
  testTick++;
  TEST_ASSERT_EQUAL_UINT32(3000, mulAlrAlgorithm.onEvent(&dev, eventTimeout));
  TEST_ASSERT_FALSE(referenceAcquired);
  testTick++;
  receiveReference(2, 9, 5000 * DTU_PER_MS);
  TEST_ASSERT_TRUE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT64(9, followerReference.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(5000 * DTU_PER_MS, followerReference.rxTimestamp);
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT(3, txCount);
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
    if (length != MUL_TEST_FRAME_LENGTH) {
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
  config.address[0]=0x86; // Receiver is a distinct clock identity.
  mulRlrAlgorithm.init(&config, &dev);
  uint8_t first[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  const uint8_t second[8] = {0x85,0,0,0,0,0,0xcf,0xbc};
  deliver(first, UINT32_MAX, MUL_TIMESTAMP_MASK, 10, false);
  deliver(second, 40, 100, MUL_TIMESTAMP_MASK, false);
  deliver(first, 0, 3, 12, false); // 32-bit sequence and 40-bit TX wrap.
  deliver(second, 43, 200, 2, false); // 40-bit RX wrap and two sequence gaps.
  deliver(first, 0, 999, 999, false);
  deliver(second, 42, 999, 999, false);
  TEST_ASSERT_EQUAL_UINT32(0, rlrObservations[0].source.sequence);
  TEST_ASSERT_EQUAL_UINT32(0, rlrObservations[0].source.gaps);
  TEST_ASSERT_EQUAL_UINT32(1, rlrObservations[0].source.duplicates);
  TEST_ASSERT_EQUAL_UINT32(0, rlrObservations[0].source.outOfOrder);
  TEST_ASSERT_EQUAL_UINT64(3, rlrObservations[0].source.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(12, rlrObservations[0].source.rxTimestamp);
  TEST_ASSERT_EQUAL_UINT32(43, rlrObservations[1].source.sequence);
  TEST_ASSERT_EQUAL_UINT32(2, rlrObservations[1].source.gaps);
  TEST_ASSERT_EQUAL_UINT32(0, rlrObservations[1].source.duplicates);
  TEST_ASSERT_EQUAL_UINT32(1, rlrObservations[1].source.outOfOrder);
  TEST_ASSERT_EQUAL_UINT64(200, rlrObservations[1].source.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(2, rlrObservations[1].source.rxTimestamp);
  first[7] ^= 1;
  deliver(first, 20, 1, 1, false);
  TEST_ASSERT_EQUAL_UINT32(3, rlrObservations[0].source.received);
  TEST_ASSERT_EQUAL_UINT32(3, rlrObservations[1].source.received);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_TRUE(receiving);
}

void test_mulRlrRebasesOnlyTheStaleSourceAfterReferenceRestart(void) {
  config.address[0]=0x86; // Receiver is a distinct clock identity.
  mulRlrAlgorithm.init(&config, &dev);
  const uint8_t first[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  const uint8_t second[8] = {0x85,0,0,0,0,0,0xcf,0xbc};
  deliver(first, 42, 100, 200, false);
  deliver(second, 42, 500, 600, false);
  testTick = 2500;
  deliver(second, 43, 501, 601, false);
  testTick = 3000;
  deliver(first, 42, 999, 999, false); // Duplicate does not reset a stale stream.
  TEST_ASSERT_EQUAL_UINT64(100, rlrObservations[0].source.txTimestamp);
  deliver(first, 0, 1, 202, false);
  TEST_ASSERT_EQUAL_UINT32(0, rlrObservations[0].source.sequence);
  TEST_ASSERT_EQUAL_UINT32(43, rlrObservations[1].source.sequence);
  TEST_ASSERT_EQUAL_UINT64(501, rlrObservations[1].source.txTimestamp);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
}

void test_mulMalformedAndUnknownPacketsCannotRefreshAcquiredReference(void) {
  initFollower();
  receiveReference(10, 100, 200);
  finishFollowerSlot();
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
  TEST_ASSERT_EQUAL_UINT(1, txCount);
  TEST_ASSERT_TRUE(receiving);
}

void test_mulFollowerSlotUsesLocalRxClockAndOwnTimestampAcrossWrap(void) {
  initFollower();
  antennaDelay = 0xffff;
  uint64_t rx = MUL_TIMESTAMP_MASK - 250 * DTU_PER_MS + 123;
  unsigned int rearmCount = rxCount;
  receiveReference(12345, UINT64_C(0x123456789a), rx);
  TEST_ASSERT_TRUE(txPending);
  TEST_ASSERT_TRUE(delayedArmed);
  TEST_ASSERT_FALSE(receiving);
  TEST_ASSERT_EQUAL_UINT(rearmCount, rxCount); // Direct RX -> TX; no intermediate RX race.
  TEST_ASSERT_EQUAL_UINT64(0, delayedRaw & 511);
  mulTestPacket_t packet;
  TEST_ASSERT_TRUE(decodeCaptured(&packet));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(config.address, packet.source, 8);
  TEST_ASSERT_EQUAL_UINT32(0, packet.sequence); // Not the reference's sequence.
  TEST_ASSERT_TRUE(packet.txTimestamp != UINT64_C(0x123456789a));
  TEST_ASSERT_EQUAL_UINT64(mulTimestampMask(delayedRaw + antennaDelay), packet.txTimestamp);
  int64_t slot = mulTimestampDifference(packet.txTimestamp, rx);
  TEST_ASSERT_TRUE(slot >= (int64_t)MUL_FOLLOWER_SLOT_DTU && slot < (int64_t)(MUL_FOLLOWER_SLOT_DTU+512));
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT64(packet.txTimestamp, actualTx);
  TEST_ASSERT_EQUAL_UINT(1, actualReads);
}

void test_mulFollowerNeverRetriesConsumedOrMissingReferenceSuperframes(void) {
  initFollower();
  receiveReference(42, 1, 0);
  finishFollowerSlot();
  deliver(referenceAddress, 42, 1, 1000, true);
  deliver(referenceAddress, 41, 1, 2000, true);
  TEST_ASSERT_EQUAL_UINT(1, txCount);
  TEST_ASSERT_FALSE(txPending);
  testTick = 1500;
  mulAlrAlgorithm.onEvent(&dev, eventTimeout);
  TEST_ASSERT_EQUAL_UINT(1, txCount); // Acquired is not permission to free-run.
  TEST_ASSERT_TRUE(receiving);
  testTick = 2000;
  receiveReference(44, 2, 2000 * DTU_PER_MS); // Reference 43 was missed: no catch-up TX.
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT(2, txCount);
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.gaps);
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.duplicates);
  TEST_ASSERT_EQUAL_UINT32(1, followerReference.outOfOrder);
  TEST_ASSERT_EQUAL_UINT32(1, pendingSequence);
}

void test_mulFollowerWatchdogKeepsEarlyWakeArmedThenRecoversWithoutRetry(void) {
  initFollower();
  uint32_t wait = receiveReference(50, 1, 0);
  unsigned int rearms = rxCount;
  uint32_t early = elapseAndTimeout(100);
  TEST_ASSERT_TRUE(early < wait);
  TEST_ASSERT_TRUE(txPending && delayedArmed);
  TEST_ASSERT_EQUAL_UINT(rearms, rxCount);
  elapseAndTimeout(early); // Absolute raw target + 100 ms has now elapsed.
  TEST_ASSERT_FALSE(txPending);
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_EQUAL_UINT(0, actualReads);
  TEST_ASSERT_EQUAL_UINT(1, txCount);
  deliver(referenceAddress, 50, 1, hardwareNow, true);
  testTick = 2000;
  mulAlrAlgorithm.onEvent(&dev, eventTimeout);
  TEST_ASSERT_EQUAL_UINT(1, txCount);
  receiveReference(51, 1, 2000 * DTU_PER_MS);
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT(2, txCount);
  TEST_ASSERT_EQUAL_UINT(1, actualReads);
}

void test_mulFollowerSkipsLateReceptionAndLateSpiSetupWithoutMovingSlot(void) {
  initFollower();
  hardwareNow = (MUL_FOLLOWER_SLOT_MS-4) * DTU_PER_MS; // Only 4 ms remain until the reference's slot.
  deliver(referenceAddress, 0, 9, 0, true);
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_FALSE(txPending);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  deliver(referenceAddress, 0, 9, hardwareNow, true); // Duplicate cannot reschedule.
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  setupDelay = MUL_FOLLOWER_SLOT_DTU;
  receiveReference(1, 9, 1000 * DTU_PER_MS);
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_FALSE(txPending);
  TEST_ASSERT_EQUAL_UINT(0, txCount); // Post-setup guard rejected expired target.
  setupDelay = 0;
  receiveReference(2, 9, 2000 * DTU_PER_MS);
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT(1, txCount);
  TEST_ASSERT_EQUAL_UINT(1, actualReads);
}

void test_mulFollowerRecoversFromBothDelayedStartErrorsWithoutRetry(void) {
  initFollower();
  const uint16_t errors[] = {0x0008, 0x0400};
  for (unsigned int i = 0; i < 2; i++) {
    txError = errors[i];
    receiveReference(2*i, 99, (2*i) * 1000 * DTU_PER_MS);
    TEST_ASSERT_FALSE(txPending);
    TEST_ASSERT_TRUE(receiving);
    TEST_ASSERT_EQUAL_UINT(2*i+1, txCount);
    mulAlrAlgorithm.onEvent(&dev, eventPacketSent); // Stale completion is not success.
    TEST_ASSERT_EQUAL_UINT(i, actualReads);
    deliver(referenceAddress, 2*i, 99, hardwareNow, true);
    TEST_ASSERT_EQUAL_UINT(2*i+1, txCount);
    txError = 0;
    receiveReference(2*i+1, 99, (2*i+1) * 1000 * DTU_PER_MS);
    finishFollowerSlot();
    TEST_ASSERT_EQUAL_UINT(i+1, actualReads);
  }
}

void test_mulFollowerRejectsDxReadbackDisagreementAndFutureRxTimes(void) {
  initFollower();
  programmedTimeError = 512;
  receiveReference(0, 9, 0);
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_TRUE(receiving);
  programmedTimeError = 0;
  hardwareNow = 1000 * DTU_PER_MS;
  deliver(referenceAddress, 1, 9, 2000 * DTU_PER_MS, true); // Impossible future RX.
  TEST_ASSERT_EQUAL_UINT(0, txCount);
  TEST_ASSERT_TRUE(receiving);
  receiveReference(2, 9, 3000 * DTU_PER_MS);
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT(1, txCount);
}

void test_mulFollowerRadioEventsCancelPendingSlotAndResumeRx(void) {
  initFollower();
  const uwbEvent_t events[] = {eventPacketReceived, eventReceiveTimeout, eventReceiveFailed};
  for (unsigned int i = 0; i < 3; i++) {
    receiveReference(2*i, 8, (2*i) * 1000 * DTU_PER_MS);
    unsigned int reads = readCount;
    rxFrame[24] = 99; // Malformed/spurious RX event cannot overwrite pending context.
    mulAlrAlgorithm.onEvent(&dev, events[i]);
    TEST_ASSERT_FALSE(txPending);
    TEST_ASSERT_TRUE(receiving);
    TEST_ASSERT_EQUAL_UINT(reads, readCount);
    TEST_ASSERT_EQUAL_UINT(i, actualReads);
    deliver(referenceAddress, 2*i, 8, hardwareNow, true);
    TEST_ASSERT_EQUAL_UINT(2*i+1, txCount);
    receiveReference(2*i+1, 8, (2*i+1) * 1000 * DTU_PER_MS);
    finishFollowerSlot();
    TEST_ASSERT_EQUAL_UINT(i+1, actualReads);
  }
}

void test_mulFollowerLossAbortsPendingTxAndRequiresFreshReacquisition(void) {
  initFollower();
  receiveReference(88, 1, 0);
  testTick = 3000; // Force liveness expiry while a hardware TX is still pending.
  mulAlrAlgorithm.onEvent(&dev, eventTimeout);
  TEST_ASSERT_FALSE(referenceAcquired);
  TEST_ASSERT_FALSE(txPending);
  TEST_ASSERT_FALSE(delayedArmed);
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_EQUAL_UINT(0, actualReads);
  mulAlrAlgorithm.onEvent(&dev, eventPacketSent);
  testTick = 9000;
  mulAlrAlgorithm.onEvent(&dev, eventTimeout);
  deliver(referenceAddress, 88, 1, hardwareNow, true);
  TEST_ASSERT_EQUAL_UINT(1, txCount);
  TEST_ASSERT_FALSE(referenceAcquired);
  receiveReference(0, 1, 9000 * DTU_PER_MS);
  finishFollowerSlot();
  TEST_ASSERT_TRUE(referenceAcquired);
  TEST_ASSERT_EQUAL_UINT(2, txCount);
  TEST_ASSERT_EQUAL_UINT(1, actualReads);
}

void test_mulFollowerAndReferenceSequenceCountersWrapIndependently(void) {
  initFollower();
  nextSequence = UINT32_MAX;
  receiveReference(UINT32_MAX, MUL_TIMESTAMP_MASK, MUL_TIMESTAMP_MASK - DTU_PER_MS);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, pendingSequence);
  finishFollowerSlot();
  testTick = 1000;
  receiveReference(0, 0, 1000 * DTU_PER_MS);
  TEST_ASSERT_EQUAL_UINT32(0, pendingSequence);
  finishFollowerSlot();
  deliver(referenceAddress, 0, 0, hardwareNow, true);
  deliver(referenceAddress, UINT32_MAX, 0, hardwareNow, true);
  TEST_ASSERT_EQUAL_UINT(2, txCount);
  TEST_ASSERT_EQUAL_UINT32(0, followerReference.gaps);
}

void test_mulFollowerRepeatsHardwareSlotsAcrossManyClockWraps(void) {
  initFollower();
  for (uint32_t frame = 0; frame < 1000; frame++) {
    testTick = frame * 1000;
    uint64_t rx = mulTimestampMask(MUL_TIMESTAMP_MASK - 100*DTU_PER_MS + frame*PERIOD_DTU + 123);
    receiveReference(100000 + frame, mulTimestampMask(frame * PERIOD_DTU), rx);
    mulTestPacket_t packet;
    TEST_ASSERT_TRUE(decodeCaptured(&packet));
    TEST_ASSERT_EQUAL_UINT32(frame, packet.sequence);
    TEST_ASSERT_EQUAL_UINT8(0x85, packet.source[0]);
    int64_t offset = mulTimestampDifference(packet.txTimestamp, rx);
    TEST_ASSERT_TRUE(offset >= (int64_t)MUL_FOLLOWER_SLOT_DTU && offset < (int64_t)(MUL_FOLLOWER_SLOT_DTU+512));
    finishFollowerSlot();
    TEST_ASSERT_EQUAL_UINT64(packet.txTimestamp, actualTx);
  }
  TEST_ASSERT_EQUAL_UINT(1000, txCount);
  TEST_ASSERT_EQUAL_UINT(1000, actualReads);
}

void test_mulRlrReceivesInterleavedReferenceAndActualFollowerPackets(void) {
  initFollower();
  receiveReference(100, UINT64_C(0x123456789a), MUL_TIMESTAMP_MASK - 200*DTU_PER_MS);
  mulTestPacket_t follower;
  TEST_ASSERT_TRUE(decodeCaptured(&follower));
  finishFollowerSlot();
  config.address[0]=0x86; // Receiver is a distinct clock identity.
  mulRlrAlgorithm.init(&config, &dev);
  uint64_t refRx = MUL_TIMESTAMP_MASK - 100*DTU_PER_MS;
  uint64_t followerRx = mulTimestampMask(refRx + MUL_FOLLOWER_SLOT_DTU);
  deliver(referenceAddress, 100, UINT64_C(0x123456789a), refRx, false);
  deliver(follower.source, follower.sequence, follower.txTimestamp, followerRx, false);
  TEST_ASSERT_EQUAL_UINT32(100, rlrObservations[0].source.sequence);
  TEST_ASSERT_EQUAL_UINT32(0, rlrObservations[1].source.sequence);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x123456789a), rlrObservations[0].source.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(actualTx, rlrObservations[1].source.txTimestamp);
  TEST_ASSERT_EQUAL_INT64((int64_t)MUL_FOLLOWER_SLOT_DTU,
    mulTimestampDifference(rlrObservations[1].source.rxTimestamp, rlrObservations[0].source.rxTimestamp));
  TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_EQUAL_UINT(1, txCount); // Only follower code transmitted, RLR never did.
}

void test_mulFollowerCanTransmitAtCounterZeroWithZeroAntennaDelay(void) {
  initFollower();
  antennaDelay = 0;
  uint64_t rx = MUL_TIMESTAMP_MASK - MUL_FOLLOWER_SLOT_DTU + 1;
  receiveReference(0, UINT64_C(0x9876543210), rx);
  TEST_ASSERT_TRUE(txPending);
  TEST_ASSERT_EQUAL_UINT64(0, delayedRaw);
  mulTestPacket_t packet;
  TEST_ASSERT_TRUE(decodeCaptured(&packet));
  TEST_ASSERT_EQUAL_UINT64(0, packet.txTimestamp);
  finishFollowerSlot();
  TEST_ASSERT_EQUAL_UINT64(0, actualTx);
  TEST_ASSERT_TRUE(receiving);
}

void test_mulFollowerClockConvergesWithoutChangingTdmaAndResetsOnLoss(void) {
  initFollower();
  uint64_t remoteStart = UINT64_C(0x123456789a);
  uint64_t localStart = MUL_TIMESTAMP_MASK - 2*PERIOD_DTU;
  for (uint32_t frame = 0; frame < 7; frame++) {
    testTick = frame * 1000;
    uint64_t local = mulTimestampMask(localStart + frame * (PERIOD_DTU + PERIOD_DTU/200000));
    receiveReference(42 + frame, mulTimestampMask(remoteStart + frame*PERIOD_DTU), local);
    TEST_ASSERT_TRUE(txPending);
    mulTestPacket_t outgoing;
    TEST_ASSERT_TRUE(decodeCaptured(&outgoing));
    int64_t slot = mulTimestampDifference(outgoing.txTimestamp, local);
    TEST_ASSERT_TRUE(slot >= (int64_t)MUL_FOLLOWER_SLOT_DTU && slot < (int64_t)(MUL_FOLLOWER_SLOT_DTU+512));
    TEST_ASSERT_EQUAL_UINT32(frame, outgoing.sequence);
    finishFollowerSlot();
    TEST_ASSERT_EQUAL_UINT64(outgoing.txTimestamp, actualTx);
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid, followerClock.status);
  TEST_ASSERT_EQUAL_INT32(5000, followerClock.skewPpb);
  TEST_ASSERT_EQUAL_UINT32(6, followerClock.samples);
  mulClockMapping_t mapping;
  TEST_ASSERT_TRUE(mulClockMapRefToFollower(&followerClock, followerClock.referenceTx, testTick, &mapping));
  TEST_ASSERT_EQUAL_UINT64(followerClock.localRx, mapping.timestamp);
  TEST_ASSERT_FALSE(mapping.syncCalibrated);
  mulClock_t before = followerClock;
  deliver(referenceAddress, 48, 0, hardwareNow, true);
  deliver(referenceAddress, 47, 0, hardwareNow, true);
  rxFrame[24] = 99;
  mulAlrAlgorithm.onEvent(&dev, eventPacketReceived);
  TEST_ASSERT_EQUAL_MEMORY(&before, &followerClock, sizeof(before));
  TEST_ASSERT_EQUAL_UINT(7, txCount);
  testTick = 9000;
  mulAlrAlgorithm.onEvent(&dev, eventTimeout);
  TEST_ASSERT_EQUAL_INT(mulClockSearching, followerClock.status);
  TEST_ASSERT_EQUAL_UINT32(0, followerClock.samples);
  TEST_ASSERT_EQUAL_INT(mulClockMapInvalid, mulClockMapStatus(&followerClock, testTick));
  for (uint32_t frame = 0; frame < 6; frame++) {
    testTick = 10000 + frame*1000;
    uint64_t local = mulTimestampMask(localStart + (10+frame)*PERIOD_DTU - frame*(PERIOD_DTU/100000));
    receiveReference(frame, frame*PERIOD_DTU, local); // Reference restarted at seq/TX zero.
    TEST_ASSERT_EQUAL_INT(frame < 5 ? mulClockMapWarmup : mulClockMapValid,
      mulClockMapStatus(&followerClock, testTick));
    finishFollowerSlot();
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid, followerClock.status);
  TEST_ASSERT_EQUAL_INT32(-10000, followerClock.skewPpb);
  TEST_ASSERT_EQUAL_UINT32(5, followerClock.samples);
  TEST_ASSERT_EQUAL_UINT(13, txCount);
}

void test_mulFollowerClockDiscontinuityDoesNotChangeRadioPolicy(void) {
  initFollower();
  receiveReference(100, 100*PERIOD_DTU, 0);
  finishFollowerSlot();
  testTick = 1000;
  receiveReference(101, 0, PERIOD_DTU); // Forward sequence but TX clock restarted.
  TEST_ASSERT_EQUAL_INT(mulClockWarmup, followerClock.status);
  TEST_ASSERT_EQUAL_UINT32(0, followerClock.samples);
  TEST_ASSERT_TRUE(txPending); // Estimator rejection cannot suppress a valid TDMA RX.
  TEST_ASSERT_EQUAL_INT(mulClockMapWarmup, mulClockMapStatus(&followerClock, testTick));
  finishFollowerSlot();
  for (uint32_t frame = 1; frame <= 5; frame++) {
    testTick = (1+frame)*1000;
    receiveReference(101+frame, frame*PERIOD_DTU, (1+frame)*PERIOD_DTU);
    finishFollowerSlot();
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid, followerClock.status);
  TEST_ASSERT_EQUAL_INT32(0, followerClock.skewPpb);
}

float dwGetReceivePower(dwDevice_t *device) { (void)device; TEST_ASSERT_FALSE(receiving); return simulatedPower; }
void dwCorrectTimestamp(dwDevice_t *device,dwTime_t *t) { (void)device; TEST_ASSERT_TRUE(isfinite(simulatedPower)); t->full+=simulatedCorrection; }

void test_mulRlrAssociatesPassivelyCapturesPowerBeforeRearmAndExpiresMissingFollower(void) {
  const uint8_t followerAddress[8]={0x85,0,0,0,0,0,0xcf,0xbc};
  config.address[0]=0x86; dev.channel=CHANNEL_2; dev.pulseFrequency=TX_PULSE_FREQ_64MHZ;
  mulRlrAlgorithm.init(&config,&dev);
  simulatedPower=-80; simulatedCorrection=-5;
  testTick=100; deliver(referenceAddress,20,100,0,false);
  testTick=100+MUL_FOLLOWER_SLOT_MS; deliver(followerAddress,900,200,MUL_FOLLOWER_SLOT_DTU,false);
  testTick=1100;
  TEST_ASSERT_EQUAL_UINT32(MUL_PAIR_MAX_AGE_MS+1,deliver(referenceAddress,21,100+PERIOD_DTU,PERIOD_DTU,false));
  TEST_ASSERT_TRUE(rlrAssociation.pending);
  TEST_ASSERT_EQUAL_INT(mulPowerApplied,rlrObservations[0].power.status);
  TEST_ASSERT_EQUAL_UINT64(PERIOD_DTU-5,rlrObservations[0].correctedRx);
  testTick=1100+MUL_FOLLOWER_SLOT_MS; deliver(followerAddress,901,200+PERIOD_DTU,(PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU),false);
  TEST_ASSERT_FALSE(rlrAssociation.pending); TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_EQUAL_UINT(0,txCount);
  testTick=2100; deliver(referenceAddress,22,100+2*PERIOD_DTU,2*PERIOD_DTU,false);
  TEST_ASSERT_TRUE(rlrAssociation.pending);
  testTick=2801; mulRlrAlgorithm.onEvent(&dev,eventTimeout);
  TEST_ASSERT_FALSE(rlrAssociation.pending); TEST_ASSERT_TRUE(receiving);
  TEST_ASSERT_EQUAL_UINT(0,txCount);
}

static void deliverRelation(uint32_t sequence,uint64_t tx,uint64_t rx,const mulRelation_t *relation) {
  mulTestPacket_t p={.source={0x85,0,0,0,0,0,0xcf,0xbc},.sequence=sequence,.txTimestamp=tx};
  mulRelationEncode(rxFrame,&p,relation); rxLength=MUL_RELATION_FRAME_LENGTH;
  incomingTimestamp=rx; hardwareNow=mulTimestampMask(rx+DTU_PER_MS); receiving=false;
  mulRlrAlgorithm.onEvent(&dev,eventPacketReceived);
}
void test_mulRlrRetainsCurrentRelationAndEpochTransitionsCannotReuseOldPair(void) {
  config.address[0]=0x86; mulRlrAlgorithm.init(&config,&dev);
  mulRelation_t r={.epoch=10,.referenceSequence=20};
  testTick=100; deliver(referenceAddress,20,0,0,false);
  testTick=100+MUL_FOLLOWER_SLOT_MS; deliverRelation(900,MUL_FOLLOWER_SLOT_DTU,MUL_FOLLOWER_SLOT_DTU,&r);
  TEST_ASSERT_TRUE(rlrObservations[1].remoteEpochKnown);
  TEST_ASSERT_EQUAL_UINT64(10,mulRoleScratch.rlr.relation.epoch);
  testTick=1100; deliver(referenceAddress,21,PERIOD_DTU,PERIOD_DTU,false);
  r=(mulRelation_t){.epoch=10,.referenceSequence=21,.twrSequence=20,.available=true,
    .referenceOrigin=PERIOD_DTU,.followerOrigin=PERIOD_DTU,.originAgeMs=MUL_FOLLOWER_SLOT_MS,.delayAgeMs=350+MUL_FOLLOWER_SLOT_MS,.remainingMs=650-MUL_FOLLOWER_SLOT_MS};
  testTick=1100+MUL_FOLLOWER_SLOT_MS; deliverRelation(901,(PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU),(PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU),&r);
  TEST_ASSERT_TRUE(mulRoleScratch.rlr.relation.available);
  TEST_ASSERT_EQUAL_UINT32(1,rlrObservations[1].generation); TEST_ASSERT_FALSE(rlrAssociation.pending);
  testTick=2100; deliver(referenceAddress,22,2*PERIOD_DTU,2*PERIOD_DTU,false);
  r=(mulRelation_t){.epoch=11,.referenceSequence=22}; // New acquisition, no fresh TWR.
  testTick=2100+MUL_FOLLOWER_SLOT_MS; deliverRelation(902,(2*PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU),(2*PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU),&r);
  TEST_ASSERT_EQUAL_UINT32(2,rlrObservations[1].generation);
  TEST_ASSERT_FALSE(mulRoleScratch.rlr.relation.available); TEST_ASSERT_FALSE(rlrAssociation.pending);
  TEST_ASSERT_EQUAL_UINT(0,txCount); TEST_ASSERT_TRUE(receiving);
}

void test_mulRlrStationaryRateUsesReferenceCadenceAndFreshCurrentMetadata(void) {
  config.address[0]=0x86; mulRlrAlgorithm.init(&config,&dev);
  for (unsigned int i=0;i<7;i++) {
    testTick=100+i*1000;
    deliver(referenceAddress,20+i,i*PERIOD_DTU,i*PERIOD_DTU,false);
    mulRelation_t r={.epoch=10,.referenceSequence=20+i};
    if (i==6) r=(mulRelation_t){.epoch=10,.referenceSequence=26,.twrSequence=25,.available=true,
      .referenceOrigin=i*PERIOD_DTU,.followerOrigin=i*PERIOD_DTU,.originAgeMs=MUL_FOLLOWER_SLOT_MS,.delayAgeMs=350+MUL_FOLLOWER_SLOT_MS,.remainingMs=650-MUL_FOLLOWER_SLOT_MS};
    testTick=100+MUL_FOLLOWER_SLOT_MS+i*1000;
    deliverRelation(900+i,i*PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU,i*PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU,&r);
  }
  TEST_ASSERT_EQUAL_INT(mulClockValid,mulRoleScratch.rlr.rate.status);
  TEST_ASSERT_EQUAL_INT32(0,mulRoleScratch.rlr.rate.skewPpb);
  TEST_ASSERT_TRUE(mulRoleScratch.rlr.relation.available);
  TEST_ASSERT_EQUAL_UINT(0,txCount); TEST_ASSERT_TRUE(receiving);
  // Same wire epoch transition used by acquisition changes invalidates rate.
  testTick=7100; deliver(referenceAddress,27,7*PERIOD_DTU,7*PERIOD_DTU,false);
  mulRelation_t changed={.epoch=11,.referenceSequence=27};
  testTick=7100+MUL_FOLLOWER_SLOT_MS; deliverRelation(907,(7*PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU),(7*PERIOD_DTU+MUL_FOLLOWER_SLOT_DTU),&changed);
  TEST_ASSERT_TRUE(mulRoleScratch.rlr.rate.status!=mulClockValid);
}

void test_mulExplicitRolesFollowConfigurationRatherThanLegacyAddress(void) {
 config.mulConfigured=true;TEST_ASSERT_TRUE(mulNetworkInit(&config.mulNetwork,0x85,0x84));
 config.mulRole=mulAlrReference;config.address[0]=0x85;mulAlrAlgorithm.init(&config,&dev);
 TEST_ASSERT_EQUAL_INT(mulAlrReference,alrRole);elapseAndTimeout(0);
 mulTestPacket_t packet;mulRelation_t relation;TEST_ASSERT_TRUE(mulDecodeTestPacket(capturedTxFrame,34,&packet));TEST_ASSERT_EQUAL(0x85,packet.source[0]);
 completeTx(0);
 config.mulRole=mulAlrFollower;config.address[0]=0x84;mulAlrAlgorithm.init(&config,&dev);
 unsigned prior=txCount;
 for(unsigned i=0;i<7;i++) {
  testTick=10000+i*1000;hardwareNow=mulTimestampMask(i*PERIOD_DTU+DTU_PER_MS);
  deliver(config.mulNetwork.addresses[0],100+i,i*PERIOD_DTU,i*PERIOD_DTU,true);
  TEST_ASSERT_TRUE(txPending);TEST_ASSERT_TRUE(mulBroadcastDecodeNetwork(&config.mulNetwork,capturedTxFrame,74,&packet,&relation));
  TEST_ASSERT_EQUAL(0x84,packet.source[0]);finishFollowerSlot();
 }
 TEST_ASSERT_EQUAL(prior+7,txCount);TEST_ASSERT_EQUAL_INT(mulClockValid,followerClock.status);
}
void test_mulInvalidExplicitRosterAndReceiverSelfIdentityStayIdle(void) {
 config.mulConfigured=true;config.mulRole=mulAlrReference;memset(&config.mulNetwork,0,sizeof(config.mulNetwork));
 mulAlrAlgorithm.init(&config,&dev);mulAlrAlgorithm.onEvent(&dev,eventTimeout);TEST_ASSERT_EQUAL(0,txCount);
 TEST_ASSERT_TRUE(mulNetworkInit(&config.mulNetwork,0x84,0x85));config.address[0]=0x84;
 mulRlrAlgorithm.init(&config,&dev);TEST_ASSERT_FALSE(rlrEnabled);TEST_ASSERT_FALSE(receiving);
 mulRlrAlgorithm.onEvent(&dev,eventPacketReceived);TEST_ASSERT_EQUAL(0,txCount);
}
