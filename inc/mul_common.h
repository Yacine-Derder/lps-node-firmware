/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_COMMON_H
#define MUL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUL_ADDRESS_LENGTH 8
#define MUL_TEST_FRAME_LENGTH 34
#define MUL_PACKET_TEST_BROADCAST 1
#define MUL_TIMESTAMP_MASK UINT64_C(0xffffffffff)
#define MUL_TIMESTAMP_BYTES 5
#define MUL_TX_TIMESTAMP_OFFSET 29

/* Wire format (all multibyte fields little endian, no C struct overlay):
 *  0..20: existing compressed-PAN, extended-address 802.15.4 data header
 *         FCF=0xdc41 (no ACK), MAC sequence, PAN=0xbccf, broadcast, source
 * 21..23: "MUL"
 *     24: packet type
 * 25..28: uint32 sequence (MAC sequence is its low byte)
 * 29..33: uint40 expected antenna-referenced TX timestamp, DW1000 time units
 * The radio driver appends/removes the two-byte FCS, not included in length.
 */
typedef struct {
  uint8_t source[MUL_ADDRESS_LENGTH];
  uint32_t sequence;
  uint64_t txTimestamp;
} mulTestPacket_t;

void mulEncodeTestPacket(uint8_t frame[MUL_TEST_FRAME_LENGTH],
                         const uint8_t source[MUL_ADDRESS_LENGTH], uint32_t sequence,
                         uint64_t txTimestamp);
bool mulDecodeTestPacket(const uint8_t *frame, size_t length, mulTestPacket_t *packet);

uint64_t mulTimestampMask(uint64_t timestamp);
void mulEncodeTimestamp(uint8_t bytes[MUL_TIMESTAMP_BYTES], uint64_t timestamp);
uint64_t mulDecodeTimestamp(const uint8_t bytes[MUL_TIMESTAMP_BYTES]);
// Signed (a - b) modulo 2^40; unambiguous only within half a counter period.
// The exactly-half-period case is represented as -2^39.
int64_t mulTimestampDifference(uint64_t a, uint64_t b);

#endif // MUL_COMMON_H
