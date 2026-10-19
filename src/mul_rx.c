/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_rx.h"

void mulAntennaConfigure(dwDevice_t *dev)
{
  // libdw1000 caches ONE value and commits it to BOTH registers. MUL needs
  // independent directions. Cache TX for legacy driver helpers; write RX
  // explicitly, and never full-commit in MUL. Validation detects such overwrites.
  dev->antennaDelay.full = MUL_ANTENNA_TX_DTU;
  const uint8_t tx[] = {MUL_ANTENNA_TX_DTU & 0xff, MUL_ANTENNA_TX_DTU >> 8};
  const uint8_t rx[] = {MUL_ANTENNA_RX_DTU & 0xff, MUL_ANTENNA_RX_DTU >> 8};
  dwSpiWrite(dev, TX_ANTD, NO_SUB, tx, sizeof(tx));
  dwSpiWrite(dev, LDE_IF, LDE_RXANTD_SUB, rx, sizeof(rx));
}

bool mulAntennaValid(dwDevice_t *dev)
{
  return dwSpiRead16(dev, TX_ANTD, NO_SUB) == MUL_ANTENNA_TX_DTU &&
         dwSpiRead16(dev, LDE_IF, LDE_RXANTD_SUB) == MUL_ANTENNA_RX_DTU &&
         dev->antennaDelay.full == MUL_ANTENNA_TX_DTU;
}

void mulRxRearm(dwDevice_t *dev)
{
  dwNewReceive(dev);
  dwSetDefaults(dev);
  dwStartReceive(dev);
}

void mulRxInit(dwDevice_t *dev)
{
  // Preserve uwbInit()'s PHY. Disable automatic RX re-enable so timestamps and
  // buffers remain stable until the event callback has captured the observation.
  dwNewConfiguration(dev);
  dwReceivePermanently(dev, false);
  dwSetFrameFilter(dev, false);
  dwSetReceiverAutoReenable(dev, false);
  dwSetReceiveWaitTimeout(dev, 0);
  dwInterruptOnReceiveFailed(dev, true);
  // These changes affect only RX/IRQ controls. A full commit re-tunes the PHY
  // and overwrites live TX_ANTD/LDE_RXANTD from the driver's cached defaults.
  dwWriteSystemConfigurationRegister(dev);
  dwWriteSystemEventMaskRegister(dev);
  mulRxRearm(dev);
}

bool mulRxRead(dwDevice_t *dev, mulTestPacket_t *packet, uint64_t *localRx)
{
  uint8_t frame[MUL_TEST_FRAME_LENGTH];
  unsigned int length = dwGetDataLength(dev);
  if (length != sizeof(frame)) {
    return false;
  }
  dwGetData(dev, frame, length);
  if (!mulDecodeTestPacket(frame, length, packet)) {
    return false;
  }
  dwTime_t arrival = {.full = 0};
  dwGetRawReceiveTimestamp(dev, &arrival);
  *localRx = mulTimestampMask(arrival.full);
  return true;
}
