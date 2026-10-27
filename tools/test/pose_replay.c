/* Host-only deterministic replay; production pose decode/stream/geometry. */
#include "mul_pose.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
 if(argc!=4)return 2;
 unsigned reference=(unsigned)strtoul(argv[1],NULL,10),follower=(unsigned)strtoul(argv[2],NULL,10),skew=(unsigned)strtoul(argv[3],NULL,10);
 if(reference>254||follower>254||reference==follower||skew>1000)return 2;
 mulPoseStream_t stream={0};uint8_t bytes[84];
 for(;;) {
  size_t n=fread(bytes,1,sizeof(bytes),stdin);if(!n)break;if(n!=sizeof(bytes))return 3;
  uint32_t now=(uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|(uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
  mulPose_t p;if(!mulPoseDecode(bytes+4,now,&p))return 4;
  if(!stream.active&&!mulPoseStreamBegin(&stream,p.clock,p.epoch,p.frame,(uint16_t)skew))return 5;
  (void)mulPoseStreamExpire(&stream,now);
  mulPoseStatus_t ingest=mulPoseStreamPut(&stream,&p);mulPoseGeometry_t g;
  mulPoseStatus_t status=mulPoseGeometry(mulPoseStreamGet(&stream,(uint8_t)reference),mulPoseStreamGet(&stream,(uint8_t)follower),now,(uint16_t)skew,&g);
  printf("{\"type\":\"geometry\",\"node\":%u,\"input_sequence\":%lu,\"input_source_us\":%llu,\"source_clock\":%lu,\"source_epoch\":%lu,\"frame\":%lu,\"reference_node\":%u,\"follower_node\":%u,\"received_ms\":%lu,\"ingest_status\":%u,\"geometry_status\":%u,\"physical_uncertainty\":\"UNKNOWN\",\"calibrated\":false,\"dtu_nominal_hz\":63897600000,\"distance_mm\":",p.node,(unsigned long)p.sequence,(unsigned long long)p.sourceUs,(unsigned long)p.clock,(unsigned long)p.epoch,(unsigned long)p.frame,reference,follower,(unsigned long)now,ingest,status);
  if(status==mulPoseAvailable) {
   printf("%.6f,\"delay_dtu\":%ld,\"age_ms\":%lu,\"measured_ms\":%lu,\"reference_pose_source_us\":%llu,\"follower_pose_source_us\":%llu,\"variance_mm2\":",g.distanceMm,(long)g.delayDtu,(unsigned long)g.ageMs,(unsigned long)g.measuredMs,(unsigned long long)g.sourceUs[0],(unsigned long long)g.sourceUs[1]);
   if(g.covarianceKnown)printf("%.6f",g.varianceMm2);else printf("null");
  }else printf("null,\"delay_dtu\":null,\"age_ms\":null,\"variance_mm2\":null");
  puts("}");
 }
 return ferror(stdin)?6:0;
}
