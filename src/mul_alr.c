/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_alr.h"
#include "mul_common.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// DW1000 time units: 499.2 MHz * 128. These are not STM32 milliseconds.
#define MUL_DTU_PER_MS UINT64_C(63897600)
#define MUL_TX_PERIOD_DTU (1000 * MUL_DTU_PER_MS)
#define MUL_TX_LEAD_DTU (20 * MUL_DTU_PER_MS)
#define MUL_TX_MIN_LEAD_DTU (5 * MUL_DTU_PER_MS)
#define MUL_TX_COMPLETION_MS 100
#define MUL_TX_ALIGNMENT UINT64_C(512)

// SYS_STATUS bits 27 (HPDWARN) and 34 (TXPUTE), read at byte offset 3.
// Match Decawave dwt_starttx()'s delayed-start check. libdw1000 returns void.
#define MUL_TX_ERROR_STATUS UINT16_C(0x0408)

static uint8_t source[MUL_ADDRESS_LENGTH];
// dwSetData() reads length + 2 bytes when hardware FCS is enabled.
static uint8_t txFrame[MUL_TEST_FRAME_LENGTH + 2];
static uint32_t nextSequence;
static uint32_t pendingSequence;
static uint64_t nextRawTx;
static uint64_t pendingTimestamp;
static bool txPending;

static void logCompletedTx(uint64_t actual)
{
  // Compare against the bytes passed to dwSetData(), not just the scheduling
  // calculation. Retain both values in the log to expose any encoding mismatch.
  uint64_t packetTimestamp = mulDecodeTimestamp(&txFrame[MUL_TX_TIMESTAMP_OFFSET]);
  int64_t difference = mulTimestampDifference(actual, packetTimestamp);
  uint64_t magnitude = (uint64_t)(difference < 0 ? -difference : difference);
  // newlib-nano here has no 64-bit printf formats. Preserve the full signed
  // 40-bit difference as decimal rather than truncating it to a 32-bit long.
  char delta[14];
  char *end = delta + sizeof(delta) - 1;
  *end = '\0';
  do {
    *--end = '0' + magnitude % 10;
    magnitude /= 10;
  } while (magnitude != 0);
  if (difference < 0) {
    *--end = '-';
  }
  printf("MUL ALR: TX seq=%" PRIu32 " src=0x%02X scheduled=0x%02X%08" PRIX32
         " packet=0x%02X%08" PRIX32
         " actual=0x%02X%08" PRIX32 " delta=%s\r\n",
         pendingSequence, (unsigned int)source[0],
         (unsigned int)(pendingTimestamp >> 32), (uint32_t)pendingTimestamp,
         (unsigned int)(packetTimestamp >> 32), (uint32_t)packetTimestamp,
         (unsigned int)(actual >> 32), (uint32_t)actual, end);
}

static uint64_t readHardwareTime(dwDevice_t *dev)
{
  dwTime_t now = {.full = 0};
  dwGetSystemTimestamp(dev, &now);
  return mulTimestampMask(now.full);
}

static void clearTxErrors(dwDevice_t *dev)
{
  // Write-one-to-clear only these two flags, without clearing other IRQ events.
  const uint8_t errors[] = {0x08, 0x04};
  dwSpiWrite(dev, SYS_STATUS, 3, errors, sizeof(errors));
}

static uint32_t waitForNextTx(dwDevice_t *dev)
{
  int64_t remaining = mulTimestampDifference(nextRawTx, readHardwareTime(dev));
  remaining -= (int64_t)MUL_TX_LEAD_DTU;
  if (remaining <= 0) {
    return 1; // Never spin at the UWB task's higher priority.
  }
  // Round up when converting hardware time to the event interface's milliseconds.
  return (uint32_t)(((uint64_t)remaining + MUL_DTU_PER_MS - 1) / MUL_DTU_PER_MS);
}

static void mulAlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  dwIdle(dev);
  dwReceivePermanently(dev, false);
  memcpy(source, config->address, sizeof(source));
  nextSequence = 0;
  txPending = false;
  nextRawTx = mulTimestampMask((readHardwareTime(dev) + MUL_TX_LEAD_DTU +
                               MUL_TX_ALIGNMENT - 1) & ~(MUL_TX_ALIGNMENT - 1));
  printf("MUL ALR: Mode initialized\r\n");
}

static uint32_t scheduleTx(dwDevice_t *dev)
{
  uint64_t now = readHardwareTime(dev);
  int64_t remaining = mulTimestampDifference(nextRawTx, now);
  if (remaining < (int64_t)MUL_TX_MIN_LEAD_DTU) {
    // Missed a deadline: skip whole periods to retain phase, without a catch-up loop.
    uint64_t periods = ((uint64_t)((int64_t)MUL_TX_MIN_LEAD_DTU - remaining) /
                        MUL_TX_PERIOD_DTU) + 1;
    nextRawTx = mulTimestampMask(nextRawTx + periods * MUL_TX_PERIOD_DTU);
    printf("MUL ALR: Skipped late TX\r\n");
    return waitForNextTx(dev);
  }
  if (remaining > (int64_t)(MUL_TX_PERIOD_DTU + MUL_TX_LEAD_DTU)) {
    // A long task stall makes modular phase ambiguous. Rebase safely, never burst.
    nextRawTx = mulTimestampMask((now + MUL_TX_LEAD_DTU + MUL_TX_ALIGNMENT - 1) &
                                ~(MUL_TX_ALIGNMENT - 1));
    return waitForNextTx(dev);
  }

  dwTime_t rawTx = {.full = nextRawTx};
  nextRawTx = mulTimestampMask(nextRawTx + MUL_TX_PERIOD_DTU);
  pendingSequence = nextSequence++;
  dwNewTransmit(dev);
  dwSetDefaults(dev);
  clearTxErrors(dev);
  dwWaitForResponse(dev, false);
  dwSetTxRxTime(dev, rawTx); // Raw RMARKER time: low 9 bits already zero.

  // Verify what the radio was actually programmed to use before encoding it.
  // DX_TIME denotes the raw RMARKER, not the start of the preamble. The driver
  // already clears its low 9 bits; no preamble or software latency is added.
  dwTime_t programmed = {.full = 0};
  dwSpiRead(dev, DX_TIME, NO_SUB, programmed.raw, LEN_DX_TIME);
  if (programmed.full != rawTx.full) {
    dwIdle(dev);
    printf("MUL ALR: DX_TIME mismatch seq=%" PRIu32 "\r\n", pendingSequence);
    return waitForNextTx(dev);
  }
  // Read the live TX_ANTD for this packet, rather than a startup snapshot.
  uint16_t txAntennaDelay = dwSpiRead16(dev, TX_ANTD, NO_SUB);
  pendingTimestamp = mulTimestampMask(programmed.full + txAntennaDelay);
  mulEncodeTestPacket(txFrame, source, pendingSequence, pendingTimestamp);
  dwSetData(dev, txFrame, MUL_TEST_FRAME_LENGTH);

  // Check again after SPI setup, before arming. Leave room for preamble/power-up.
  if (mulTimestampDifference(rawTx.full, readHardwareTime(dev)) <
      (int64_t)MUL_TX_MIN_LEAD_DTU) {
    dwIdle(dev);
    printf("MUL ALR: Skipped late TX seq=%" PRIu32 "\r\n", pendingSequence);
    return waitForNextTx(dev);
  }
  txPending = true;
  dwStartTransmit(dev);
  if (dwSpiRead16(dev, SYS_STATUS, 3) & MUL_TX_ERROR_STATUS) {
    dwIdle(dev);
    clearTxErrors(dev);
    txPending = false;
    printf("MUL ALR: Delayed TX failed seq=%" PRIu32 "\r\n", pendingSequence);
    return waitForNextTx(dev);
  }
  // Completion must arrive within this bounded window, even if start failed silently.
  return MUL_TX_COMPLETION_MS;
}

static uint32_t mulAlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  switch (event) {
    case eventTimeout:
      if (!txPending) {
        return scheduleTx(dev);
      }
      dwIdle(dev);
      clearTxErrors(dev);
      txPending = false;
      printf("MUL ALR: TX timeout seq=%" PRIu32 "\r\n", pendingSequence);
      break;
    case eventPacketSent:
      if (txPending) {
        dwTime_t actual = {.full = 0};
        dwGetTransmitTimestamp(dev, &actual);
        actual.full = mulTimestampMask(actual.full);
        txPending = false;
        logCompletedTx(actual.full);
      }
      break;
    case eventReceiveTimeout:
    case eventReceiveFailed:
      dwIdle(dev);
      clearTxErrors(dev);
      txPending = false;
      printf("MUL ALR: Unexpected RX error\r\n");
      break;
    case eventPacketReceived:
      if (txPending) {
        return MUL_TX_COMPLETION_MS;
      }
      break;
  }
  return waitForNextTx(dev);
}

uwbAlgorithm_t mulAlrAlgorithm = {
  .init = mulAlrInit,
  .onEvent = mulAlrOnEvent,
};
