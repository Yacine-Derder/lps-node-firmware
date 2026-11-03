/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_log.h"
#include <string.h>
static volatile uint8_t level=mulLogNormal;
static const char *const names[]={"quiet","normal","debug"};
unsigned mulLogLevel(void){return level;}
void mulLogSetLevel(unsigned value){if(value<=mulLogDebug)level=value;}
const char *mulLogName(unsigned value){return value<=mulLogDebug?names[value]:"unknown";}
bool mulLogParse(const char *name,uint8_t *value){
 for(unsigned i=0;i<3;i++)if(!strcmp(name,names[i])){*value=i;return true;}
 return false;
}

#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <inttypes.h>
static mulLogRecord_t ring[4],pending;
static uint8_t head,tail,count,part;
static bool ready;
static uint32_t counters[logCount];
void mulLogCount(unsigned c){if(c<logCount){taskENTER_CRITICAL();counters[c]++;taskEXIT_CRITICAL();}}
uint32_t mulLogCounter(unsigned c){return c<logCount?counters[c]:0;}
void mulLogTimingCount(unsigned event,unsigned why){
 if(event==2)mulLogCount(why?logTxFail:logTxOk);
 if(event==3&&why<=2)mulLogCount(logKalman+why);
 if((event==5||event==6)&&why)mulLogCount(logPrepFail);
}
static bool wanted(const mulLogRecord_t *r){return r->event==9||level>=(r->event==8?mulLogNormal:mulLogDebug);}
void mulLogPush(const mulLogRecord_t *r){
 if(!wanted(r))return;
 taskENTER_CRITICAL();
 if(count==4)counters[logDropped]++;
 else {ring[head]=*r;head=(head+1)%4;count++;}
 taskEXIT_CRITICAL();
}
void mulLogStatus(unsigned page,char *line,unsigned capacity){
 static const char *const names[]={"tx_ok","tx_fail","slot_miss","kalman","postpone","backlog","overrun","prep_fail","twr_ok","twr_fail","dropped","filtered"};
 if(page<logCount)snprintf(line,capacity,"LOG %s=%" PRIu32,names[page],mulLogCounter(page));
}
void mulLogPoll(bool (*write)(const char *,unsigned)){
 static TickType_t last;static uint8_t summary;
 char line[128];int n;
 if(!write)return;
 if(!ready){taskENTER_CRITICAL();
  if(count){pending=ring[tail];tail=(tail+1)%4;count--;ready=true;}
  taskEXIT_CRITICAL();
 }
 if(ready&&!wanted(&pending)){ready=false;part=0;mulLogCount(logFiltered);return;}
 if(!ready){
  TickType_t now=xTaskGetTickCount();
  if(level==mulLogQuiet||(TickType_t)(now-last)<5000/portTICK_PERIOD_MS)return;
  mulLogStatus(summary,line,sizeof(line));n=strlen(line);line[n++]='\r';line[n++]='\n';
  if(write(line,n)&&++summary==logCount){summary=0;last=now;}
  return;
 }
 mulLogRecord_t *r=&pending;
 if(r->event==9)n=snprintf(line,sizeof(line),"MUL TWR: id=%" PRIu32 " result=FAILED reason=%s\r\n",r->cycle,r->reason);
 else if(r->event==8){
  uint32_t mm=(r->t[0]*UINT64_C(299792458000)+UINT64_C(31948800000))/UINT64_C(63897600000);
  uint32_t raw=(r->t[1]*UINT64_C(299792458000)+UINT64_C(31948800000))/UINT64_C(63897600000);
  n=snprintf(line,sizeof(line),"MUL TWR: id=%" PRIu32 " role=%s ref=0x%02X fol=0x%02X result=OK tof_dtu=%" PRIu32 " distance_m=%" PRIu32 ".%03" PRIu32 " power=%s raw_mm=%" PRIu32 "\r\n",
   r->cycle,r->local?"RESP":"INIT",r->initiator,r->responder,(uint32_t)r->t[0],mm/1000,mm%1000,r->why?"APPLIED":"UNAVAILABLE",raw);
 }else{
  uint64_t a=r->t[2*part],b=r->t[2*part+1];
  n=snprintf(line,sizeof(line),"SL1 c=%" PRIu32 " l=%02X e=%u i=%u j=%u k=%u a=%02X%08" PRIX32 " b=%02X%08" PRIX32 " w=%u z=%" PRIu32 "\r\n",
   r->cycle,r->local,r->event,r->initiator,r->responder,part,(unsigned)(a>>32),(uint32_t)a,(unsigned)(b>>32),(uint32_t)b,r->why,mulLogCounter(logDropped));
 }
 if(n>0&&(unsigned)n<sizeof(line)&&write(line,n)){
  if(r->event>=8||++part==4){ready=false;part=0;}
 }
}
