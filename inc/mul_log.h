/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_LOG_H
#define MUL_LOG_H
#include <stdbool.h>
#include <stdint.h>
enum {mulLogQuiet=0,mulLogNormal=1,mulLogDebug=2};
unsigned mulLogLevel(void);
void mulLogSetLevel(unsigned level);
const char *mulLogName(unsigned level);
bool mulLogParse(const char *name,uint8_t *level);
/* One bounded producer queue; all text formatting belongs to main. */
typedef struct {uint64_t t[8];uint32_t cycle;uint8_t local,why,event,initiator,responder;
 const char *reason;} mulLogRecord_t;
enum {logTxOk,logTxFail,logMiss,logKalman,logPostpone,logBacklog,logOverrun,
 logPrepFail,logTwrOk,logTwrFail,logDropped,logFiltered,logCount};
void mulLogCount(unsigned counter);
uint32_t mulLogCounter(unsigned counter);
void mulLogTimingCount(unsigned event,unsigned why);
void mulLogPush(const mulLogRecord_t *record);
void mulLogStatus(unsigned page,char *line,unsigned capacity);
void mulLogPoll(bool (*write)(const char *,unsigned));
#endif
