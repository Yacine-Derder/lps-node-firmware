/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_geometry.h"
#include "FreeRTOS.h"
#include "task.h"
static mulPoseStream_t stream;
static mulPoseGeometry_t cached;
static uint32_t generation,cachedGeneration;
static mulPoseStatus_t cachedStatus=mulPoseMissing;
static uint8_t reference=255,follower=255;
static mulDelaySource_t selected=mulDelayTwr;
void mulGeometryClear(void) {
 taskENTER_CRITICAL();stream.active=false;stream.count=0;generation++;taskEXIT_CRITICAL();
}
bool mulGeometryBegin(uint32_t clock,uint32_t epoch,uint32_t frame,uint16_t skewMs) {
 if(!clock||!frame||skewMs>1000)return false;
 taskENTER_CRITICAL();
 (void)mulPoseStreamBegin(&stream,clock,epoch,frame,skewMs);generation++;
 taskEXIT_CRITICAL();return true;
}
mulPoseStatus_t mulGeometryPut(const mulPose_t *pose) {
 if(!mulPoseValid(pose))return mulPoseInvalid;
 taskENTER_CRITICAL();mulPoseStatus_t status=mulPoseStreamCommit(&stream,pose);
 if(status==mulPoseAvailable)generation++;
 taskEXIT_CRITICAL();return status;
}
void mulGeometryPair(const mulNetwork_t *network) {
 taskENTER_CRITICAL();
 reference=network?network->addresses[0][0]:255;follower=network?network->addresses[1][0]:255;generation++;
 taskEXIT_CRITICAL();
}
void mulGeometrySelect(mulDelaySource_t source) {taskENTER_CRITICAL();selected=source<=mulDelayNone?source:mulDelayNone;taskEXIT_CRITICAL();}
mulDelaySource_t mulGeometrySource(void) {taskENTER_CRITICAL();mulDelaySource_t source=selected;taskEXIT_CRITICAL();return source;}
void mulGeometryPoll(uint32_t nowMs) {
 mulPose_t a,b;bool haveA=false,haveB=false;uint16_t skew;
 taskENTER_CRITICAL();if(mulPoseStreamExpire(&stream,nowMs))generation++;
 uint32_t version=generation;
 if(cachedGeneration==version){taskEXIT_CRITICAL();return;}
 const mulPose_t *p=mulPoseStreamGet(&stream,reference);if(p){a=*p;haveA=true;}
 p=mulPoseStreamGet(&stream,follower);if(p){b=*p;haveB=true;}
 skew=stream.maxSkewMs;taskEXIT_CRITICAL();
 mulPoseGeometry_t g={0};mulPoseStatus_t status=mulPoseGeometry(haveA?&a:NULL,haveB?&b:NULL,nowMs,skew,&g);
 taskENTER_CRITICAL();if(version==generation){cached=g;cachedStatus=status;cachedGeneration=version;}taskEXIT_CRITICAL();
}
mulPoseStatus_t mulGeometrySnapshot(uint32_t nowMs,mulPoseGeometry_t *geometry,unsigned *count) {
 taskENTER_CRITICAL();mulPoseStatus_t status=cachedGeneration==generation?cachedStatus:mulPoseMissing;
 mulPoseGeometry_t g=cached;if(count)*count=stream.count;
 if(status==mulPoseAvailable&&(uint32_t)(nowMs-g.measuredMs)>g.ttlMs){status=mulPoseStale;cachedStatus=mulPoseStale;}
 taskEXIT_CRITICAL();
 if(geometry&&status==mulPoseAvailable)*geometry=g;
 return status;
}
bool mulGeometryDelay(const mulNetwork_t *network,const mulClock_t *clock,uint32_t nowMs,mulClockDelay_t *delay) {
 mulPoseGeometry_t g;
 return mulGeometrySource()==mulDelayExternal&&mulGeometrySnapshot(nowMs,&g,NULL)==mulPoseAvailable&&mulPoseDelay(network,clock,&g,delay);
}
