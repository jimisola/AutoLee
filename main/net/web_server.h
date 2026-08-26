#pragma once

// Web server (native ESP-IDF via PsychicHttp), ported from src/web_server.cpp.
// See docs/PLAN.md Phase 5.
void setupWebServer();
void broadcastState();   // sse_task (app_main.cpp): SSE state + log push, rate-limited to
                         // SSE_INTERVAL_MS. Deliberately NOT called from pump_task - its
                         // blocking socket send() must never share a task/watchdog with
                         // motion-critical polling (see app_main.cpp's sse_task comment).
void otaWatchdogTick();  // sse_task: releases the OTA in-progress flag if an upload died
                         // without a final chunk (vanished client). See web_server.cpp.

// Touch-UI recovery for a forgotten/unknown web password: restore the factory
// default, which re-arms the force-change gate so the next web caller must set a
// real one before anything state-changing works. Physical presence at the LCD is
// the gate - see the comment on webPasswordResetTick() in web_server.cpp.
//
// Request from any task (the LVGL handler calls it); the work happens on
// sse_task, so the digest credential is only ever mutated from one task.
void requestWebPasswordReset();
void webPasswordResetTick();
