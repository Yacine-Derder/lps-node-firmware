/* Host-only preview; production codec and planner supply every reservation. */
#include "mul_ranging_plan.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
int main(int argc,char **argv) {
 if(argc!=3)return 2;
 char *end;unsigned long long first=strtoull(argv[1],&end,10);if(*end||first>UINT32_MAX)return 2;
 unsigned long count=strtoul(argv[2],&end,10);if(*end||!count||count>100)return 2;
 uint8_t bytes[MUL_PLAN_BYTES];if(fread(bytes,1,sizeof(bytes),stdin)!=sizeof(bytes)||fgetc(stdin)!=EOF)return 2;
 mulRangingPlan_t p;const char *decode=mulRangingPlanDecodeError(bytes,&p);if(decode){fprintf(stderr,"%s\n",decode);return 1;}
 mulRangingBudget_t b;const char *error=mulRangingPlanValidate(&p,&b);if(error){fprintf(stderr,"%s\n",error);return 1;}
 printf("{\"version\":2,\"planner_version\":%u,\"radio_integration\":{\"lps_aa\":true,\"rlr_receive\":true,\"alr_rlr\":false},\"fingerprint\":\"%08lX\",\"reservation_us\":%lu,\"data_end_us\":%lu,\"capacity\":%u,\"peak_operations\":%u,\"aa_pairs\":%u,\"ar_pairs\":%u,\"aa_pair_millihz\":%u,\"ar_pair_millihz\":%u,\"max_utilization_percent\":%u,\"requested_millihz\":%lu,\"cycles\":[",
  MUL_PLAN_VERSION,(unsigned long)mulRangingPlanFingerprint(&p),(unsigned long)b.reservationUs,(unsigned long)b.dataEndUs,b.capacity,b.peakOperations,b.aaPairs,b.arPairs,b.aaPairs?p.aaMilliHz:0,b.arPairs?p.arMilliHz:0,p.maxUtilization,(unsigned long)b.requestedMilliHz);
 for(unsigned at=0;at<count;at++) {
  uint32_t cycle=(uint32_t)(first+at);unsigned n=mulRangingPlanCount(&p,cycle);
  printf("%s{\"cycle\":%lu,\"reserved_us\":%lu,\"utilization_permille\":%lu,\"ranging_reserved_us\":%lu,\"operations\":[",at?",":"",(unsigned long)cycle,(unsigned long)(b.dataEndUs+n*b.reservationUs),(unsigned long)((b.dataEndUs+n*b.reservationUs)/p.broadcast.periodMs),(unsigned long)(n*b.reservationUs));
  for(unsigned i=0;i<n;i++) {
   mulRangingOperation_t o;if(!mulRangingPlanOperation(&p,cycle,i,&o))return 1;
   printf("%s{\"kind\":\"%s\",\"initiator\":%u,\"responder\":%u,\"start_us\":%lu,\"poll_us\":%lu,\"end_us\":%lu}",i?",":"",o.kind==MUL_PLAN_AA?"alr_alr":"alr_rlr",o.initiator,o.responder,(unsigned long)o.startUs,(unsigned long)o.pollUs,(unsigned long)o.endUs);
  }
  printf("]}");
 }
 puts("]}");return 0;
}
