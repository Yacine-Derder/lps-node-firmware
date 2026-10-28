/* Host only: expose production state for independent Python model comparison. */
#include "mul_clock_fit.h"
#include <stdlib.h>
#include <string.h>
typedef struct {mulClock_t clock;mulClockFit_t fit;mulClockFitConfig_t config;} Replay;
void *model_new(void){Replay *s=calloc(1,sizeof(*s));if(!s)return NULL;mulClockReset(&s->clock);mulClockFitDefaults(&s->config);s->config.estimator=2;return s;}
void model_free(void *p){free(p);}
void model_step(void *ptr,uint32_t seq,uint64_t tx,uint64_t rx,uint32_t now,double out[18]) {
 Replay *s=ptr;mulTestPacket_t p={.sequence=seq,.txTimestamp=tx};memcpy(p.source,mulDefaultNetwork.addresses[0],8);
 mulClockObserveFitted(NULL,&s->clock,&s->fit,&s->config,&p,rx,now,true,NULL);
 for(unsigned i=0;i<3;i++){out[i]=s->fit.state[i];for(unsigned j=0;j<3;j++)out[3+3*i+j]=s->fit.covariance[i][j];}
 out[12]=s->clock.skewPpb;out[13]=s->fit.applied;out[14]=s->fit.rejected;out[15]=s->clock.status;out[16]=s->fit.resets;out[17]=s->fit.uncertaintyPpb;
}
