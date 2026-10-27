/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef MUL_GEOMETRY_H
#define MUL_GEOMETRY_H
#include "mul_pose.h"
void mulGeometryClear(void);
bool mulGeometryBegin(uint32_t clock,uint32_t epoch,uint32_t frame,uint16_t skewMs);
mulPoseStatus_t mulGeometryPut(const mulPose_t *pose);
void mulGeometryPair(const mulNetwork_t *network);
void mulGeometrySelect(mulDelaySource_t source);
mulDelaySource_t mulGeometrySource(void);
// Main/USB task only: floating-point geometry outside radio callbacks/locks.
void mulGeometryPoll(uint32_t nowMs);
mulPoseStatus_t mulGeometrySnapshot(uint32_t nowMs,mulPoseGeometry_t *geometry,unsigned *count);
// UWB owner: bounded integer/copy operations only; no I2C, parsing or sqrt.
bool mulGeometryDelay(const mulNetwork_t *network,const mulClock_t *clock,uint32_t nowMs,mulClockDelay_t *delay);
#endif
