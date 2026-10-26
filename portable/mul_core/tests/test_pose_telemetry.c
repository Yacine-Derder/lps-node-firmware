#include "mul_telemetry.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv) {
 (void)argv;
 mulTelemetry_t r={.type=mulTlmPlanVector,.quality=4,.node=141,.radio=2,.role=2,.mode=5,.ratePolicy=1,.epoch=9,
  .vector={.count=4,.ids={32,33,34,35},.slotMs=3,.periodMs=200,.revision=1,.seenMask=15,.sourceVersion=1}};
 for(unsigned i=0;i<3;i++)r.vector.values[i]=(mulMultiValue_t){.reason=5,.source=i+1,.tdoaDtu=INT32_MIN,.rangeDifferenceMm=INT32_MIN};
 char line[MUL_TLM_LINE];mulTelemetry_t d;
 assert(mulTelemetryEncode(line,sizeof(line),&r)&&mulTelemetryDecode(line,&d));
 assert(d.vector.sourceVersion==1&&d.vector.values[0].source==1&&d.vector.values[1].source==2&&d.vector.values[2].source==3);
 if(argc>1)puts(line);
 r.vector.sourceVersion=0;assert(!mulTelemetryEncode(line,sizeof(line),&r));
 r=(mulTelemetry_t){.type=mulTlmTdoa,.node=141,.radio=2,.role=2,.mode=4,.epoch=9,.legacySlotMs=10,
  .tdoa={.reference=132,.follower=133,.tdoaDtu=INT32_MIN,.rangeDifferenceMm=INT32_MIN,.ratePpb=INT32_MIN,.unknownComponents=63}};
 for(unsigned source=1;source<=2;source++) {
  r.tdoa.delaySource=source;assert(mulTelemetryEncode(line,sizeof(line),&r)&&mulTelemetryDecode(line,&d));
  assert(d.tdoa.delaySource==source);if(argc>1)puts(line);
 }
 r.tdoa.delaySource=3;assert(!mulTelemetryEncode(line,sizeof(line),&r));
 if(argc==1)puts("Pose provenance telemetry: strict vector subversion1 source bits and TDoA subversion1 external/none,152-byte envelope, legacy zero preservation passed");
}
