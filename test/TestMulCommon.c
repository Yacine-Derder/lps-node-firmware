/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "unity.h"
#include "mul_common.h"

#include <string.h>

static const uint8_t source[8] = {0x84, 0, 0, 0, 0, 0, 0xcf, 0xbc};
static uint8_t frame[MUL_TEST_FRAME_LENGTH];

void setUp(void) {
  mulEncodeTestPacket(frame, source, UINT32_C(0x12345678), UINT64_C(0x0123456789));
}

void tearDown(void) {}

void test_mulEncodingMatchesWireFormat(void) {
  const uint8_t expected[] = {
    0x41, 0xdc, 0x78, 0xcf, 0xbc,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x84, 0, 0, 0, 0, 0, 0xcf, 0xbc,
    'M', 'U', 'L', 1, 0x78, 0x56, 0x34, 0x12,
    0x89, 0x67, 0x45, 0x23, 0x01
  };
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, sizeof(expected));
}

void test_mulDecodingPreservesAddressAndSequenceBoundaries(void) {
  const uint32_t sequences[] = {0, 255, 256, UINT32_C(0x80000000), UINT32_MAX};
  mulTestPacket_t packet;
  for (unsigned int i = 0; i < sizeof(sequences) / sizeof(sequences[0]); i++) {
    mulEncodeTestPacket(frame, source, sequences[i], UINT64_C(0x0123456789));
    TEST_ASSERT_TRUE(mulDecodeTestPacket(frame, sizeof(frame), &packet));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source, packet.source, sizeof(source));
    TEST_ASSERT_EQUAL_UINT32(sequences[i], packet.sequence);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x0123456789), packet.txTimestamp);
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

void test_mulTimestampsPreserveAllFortyBitsAndMaskUpperBits(void) {
  const uint64_t values[] = {0, 1, UINT64_C(0x0123456789), MUL_TIMESTAMP_MASK - 1,
                            MUL_TIMESTAMP_MASK, UINT64_C(0x1234ffffffffff)};
  for (unsigned int i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    uint8_t bytes[MUL_TIMESTAMP_BYTES];
    mulEncodeTimestamp(bytes, values[i]);
    TEST_ASSERT_EQUAL_UINT64(values[i] & MUL_TIMESTAMP_MASK, mulDecodeTimestamp(bytes));
    TEST_ASSERT_EQUAL_UINT64(values[i] & MUL_TIMESTAMP_MASK, mulTimestampMask(values[i]));
    mulEncodeTestPacket(frame, source, 0, values[i]);
    mulTestPacket_t packet;
    TEST_ASSERT_TRUE(mulDecodeTestPacket(frame, sizeof(frame), &packet));
    TEST_ASSERT_EQUAL_UINT64(values[i] & MUL_TIMESTAMP_MASK, packet.txTimestamp);
  }
}

void test_mulTimestampDifferenceHandlesWrapAndHalfPeriod(void) {
  TEST_ASSERT_EQUAL_INT64(0, mulTimestampDifference(0, 0));
  TEST_ASSERT_EQUAL_INT64(1, mulTimestampDifference(0, MUL_TIMESTAMP_MASK));
  TEST_ASSERT_EQUAL_INT64(-1, mulTimestampDifference(MUL_TIMESTAMP_MASK, 0));
  TEST_ASSERT_EQUAL_INT64(7, mulTimestampDifference(3, MUL_TIMESTAMP_MASK - 3));
  TEST_ASSERT_EQUAL_INT64(-7, mulTimestampDifference(MUL_TIMESTAMP_MASK - 3, 3));
  TEST_ASSERT_EQUAL_INT64((INT64_C(1) << 39) - 1,
                        mulTimestampDifference((UINT64_C(1) << 39) - 1, 0));
  TEST_ASSERT_EQUAL_INT64(-(INT64_C(1) << 39),
                        mulTimestampDifference(UINT64_C(1) << 39, 0));
  TEST_ASSERT_EQUAL_INT64(1, mulTimestampDifference(UINT64_C(1) << 40, MUL_TIMESTAMP_MASK));
}

void test_mulRoleSelectionUsesEveryAddressByteAndFailsClosed(void) {
  const uint8_t reference[8] = {0x84,0,0,0,0,0,0xcf,0xbc};
  uint8_t address[8];
  TEST_ASSERT_EQUAL_INT(mulAlrReference, mulAlrRoleForAddress(reference));
  memcpy(address, reference, sizeof(address));
  address[0] = 0x85;
  TEST_ASSERT_EQUAL_INT(mulAlrFollower, mulAlrRoleForAddress(address));
  TEST_ASSERT_EQUAL_INT(mulAlrUnsupported, mulAlrRoleForAddress(NULL));
  for (unsigned int i = 0; i < sizeof(address); i++) {
    memcpy(address, reference, sizeof(address));
    address[i] ^= 2;
    TEST_ASSERT_EQUAL_INT(mulAlrUnsupported, mulAlrRoleForAddress(address));
  }
}

void test_mulSourceObservationOrdersModuloSequenceAndRetainsAcceptedPair(void) {
  mulSourceState_t state = {0};
  mulTestPacket_t packet = {.sequence = UINT32_MAX, .txTimestamp = MUL_TIMESTAMP_MASK};
  TEST_ASSERT_EQUAL_INT(mulSequenceFirst, mulObservePacket(&state, &packet, 7));
  packet.sequence = 0;
  packet.txTimestamp = 3;
  TEST_ASSERT_EQUAL_INT(mulSequenceForward, mulObservePacket(&state, &packet, 9));
  packet.sequence = 3;
  TEST_ASSERT_EQUAL_INT(mulSequenceGap, mulObservePacket(&state, &packet, 10));
  TEST_ASSERT_EQUAL_UINT32(2, state.gaps);
  packet.txTimestamp = 99;
  TEST_ASSERT_EQUAL_INT(mulSequenceDuplicate, mulObservePacket(&state, &packet, 99));
  packet.sequence = 1;
  TEST_ASSERT_EQUAL_INT(mulSequenceOld, mulObservePacket(&state, &packet, 99));
  packet.sequence = 3 + UINT32_C(0x80000000);
  TEST_ASSERT_EQUAL_INT(mulSequenceOld, mulObservePacket(&state, &packet, 99));
  TEST_ASSERT_EQUAL_UINT32(3, state.sequence);
  TEST_ASSERT_EQUAL_UINT32(6, state.received);
  TEST_ASSERT_EQUAL_UINT32(1, state.duplicates);
  TEST_ASSERT_EQUAL_UINT32(2, state.outOfOrder);
  TEST_ASSERT_EQUAL_UINT64(3, state.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(10, state.rxTimestamp);
}

void test_mulSourceObservationMasksTimestampsWithoutRelatingClockDomains(void) {
  mulSourceState_t state = {0};
  mulTestPacket_t packet = {.sequence = 0, .txTimestamp = UINT64_MAX};
  TEST_ASSERT_EQUAL_INT(mulSequenceFirst,
    mulObservePacket(&state, &packet, UINT64_C(0x123400000001)));
  TEST_ASSERT_EQUAL_UINT64(MUL_TIMESTAMP_MASK, state.txTimestamp);
  TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x3400000001), state.rxTimestamp);
}
