/* SPDX-License-Identifier: MIT */
#ifndef PHY_DIAG_H
#define PHY_DIAG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef PHY_INTEROP_ROLE
#define PHY_INTEROP_ROLE 0
#endif
#if PHY_INTEROP_ROLE < 0 || PHY_INTEROP_ROLE > 2
#error "PHY_INTEROP_ROLE must be 0 (off), 1 (TX) or 2 (RX)"
#endif
#define PHY_DIAG_LENGTH 32u
#define PHY_DIAG_DW1000 0x84u
#define PHY_DIAG_DWM3000 0x86u
// Canonical layout/profile: lps-node-firmware/docs/phy_interop.md.
static inline void phyDiagEncode(uint8_t frame[PHY_DIAG_LENGTH],uint8_t source,uint32_t sequence)
{
  const uint8_t prefix[14]={0x41,0x88,0,0xcf,0xbc,0xff,0xff,0,0,'M','3','F','I',1};
  for (unsigned i=0;i<14;i++) frame[i]=prefix[i];
  frame[2]=(uint8_t)sequence; frame[7]=source;
  for (unsigned i=0;i<4;i++) frame[14+i]=(uint8_t)(sequence>>(8*i));
  for (unsigned i=0;i<14;i++) frame[18+i]=(uint8_t)(0xa5u ^ i ^ source ^ (sequence>>(8*(i%4))));
}
static inline bool phyDiagDecode(const uint8_t *frame,size_t length,uint8_t *source,uint32_t *sequence)
{
  if (!frame || length!=PHY_DIAG_LENGTH || !source || !sequence ||
      (frame[7]!=PHY_DIAG_DW1000 && frame[7]!=PHY_DIAG_DWM3000)) return false;
  uint32_t seq=0;
  for (unsigned i=0;i<4;i++) seq|=(uint32_t)frame[14+i]<<(8*i);
  uint8_t expected[PHY_DIAG_LENGTH]; phyDiagEncode(expected,frame[7],seq);
  for (unsigned i=0;i<PHY_DIAG_LENGTH;i++) if (frame[i]!=expected[i]) return false;
  *source=frame[7]; *sequence=seq; return true;
}
typedef struct {
  uint32_t valid,gaps,duplicates,old,last;
  bool baseline;
} phyDiagStats_t;
static inline void phyDiagObserve(phyDiagStats_t *s,uint32_t seq)
{
  s->valid++;
  if (!s->baseline) { s->baseline=true; s->last=seq; return; }
  uint32_t advance=seq-s->last;
  if (!advance) s->duplicates++;
  else if (advance<UINT32_C(0x80000000)) { s->gaps+=advance-1; s->last=seq; }
  else s->old++;
}
// Fits existing mode-exclusive LPS scratch. Platform wrappers own its lifetime.
typedef struct {
  phyDiagStats_t rx;
  uint32_t sequence,attempts,done,invalid,errors,timeouts,lastTxMs,lastReportMs;
  uint8_t frame[PHY_DIAG_LENGTH+2]; // libdw1000 FCS reserve read.
  bool pending;
} phyDiagState_t;
#endif
