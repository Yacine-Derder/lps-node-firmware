/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_rlr.h"
#include "mul_common.h"

#include <inttypes.h>
#include <stdio.h>

static void mulRlrStartReceive(dwDevice_t *dev)
{
  dwNewReceive(dev);
  dwSetDefaults(dev);
  dwStartReceive(dev);
}

static void mulRlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  (void)config;
  // Keep the PHY selected by uwbInit(), with explicit callback-driven RX re-arm.
  dwNewConfiguration(dev);
  dwReceivePermanently(dev, false);
  dwSetFrameFilter(dev, false);
  dwSetReceiverAutoReenable(dev, false);
  dwSetReceiveWaitTimeout(dev, 0);
  dwInterruptOnReceiveFailed(dev, true);
  dwCommitConfiguration(dev);
  printf("MUL RLR: Mode initialized\r\n");
  mulRlrStartReceive(dev);
}

static uint32_t mulRlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  uint8_t frame[MUL_TEST_FRAME_LENGTH];
  mulTestPacket_t packet;
  dwTime_t arrival = {.full = 0};
  bool valid = false;

  if (event == eventPacketReceived) {
    // Reject unrelated, truncated, and oversized frames before copying data.
    unsigned int length = dwGetDataLength(dev);
    if (length == sizeof(frame)) {
      dwGetData(dev, frame, length);
      valid = mulDecodeTestPacket(frame, length, &packet);
      if (valid) {
        // Raw RX_TIME hardware value, without range-bias correction.
        dwGetRawReceiveTimestamp(dev, &arrival);
      }
    }
  }

  // Re-arm on every event, including startup's immediate timeout and RX errors.
  // Read the timestamp before re-arming; log only once the radio is listening.
  mulRlrStartReceive(dev);
  if (valid) {
    printf("MUL RLR: RX seq=%" PRIu32 " src=0x%02X ts=0x%02X%08" PRIX32 "\r\n",
           packet.sequence, (unsigned int)packet.source[0],
           (unsigned int)arrival.high8, arrival.low32);
  }
  return MAX_TIMEOUT;
}

uwbAlgorithm_t mulRlrAlgorithm = {
  .init = mulRlrInit,
  .onEvent = mulRlrOnEvent,
};
