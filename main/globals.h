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

// Web request flags (set from the HTTP server's task, serviced from the main
// loop) - mirrors the original's "async callbacks must not touch motion"
// rule (see CLAUDE.md).
extern volatile bool webCalRequested;
extern volatile bool webHomeRequested;
extern volatile bool rebootRequested;
extern uint32_t rebootRequestMs;

// Log ring for the web UI's log panel + SSE "log" events.
#include "log_ring.h"
extern autolee::LogRing<LOG_LINES, LOG_LINE_LEN> g_log;
extern uint32_t logSentSerial; // last serial# broadcast over SSE
void webLog(const char *fmt, ...);
