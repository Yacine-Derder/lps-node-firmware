/* Host-only ABI bridge: replay uses the production firmware estimators. */
#include "mul_clock_fit.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef struct {mulClock_t median,reg;mulClockFit_t fit;mulClockFitConfig_t cfg;mulNetwork_t network;} Replay;
void *replay_new(unsigned window,unsigned period,unsigned reference,unsigned local) {
 if(window!=5||period<10||period>2000||reference>254||local>254||reference==local)return NULL;
 Replay *s=calloc(1,sizeof(*s));if(!s)return NULL;
 if(!mulNetworkInit(&s->network,(uint8_t)reference,(uint8_t)local)){free(s);return NULL;}
 mulClockReset(&s->median);mulClockReset(&s->reg);mulClockFitReset(&s->fit);mulClockFitDefaults(&s->cfg);
 s->cfg.estimator=2;s->cfg.window=window;s->cfg.periodMs=period;
 if(!mulClockFitConfigValid(&s->cfg)){free(s);return NULL;}return s;
}
void replay_free(void *p){free(p);}
int replay_step(void *context,unsigned sequence,unsigned long long tx,unsigned long long rx,unsigned now,unsigned captureValid,int delayDtu,int delayAge,char *out,unsigned capacity) {
 Replay *s=context;mulTestPacket_t p={.sequence=sequence,.txTimestamp=tx};memcpy(p.source,s->network.addresses[0],8);
 mulClockObservePeriodNetwork(&s->network,&s->median,&p,rx,now,captureValid!=0,s->cfg.periodMs);
 mulClockDelay_t delay={.epoch=s->reg.epoch,.oneWayDtu=delayDtu,.measuredMs=now-(unsigned)delayAge,.maxAgeMs=1000,.referenceSequence=sequence,.valid=delayDtu>=0};
 memcpy(delay.reference,s->network.addresses[0],8);memcpy(delay.follower,s->network.addresses[1],8);
 s->cfg.compensate=delayDtu>=0;
 mulClockResult_t result=mulClockObserveFitted(&s->network,&s->reg,&s->fit,&s->cfg,&p,rx,now,captureValid!=0,delayDtu>=0?&delay:NULL);
 return snprintf(out,capacity,"{\"sequence\":%u,\"median_ppb\":%ld,\"fitted_ppb\":%ld,\"kalman_ppb\":%ld,\"mapping_status\":%u,\"result\":%u,\"fit_ready\":%u,\"fit_applied\":%u,\"fallback\":%u,\"count\":%u,\"inliers\":%u,\"residual_rms_dtu\":%lu,\"fit_sigma_ppb\":%lu,\"rejected\":%lu,\"compensated\":%u}",
 sequence,(long)s->median.skewPpb,(long)s->fit.skewPpb,(long)s->reg.skewPpb,s->reg.status,result,s->fit.ready,s->fit.applied,s->fit.fallback,s->fit.count,s->fit.inliers,(unsigned long)s->fit.rmsDtu,(unsigned long)s->fit.uncertaintyPpb,(unsigned long)s->fit.rejected,s->fit.compensated);
}
