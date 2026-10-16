/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_COMMON_H
#define MUL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUL_ADDRESS_LENGTH 8
#define MUL_TEST_FRAME_LENGTH 29
#define MUL_PACKET_TEST_BROADCAST 1

/* Wire format (all multibyte fields little endian, no C struct overlay):
 *  0..20: existing compressed-PAN, extended-address 802.15.4 data header
 *         FCF=0xdc41 (no ACK), MAC sequence, PAN=0xbccf, broadcast, source
 * 21..23: "MUL"
 *     24: packet type
 * 25..28: uint32 sequence (MAC sequence is its low byte)
 * The radio driver appends/removes the two-byte FCS, not included in length.
 */
typedef struct {
  uint8_t source[MUL_ADDRESS_LENGTH];
  uint32_t sequence;
} mulTestPacket_t;

void mulEncodeTestPacket(uint8_t frame[MUL_TEST_FRAME_LENGTH],
                         const uint8_t source[MUL_ADDRESS_LENGTH], uint32_t sequence);
bool mulDecodeTestPacket(const uint8_t *frame, size_t length, mulTestPacket_t *packet);

#endif // MUL_COMMON_H
