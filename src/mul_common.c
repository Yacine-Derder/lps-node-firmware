/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_common.h"

#include <string.h>

void mulEncodeTestPacket(uint8_t frame[MUL_TEST_FRAME_LENGTH],
                         const uint8_t source[MUL_ADDRESS_LENGTH], uint32_t sequence)
{
  frame[0] = 0x41;
  frame[1] = 0xdc;
  frame[2] = (uint8_t)sequence;
  frame[3] = 0xcf;
  frame[4] = 0xbc;
  memset(&frame[5], 0xff, MUL_ADDRESS_LENGTH);
  memcpy(&frame[13], source, MUL_ADDRESS_LENGTH);
  memcpy(&frame[21], "MUL", 3);
  frame[24] = MUL_PACKET_TEST_BROADCAST;
  for (unsigned int i = 0; i < 4; i++) {
    frame[25 + i] = (uint8_t)(sequence >> (8 * i));
  }
}

bool mulDecodeTestPacket(const uint8_t *frame, size_t length, mulTestPacket_t *packet)
{
  if (length != MUL_TEST_FRAME_LENGTH || frame == NULL || packet == NULL) {
    return false;
  }
  if (frame[0] != 0x41 || frame[1] != 0xdc ||
      frame[3] != 0xcf || frame[4] != 0xbc ||
      memcmp(&frame[21], "MUL", 3) != 0 ||
      frame[24] != MUL_PACKET_TEST_BROADCAST || frame[2] != frame[25]) {
    return false;
  }
  for (unsigned int i = 0; i < MUL_ADDRESS_LENGTH; i++) {
    if (frame[5 + i] != 0xff) {
      return false;
    }
  }

  memcpy(packet->source, &frame[13], MUL_ADDRESS_LENGTH);
  packet->sequence = 0;
  for (unsigned int i = 0; i < 4; i++) {
    packet->sequence |= (uint32_t)frame[25 + i] << (8 * i);
  }
  return true;
}
