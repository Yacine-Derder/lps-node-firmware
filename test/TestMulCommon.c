/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_common.h"

#include <string.h>

static const uint8_t source[8] = {0x84, 0, 0, 0, 0, 0, 0xcf, 0xbc};
static uint8_t frame[MUL_TEST_FRAME_LENGTH];

void setUp(void) {
  mulEncodeTestPacket(frame, source, UINT32_C(0x12345678));
}

void tearDown(void) {}

void test_mulEncodingMatchesWireFormat(void) {
  const uint8_t expected[] = {
    0x41, 0xdc, 0x78, 0xcf, 0xbc,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x84, 0, 0, 0, 0, 0, 0xcf, 0xbc,
    'M', 'U', 'L', 1, 0x78, 0x56, 0x34, 0x12
  };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, sizeof(expected));
}

void test_mulDecodingPreservesAddressAndSequenceBoundaries(void) {
  const uint32_t sequences[] = {0, 255, 256, UINT32_C(0x80000000), UINT32_MAX};
  mulTestPacket_t packet;
  for (unsigned int i = 0; i < sizeof(sequences) / sizeof(sequences[0]); i++) {
    mulEncodeTestPacket(frame, source, sequences[i]);
    TEST_ASSERT_TRUE(mulDecodeTestPacket(frame, sizeof(frame), &packet));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source, packet.source, sizeof(source));
    TEST_ASSERT_EQUAL_UINT32(sequences[i], packet.sequence);
  }
}

void test_mulRejectsEveryIncorrectFrameLength(void) {
  mulTestPacket_t packet;
  for (size_t length = 0; length <= 1024; length++) {
    if (length != sizeof(frame)) {
      TEST_ASSERT_FALSE(mulDecodeTestPacket(frame, length, &packet));
    }
  }
  TEST_ASSERT_FALSE(mulDecodeTestPacket(NULL, sizeof(frame), &packet));
  TEST_ASSERT_FALSE(mulDecodeTestPacket(frame, sizeof(frame), NULL));
}

void test_mulRejectsUnrelatedHeadersProtocolsAndTypes(void) {
  const unsigned int checked[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                  21, 22, 23, 24};
  mulTestPacket_t packet;
  memset(&packet, 0xa5, sizeof(packet));
  mulTestPacket_t original = packet;
  for (unsigned int i = 0; i < sizeof(checked) / sizeof(checked[0]); i++) {
    frame[checked[i]] ^= 1;
    TEST_ASSERT_FALSE(mulDecodeTestPacket(frame, sizeof(frame), &packet));
    TEST_ASSERT_EQUAL_MEMORY(&original, &packet, sizeof(packet));
    frame[checked[i]] ^= 1;
  }
}
