/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_alr.h"
#include "mul_common.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define MUL_TX_INTERVAL_MS 1000

static uint8_t source[MUL_ADDRESS_LENGTH];
// dwSetData() reads length + 2 bytes when hardware FCS is enabled.
static uint8_t txFrame[MUL_TEST_FRAME_LENGTH + 2];
static uint32_t nextSequence;
static uint32_t pendingSequence;
static bool txPending;

static void mulAlrInit(uwbConfig_t *config, dwDevice_t *dev)
{
  dwIdle(dev);
  dwReceivePermanently(dev, false);
  memcpy(source, config->address, sizeof(source));
  nextSequence = 0;
  txPending = false;
  printf("MUL ALR: Mode initialized\r\n");
}

static uint32_t mulAlrOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  switch (event) {
    case eventTimeout:
      if (txPending) {
        // No TX-done interrupt: abort the old attempt and keep making progress.
        dwIdle(dev);
        printf("MUL ALR: TX timeout seq=%" PRIu32 "\r\n", pendingSequence);
      }
      pendingSequence = nextSequence++;
      mulEncodeTestPacket(txFrame, source, pendingSequence);
      dwNewTransmit(dev);
      dwSetDefaults(dev);
      dwSetData(dev, txFrame, MUL_TEST_FRAME_LENGTH);
      dwWaitForResponse(dev, false);
      txPending = true;
      dwStartTransmit(dev);
      break;
    case eventPacketSent:
      if (txPending) {
        txPending = false;
        printf("MUL ALR: TX seq=%" PRIu32 " src=0x%02X\r\n",
               pendingSequence, (unsigned int)source[0]);
      }
      break;
    case eventReceiveTimeout:
    case eventReceiveFailed:
      dwIdle(dev);
      txPending = false;
      printf("MUL ALR: Unexpected RX error\r\n");
      break;
    case eventPacketReceived:
      break;
  }
  // The first eventTimeout is immediate; later sends follow TX completion.
  // This is also a bounded recovery timeout when completion is lost.
  return MUL_TX_INTERVAL_MS;
}

uwbAlgorithm_t mulAlrAlgorithm = {
  .init = mulAlrInit,
  .onEvent = mulAlrOnEvent,
};
