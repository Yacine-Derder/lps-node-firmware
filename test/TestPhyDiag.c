/* SPDX-License-Identifier: MIT */
#include "unity.h"
#include "phy_diag.h"
#include <string.h>
void setUp(void) {}
void tearDown(void) {}
void test_phyDiagGoldenSequenceAndEveryCorruptedByteAreChecked(void) {
  uint8_t frame[32],src=0; uint32_t seq=0;
  const uint8_t golden[32]={0x41,0x88,0,0xcf,0xbc,0xff,0xff,0x84,0,'M','3','F','I',1,0,0,0,0,
    0x21,0x20,0x23,0x22,0x25,0x24,0x27,0x26,0x29,0x28,0x2b,0x2a,0x2d,0x2c};
  phyDiagEncode(frame,0x84,0); TEST_ASSERT_EQUAL_UINT8_ARRAY(golden,frame,32);
  for (unsigned i=0;i<32;i++) {
    frame[i]^=1; TEST_ASSERT_FALSE(phyDiagDecode(frame,32,&src,&seq)); frame[i]^=1;
  }
  TEST_ASSERT_TRUE(phyDiagDecode(frame,32,&src,&seq)); TEST_ASSERT_EQUAL_UINT8(0x84,src);
}
void test_phyDiagLengthVersionSourceOutputAndWrapAreStrict(void) {
  uint8_t frame[34]={0},src=3; uint32_t seq=77;
  phyDiagEncode(frame,0x86,UINT32_MAX);
  TEST_ASSERT_FALSE(phyDiagDecode(frame,34,&src,&seq)); TEST_ASSERT_EQUAL_UINT32(77,seq);
  TEST_ASSERT_FALSE(phyDiagDecode(frame,31,&src,&seq)); TEST_ASSERT_FALSE(phyDiagDecode(NULL,32,&src,&seq));
  TEST_ASSERT_TRUE(phyDiagDecode(frame,32,&src,&seq)); TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,seq);
  phyDiagStats_t s={0}; phyDiagObserve(&s,UINT32_MAX); phyDiagObserve(&s,0); phyDiagObserve(&s,0);
  phyDiagObserve(&s,2); phyDiagObserve(&s,1);
  TEST_ASSERT_EQUAL_UINT32(1,s.gaps); TEST_ASSERT_EQUAL_UINT32(1,s.duplicates); TEST_ASSERT_EQUAL_UINT32(1,s.old);
}
