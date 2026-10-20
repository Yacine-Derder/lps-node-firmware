/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_ADS_H
#define MUL_ADS_H
#include "mul_core.h"
#define MUL_ADS_LENGTH 56u
#define MUL_ADS_VERSION 1u
#define MUL_ADS_TIMEOUT_MS 100u
#define MUL_ADS_MAX_INTERVAL UINT64_C(6389760000) /* 100 ms, < half 40-bit wrap */
typedef enum { mulAdsPoll=0x71, mulAdsResponse, mulAdsFinal, mulAdsReport } mulAdsType_t;
typedef struct {
  uint64_t session, stamps[3]; /* REPORT only: responder t2,t3,t6. */
  uint32_t transaction;
  uint8_t source, destination, type;
} mulAdsPacket_t;
typedef enum { adsIdle, adsPollTx, adsResponseRx, adsResponseTx,
               adsFinalRx, adsFinalTx, adsReportRx, adsReportTx } mulAdsStage_t;
typedef struct {
  uint64_t session, stamps[3]; /* Initiator t1,t4,t5 OR responder t2,t3,t6. */
  double tof, rateRatio;
  uint32_t transaction, changedMs, attempts, completed, aborted, rejected;
  uint8_t local, peer, stage;
  bool initiator, pending, seen;
} mulAds_t;
bool mulAdsEncode(uint8_t *frame,size_t capacity,const mulAdsPacket_t *packet);
bool mulAdsDecode(const uint8_t *frame,size_t length,mulAdsPacket_t *packet);
/* Standard mixed-clock ADS formula; nominal DTU, NOT calibrated SI/reference DTU.
 * Fractional/negative ToF retained for bias investigation. Output unchanged on error. */
bool mulAdsCalculate(const uint64_t stamps[6],double *tof,double *responderRateRatio);
bool mulAdsInit(mulAds_t *s,uint8_t local,uint8_t peer,bool initiator,uint64_t session);
bool mulAdsBegin(mulAds_t *s,uint32_t nowMs);
bool mulAdsTakeTx(mulAds_t *s,uint32_t nowMs,mulAdsPacket_t *packet);
bool mulAdsTxDone(mulAds_t *s,uint64_t actualMarker,uint32_t nowMs);
/* true only when accepted, not merely decoded. */
bool mulAdsReceive(mulAds_t *s,const mulAdsPacket_t *packet,uint64_t rxMarker,uint32_t nowMs);
void mulAdsAbort(mulAds_t *s);
bool mulAdsTick(mulAds_t *s,uint32_t nowMs); /* true on deadline abort */
#endif
