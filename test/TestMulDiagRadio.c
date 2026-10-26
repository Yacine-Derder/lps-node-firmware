/* SPDX-License-Identifier: MIT */
#include "unity.h"
#ifndef MUL_DIAG_ROLE
#define MUL_DIAG_ROLE 1
#endif
#include "mul_core.h"
#include "mul_common.h"
#include "mul_diag_dw.h"
#include "mul_role_state.h"
#include "task.h"
#include <string.h>
#include "../vendor/libdw1000/src/libdw1000.c"
#include "../vendor/libdw1000/src/libdw1000Spi.c"
#include "../src/phy_interop_dw.c"
#include "../src/mul_diag_dw.c"
static uint8_t regs[64][0x3000];
static dwDevice_t dev;
static uint32_t tick,starts;
TickType_t xTaskGetTickCount(void) { return tick; }
static unsigned offset(const uint8_t *h,size_t n) { return n>1 ? (h[1]&127u)+(n>2 ? (unsigned)h[2]*128 : 0) : 0; }
static void rd(dwDevice_t *d,const void *h,size_t n,void *out,size_t count) {
  (void)d; const uint8_t *header=h; memcpy(out,regs[header[0]&63]+offset(header,n),count);
}
static void wr(dwDevice_t *d,const void *h,size_t n,const void *in,size_t count) {
  (void)d; const uint8_t *header=h; unsigned reg=header[0]&63,at=offset(header,n);
  if (reg==SYS_STATUS) { for (size_t i=0;i<count;i++) regs[reg][at+i]&=~((const uint8_t*)in)[i]; return; }
  memcpy(regs[reg]+at,in,count);
  if (reg==SYS_CTRL && (regs[reg][0]&(1u<<TXSTRT_BIT))) starts++;
}
static dwOps_t ops={.spiRead=rd,.spiWrite=wr};
static void rx(dwDevice_t *d) { mulDiagAlgorithm.onEvent(d,eventPacketReceived); }
static void bad(dwDevice_t *d) { mulDiagAlgorithm.onEvent(d,eventReceiveFailed); }
void setUp(void) {
  memset(regs,0,sizeof(regs)); tick=starts=0;
  dwInit(&dev,&ops); dwNewConfiguration(&dev); dwSetDefaults(&dev);
  phyInteropConfigure(&dev); dwCommitConfiguration(&dev);
  uwbConfig_t config={0}; mulDiagAlgorithm.init(&config,&dev);
  dwAttachReceivedHandler(&dev,rx); dwAttachReceiveFailedHandler(&dev,bad);
}
void tearDown(void) {}
void test_mulDiagRealDriverProfileUsesCommonSfdPhrFcsAndSharedScratch(void) {
  TEST_ASSERT_EQUAL_UINT8(CHANNEL_5,dev.channel); TEST_ASSERT_EQUAL_UINT8(TX_PULSE_FREQ_64MHZ,dev.pulseFrequency);
  TEST_ASSERT_EQUAL_UINT8(TX_PREAMBLE_LEN_128,dev.preambleLength); TEST_ASSERT_EQUAL_UINT8(9,dev.preambleCode);
  TEST_ASSERT_EQUAL_UINT8(TRX_RATE_6800KBPS,dev.dataRate); TEST_ASSERT_TRUE(dev.frameCheck);
  TEST_ASSERT_FALSE(dev.extendedFrameLength); TEST_ASSERT_FALSE(dev.permanentReceive);
  TEST_ASSERT_FALSE(getBit(dev.chanctrl,LEN_CHAN_CTRL,DWSFD_BIT));
  TEST_ASSERT_FALSE(getBit(dev.chanctrl,LEN_CHAN_CTRL,TNSSFD_BIT)); TEST_ASSERT_FALSE(getBit(dev.chanctrl,LEN_CHAN_CTRL,RNSSFD_BIT));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(dev.chanctrl,regs[CHAN_CTRL],LEN_CHAN_CTRL);
  TEST_ASSERT_TRUE(sizeof(mulDiagDwState_t)<=128); TEST_ASSERT_TRUE(sizeof(mulRoleScratch)<=160);
}
void test_mulDiagRealDriverRoleWatchdogBoundedReadsAndRecovery(void) {
#if MUL_DIAG_ROLE==1
  tick=1000; mulDiagAlgorithm.onEvent(&dev,eventTimeout);
  TEST_ASSERT_EQUAL_UINT32(1,starts); TEST_ASSERT_TRUE(state.pending);
  TEST_ASSERT_FALSE(getBit(dev.sysctrl,LEN_SYS_CTRL,TXDLYS_BIT));
  mulCoreDiag_t packet;
  TEST_ASSERT_EQUAL(mulCoreOk,mulCoreDecodeDiag(regs[TX_BUFFER],44,&packet));
  TEST_ASSERT_EQUAL_UINT8(0x84,packet.source[0]); TEST_ASSERT_FALSE(packet.markerValid);
  TEST_ASSERT_EQUAL_UINT8(46,regs[TX_FCTRL][0]);
  memset(regs[TX_TIME],0xfe,5);
  mulDiagAlgorithm.onEvent(&dev,eventPacketSent); TEST_ASSERT_EQUAL_UINT32(1,state.done);
  tick=2000; mulDiagAlgorithm.onEvent(&dev,eventTimeout);
  TEST_ASSERT_EQUAL(mulCoreOk,mulCoreDecodeDiag(regs[TX_BUFFER],44,&packet));
  TEST_ASSERT_TRUE(packet.markerValid); TEST_ASSERT_EQUAL_UINT64(UINT64_C(0xfefefefefe),packet.txMarker);
  TEST_ASSERT_EQUAL_UINT32(0,packet.markerSequence);
  tick=2000; mulDiagAlgorithm.onEvent(&dev,eventTimeout);
  tick=2100; mulDiagAlgorithm.onEvent(&dev,eventTimeout); TEST_ASSERT_EQUAL_UINT32(1,state.errors);
#else
  mulCoreDiag_t packet={.source={0x86,0,0,0,0,0,0xcf,0xbc},.sequence=UINT32_MAX,.radio=MUL_CORE_DW3000};
  mulCoreEncodeDiag(regs[RX_BUFFER],44,&packet); regs[RX_FINFO][0]=46;
  regs[SYS_STATUS][1]=1u<<(RXFCG_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(1,state.rx.valid); TEST_ASSERT_EQUAL_UINT(RX_MODE,dev.deviceMode);
  packet.sequence=0; mulCoreEncodeDiag(regs[RX_BUFFER],44,&packet);
  regs[SYS_STATUS][1]=1u<<(RXFCG_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(0,state.rx.gaps);
  regs[RX_BUFFER][43]^=1; regs[SYS_STATUS][1]=1u<<(RXFCG_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(1,state.invalid);
  regs[RX_FINFO][0]=127; regs[SYS_STATUS][1]=1u<<(RXFCG_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(2,state.invalid);
  regs[SYS_STATUS][1]=1u<<(RXFCE_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(1,state.errors); TEST_ASSERT_EQUAL_UINT(RX_MODE,dev.deviceMode);
#endif
#if MUL_DIAG_ROLE==2
  packet.sequence=3000; mulCoreEncodeDiag(regs[RX_BUFFER],44,&packet); regs[RX_FINFO][0]=46;
  mulDiagAlgorithm.onEvent(&dev,eventPacketReceived);
  packet.sequence=0; mulCoreEncodeDiag(regs[RX_BUFFER],44,&packet);
  mulDiagAlgorithm.onEvent(&dev,eventPacketReceived);
  TEST_ASSERT_EQUAL_UINT32(1,state.rx.old); TEST_ASSERT_EQUAL_UINT32(3000,state.rx.last);
  tick=5000; packet.sequence=1; mulCoreEncodeDiag(regs[RX_BUFFER],44,&packet);
  mulDiagAlgorithm.onEvent(&dev,eventPacketReceived);
  TEST_ASSERT_EQUAL_UINT32(1,state.rx.old); TEST_ASSERT_EQUAL_UINT32(1,state.rx.last);
#endif
  state.lastTxMs=UINT32_MAX-500; tick=600;
  mulDiagAlgorithm.onEvent(&dev,eventTimeout); // Coarse timer wrap is bounded.
}
