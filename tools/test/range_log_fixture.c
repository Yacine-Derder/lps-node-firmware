/* Native fixture: use the production formatter and bounded queue. */
#include "mul_log.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
TickType_t xTaskGetTickCount(void){return 0;}
void vPortEnterCritical(void){}
void vPortExitCritical(void){}
static bool writeLine(const char *line,unsigned n){return n<=128&&fwrite(line,1,n,stdout)==n;}
int main(int argc,char **argv) {
 if(argc!=8)return 2;
 unsigned i=(unsigned)strtoul(argv[1],NULL,10),j=(unsigned)strtoul(argv[2],NULL,10);
 unsigned tof=(unsigned)strtoul(argv[3],NULL,10),cycle=(unsigned)strtoul(argv[4],NULL,10);
 mulLogSetLevel(mulLogNormal);
 mulLogRecord_t r={.event=10,.cycle=cycle,.initiator=i,.responder=j,.local=i,
   .t={tof,tof,UINT64_C(0x123456789a),UINT64_C(0xfedcba9876543210),UINT32_C(0xec2b4b2a),i==132?1:4}};
 r.t[2]=(r.t[2]+UINT64_C(6389760000)*cycle)&UINT64_C(0xffffffffff);
 if(strtoul(argv[5],NULL,10)){r.event=11;r.reason="late_setup";}
 if(strtoul(argv[6],NULL,10))r.local=j;
 r.why=(uint8_t)strtoul(argv[7],NULL,10);
 mulLogPush(&r);for(unsigned k=0;k<8;k++)mulLogPoll(writeLine);
 return ferror(stdout)?1:0;
}
