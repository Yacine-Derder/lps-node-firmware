/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_log.h"
#include "unity.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
static TickType_t tick;
static char output[8192];static bool writable;
TickType_t xTaskGetTickCount(void){return tick;}
void vPortEnterCritical(void){}
void vPortExitCritical(void){}
static bool sink(const char *s,unsigned n){TEST_ASSERT_TRUE(n<=128);if(!writable)return false;TEST_ASSERT_TRUE(strlen(output)+n<sizeof(output));strncat(output,s,n);return true;}
static void drain(void){for(unsigned i=0;i<40;i++)mulLogPoll(sink);}
void setUp(void){tick=0;writable=true;mulLogSetLevel(mulLogDebug);drain();output[0]=0;}
void tearDown(void){}
void test_quietAndNormalDoNotProduceTimingRecordsButKeepCountersAndErrors(void){
 for(unsigned level=0;level<2;level++){
  mulLogSetLevel(level);uint32_t before=mulLogCounter(logKalman);
  mulLogTimingCount(3,0);mulLogRecord_t r={.event=3,.cycle=123};mulLogPush(&r);
  TEST_ASSERT_EQUAL(before+1,mulLogCounter(logKalman));drain();TEST_ASSERT_EQUAL_STRING("",output);
  r.event=8;r.t[0]=120;r.t[1]=103;r.initiator=132;r.responder=141;mulLogPush(&r);drain();
  TEST_ASSERT_EQUAL(level!=0,strstr(output,"result=OK")!=NULL);output[0]=0;
  r.event=9;r.reason="late_setup";mulLogPush(&r);drain();TEST_ASSERT_NOT_NULL(strstr(output,"late_setup"));output[0]=0;
 }
}
void test_backpressureOverflowAndLevelChangePreserveLossAccounting(void){
 mulLogRecord_t r={.event=3,.cycle=42};mulLogPush(&r);writable=false;mulLogPoll(sink);
 TEST_ASSERT_EQUAL_STRING("",output);uint32_t before=mulLogCounter(logDropped);
 for(unsigned i=0;i<7;i++){r.cycle=50+i;mulLogPush(&r);}TEST_ASSERT_EQUAL(before+3,mulLogCounter(logDropped));
 writable=true;mulLogPoll(sink);TEST_ASSERT_NOT_NULL(strstr(output,"c=42"));
 uint32_t filtered=mulLogCounter(logFiltered);mulLogSetLevel(mulLogQuiet);output[0]=0;drain();
 TEST_ASSERT_EQUAL_STRING("",output);TEST_ASSERT_EQUAL(filtered+5,mulLogCounter(logFiltered));
 TEST_ASSERT_EQUAL(before+3,mulLogCounter(logDropped));
 char line[48];mulLogStatus(logDropped,line,sizeof(line));TEST_ASSERT_NOT_NULL(strstr(line,"LOG dropped="));
}
void test_normalSummariesAndMaximumTwrRecordFitAtomicUsbWrites(void){
 mulLogSetLevel(mulLogNormal);tick=5000/portTICK_PERIOD_MS;drain();TEST_ASSERT_NOT_NULL(strstr(output,"LOG tx_ok="));TEST_ASSERT_NOT_NULL(strstr(output,"LOG filtered="));
 output[0]=0;mulLogRecord_t r={.event=8,.cycle=UINT32_MAX,.t={65535,65535},.local=1,.initiator=254,.responder=253};
 mulLogPush(&r);drain();TEST_ASSERT_EQUAL(127,strlen(output));TEST_ASSERT_NOT_NULL(strstr(output,"distance_m=307.475"));
}
