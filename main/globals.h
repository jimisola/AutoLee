#pragma once

#include <cstdint>
#include "config.h"

// Mutable, cross-module state - defined once in globals.cpp. Mirrors
// src/main.cpp's role in the old Arduino TU-split (see CLAUDE.md history);
// config.h's `extern` declarations point here now instead.

enum RunState : uint8_t { IDLE, RUNNING, STOPPING, CALIBRATING, STALLED, HOMING };
extern volatile RunState runState;
extern long currentTarget;
extern uint32_t stopEntryMs;

extern long rawUp, rawDown;
extern long endpointUp, endpointDown;
extern bool endpointsCalibrated;
extern long counter;

extern uint32_t lastDirectionChangeMs;
extern uint8_t runSGHighCount;
extern uint8_t runSGLowCount;

extern bool batchActive;
extern int32_t batchCount;
extern int32_t batchTarget;
