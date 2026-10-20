/* SPDX-License-Identifier: MIT */
#include "unity.h"
#ifndef PHY_INTEROP_ROLE
#define PHY_INTEROP_ROLE 1
#endif
#include "phy_diag.h"
#include "mul_role_state.h"
#include "task.h"
#include <string.h>
#include "../vendor/libdw1000/src/libdw1000.c"
#include "../vendor/libdw1000/src/libdw1000Spi.c"
#include "../src/phy_interop_dw.c"
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
static void rx(dwDevice_t *d) { phyInteropAlgorithm.onEvent(d,eventPacketReceived); }
static void bad(dwDevice_t *d) { phyInteropAlgorithm.onEvent(d,eventReceiveFailed); }
void setUp(void) {
  memset(regs,0,sizeof(regs)); tick=starts=0;
  dwInit(&dev,&ops); dwNewConfiguration(&dev); dwSetDefaults(&dev);
  phyInteropConfigure(&dev); dwCommitConfiguration(&dev);
  uwbConfig_t config={0}; phyInteropAlgorithm.init(&config,&dev);
  dwAttachReceivedHandler(&dev,rx); dwAttachReceiveFailedHandler(&dev,bad);
}
void tearDown(void) {}
void test_phyRealDriverProfileUsesCommonSfdPhrFcsAndSharedScratch(void) {
  TEST_ASSERT_EQUAL_UINT8(CHANNEL_5,dev.channel); TEST_ASSERT_EQUAL_UINT8(TX_PULSE_FREQ_64MHZ,dev.pulseFrequency);
  TEST_ASSERT_EQUAL_UINT8(TX_PREAMBLE_LEN_128,dev.preambleLength); TEST_ASSERT_EQUAL_UINT8(9,dev.preambleCode);
  TEST_ASSERT_EQUAL_UINT8(TRX_RATE_6800KBPS,dev.dataRate); TEST_ASSERT_TRUE(dev.frameCheck);
  TEST_ASSERT_FALSE(dev.extendedFrameLength); TEST_ASSERT_FALSE(dev.permanentReceive);
  TEST_ASSERT_FALSE(getBit(dev.chanctrl,LEN_CHAN_CTRL,DWSFD_BIT));
  TEST_ASSERT_FALSE(getBit(dev.chanctrl,LEN_CHAN_CTRL,TNSSFD_BIT)); TEST_ASSERT_FALSE(getBit(dev.chanctrl,LEN_CHAN_CTRL,RNSSFD_BIT));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(dev.chanctrl,regs[CHAN_CTRL],LEN_CHAN_CTRL);
  TEST_ASSERT_TRUE(sizeof(phyDiagState_t)<=128); TEST_ASSERT_EQUAL_UINT(128,sizeof(mulRoleScratch));
}
void test_phyRealDriverRoleWatchdogBoundedReadsAndRecovery(void) {
#if PHY_INTEROP_ROLE==1
  tick=1000; phyInteropAlgorithm.onEvent(&dev,eventTimeout);
  TEST_ASSERT_EQUAL_UINT32(1,starts); TEST_ASSERT_TRUE(state.pending);
  TEST_ASSERT_FALSE(getBit(dev.sysctrl,LEN_SYS_CTRL,TXDLYS_BIT));
  uint8_t source; uint32_t sequence;
  TEST_ASSERT_TRUE(phyDiagDecode(regs[TX_BUFFER],32,&source,&sequence)); TEST_ASSERT_EQUAL_UINT8(0x84,source);
  TEST_ASSERT_EQUAL_UINT8(34,regs[TX_FCTRL][0]);
  phyInteropAlgorithm.onEvent(&dev,eventPacketSent); TEST_ASSERT_EQUAL_UINT32(1,state.done);
  tick=2000; phyInteropAlgorithm.onEvent(&dev,eventTimeout);
  tick=2100; phyInteropAlgorithm.onEvent(&dev,eventTimeout); TEST_ASSERT_EQUAL_UINT32(1,state.errors);
#else
  phyDiagEncode(regs[RX_BUFFER],0x86,UINT32_MAX); regs[RX_FINFO][0]=34;
  regs[SYS_STATUS][1]=1u<<(RXFCG_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(1,state.rx.valid); TEST_ASSERT_EQUAL_UINT(RX_MODE,dev.deviceMode);
  phyDiagEncode(regs[RX_BUFFER],0x86,0); regs[SYS_STATUS][1]=1u<<(RXFCG_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(0,state.rx.gaps);
  regs[RX_BUFFER][31]^=1; regs[SYS_STATUS][1]=1u<<(RXFCG_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(1,state.invalid);
  regs[RX_FINFO][0]=127; regs[SYS_STATUS][1]=1u<<(RXFCG_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(2,state.invalid); // no oversized buffer read
  regs[SYS_STATUS][1]=1u<<(RXFCE_BIT-8); dwHandleInterrupt(&dev);
  TEST_ASSERT_EQUAL_UINT32(1,state.errors); TEST_ASSERT_EQUAL_UINT(RX_MODE,dev.deviceMode);
#endif
  state.lastTxMs=UINT32_MAX-500; tick=600;
  phyInteropAlgorithm.onEvent(&dev,eventTimeout); // Coarse timer wrap is bounded.
}
