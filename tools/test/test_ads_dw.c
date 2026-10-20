/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_ads_dw.h"
#include "mul_role_state.h"
#include "task.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../vendor/libdw1000/src/libdw1000.c"
#include "../../vendor/libdw1000/src/libdw1000Spi.c"
#include "../../src/phy_interop_dw.c"
#include "../../src/mul_ads_dw.c"
static uint8_t regs[64][0x3000];
static uint32_t tick;
TickType_t xTaskGetTickCount(void) { return tick; }
static unsigned offset(const uint8_t *h,size_t n) { return n>1 ? (h[1]&127u)+(n>2 ? (unsigned)h[2]*128 : 0) : 0; }
static void rd(dwDevice_t *d,const void *h,size_t n,void *out,size_t count) {
  (void)d; const uint8_t *b=h; memcpy(out,regs[b[0]&63]+offset(b,n),count);
}
static void wr(dwDevice_t *d,const void *h,size_t n,const void *in,size_t count) {
  (void)d; const uint8_t *b=h; unsigned reg=b[0]&63,at=offset(b,n);
  if(reg==SYS_STATUS) { for(size_t i=0;i<count;i++) regs[reg][at+i]&=~((const uint8_t*)in)[i]; }
  else memcpy(regs[reg]+at,in,count);
}
static dwOps_t ops={.spiRead=rd,.spiWrite=wr};
static void receive(dwDevice_t *d,mulAdsPacket_t *p,uint64_t rx) {
  assert(mulAdsEncode(regs[RX_BUFFER],56,p)); regs[RX_FINFO][0]=58;
  mulEncodeTimestamp(regs[RX_TIME],rx); tick++;
  mulAdsAlgorithm.onEvent(d,eventPacketReceived);
}
static mulAdsPacket_t transmitted(void) {
  mulAdsPacket_t p; assert(mulAdsDecode(regs[TX_BUFFER],56,&p)); assert(regs[TX_FCTRL][0]==58); return p;
}
static void sent(dwDevice_t *d,uint64_t tx) {
  mulEncodeTimestamp(regs[TX_TIME],tx); tick++;
  mulAdsAlgorithm.onEvent(d,eventPacketSent);
}
int main(void) {
  dwDevice_t d; dwInit(&d,&ops); dwNewConfiguration(&d); dwSetDefaults(&d);
  phyInteropConfigure(&d); dwTime_t antenna={.full=MUL_ADS_DW_ANT_DTU}; dwSetAntenaDelay(&d,antenna); dwCommitConfiguration(&d);
  assert(d.channel==5 && d.frameCheck && !d.extendedFrameLength);
  uwbConfig_t config={0}; mulAdsAlgorithm.init(&config,&d);
  assert(sizeof(mulRoleScratch)==128);
#if MUL_TWR_ROLE==1
  tick=1000; mulAdsAlgorithm.onEvent(&d,eventTimeout);
  mulAdsPacket_t p=transmitted(); assert(p.type==mulAdsPoll && p.source==0x84 && p.destination==0x86);
  assert(getBit(d.sysctrl,LEN_SYS_CTRL,WAIT4RESP_BIT) && !getBit(d.sysctrl,LEN_SYS_CTRL,TXDLYS_BIT));
  const uint64_t t1=MUL_TIMESTAMP_MASK-99999999, t4=50000001, t5=250000001;
  sent(&d,t1); assert(state.core.stamps[0]==t1);
  p.source=0x86; p.destination=0x84; p.type=mulAdsResponse;
  receive(&d,&p,t4); assert(transmitted().type==mulAdsFinal);
  sent(&d,t5); assert(state.core.stamps[2]==t5);
  p.type=mulAdsReport; p.stamps[0]=10000; p.stamps[1]=150000000; p.stamps[2]=350010000;
  receive(&d,&p,300000000); assert(state.core.completed==1 && state.core.stage==adsIdle);
  // A missing TX completion cannot be replaced by an intended time.
  tick=2000; mulAdsAlgorithm.onEvent(&d,eventTimeout); assert(state.core.pending);
  tick=2100; mulAdsAlgorithm.onEvent(&d,eventTimeout); assert(!state.core.pending && state.core.aborted==1);
#else
  mulAdsPacket_t p={.source=0x86,.destination=0x84,.type=mulAdsPoll,.session=55,.transaction=1};
  receive(&d,&p,MUL_TIMESTAMP_MASK-20); assert(transmitted().type==mulAdsResponse);
  sent(&d,10000000); p.type=mulAdsFinal; receive(&d,&p,20000000);
  mulAdsPacket_t report=transmitted(); assert(report.type==mulAdsReport && report.stamps[0]==MUL_TIMESTAMP_MASK-20 && report.stamps[1]==10000000 && report.stamps[2]==20000000);
  sent(&d,21000000); assert(state.core.completed==1 && state.core.stage==adsIdle);
  p.type=mulAdsPoll; p.transaction=2; receive(&d,&p,0);
  mulAdsAlgorithm.onEvent(&d,eventReceiveFailed); assert(state.core.stage==adsIdle && state.errors==1);
  p.transaction=3; receive(&d,&p,1); assert(state.core.pending);
#endif
  regs[RX_FINFO][0]=127; mulAdsAlgorithm.onEvent(&d,eventPacketReceived); assert(state.invalid==1);
  regs[TX_ANTD][0]^=1; mulAdsAlgorithm.init(&config,&d); assert(!state.ready);
  tick+=1000; mulAdsAlgorithm.onEvent(&d,eventTimeout); assert(!state.core.attempts);
  puts("DW1000 ADS real-driver register/timestamp/recovery adapter test passed");
}
