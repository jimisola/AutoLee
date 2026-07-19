#pragma once

// Web server (native ESP-IDF via PsychicHttp), ported from src/web_server.cpp.
// See docs/PLAN.md Phase 5.
void setupWebServer();
void handleWebCalibration();  // pump: services webCalRequested (call every loop iteration)
void handleWebHome();         // pump: services webHomeRequested
void broadcastState();        // sse_task (app_main.cpp): SSE state + log push, rate-limited to
                              // SSE_INTERVAL_MS. Deliberately NOT called from pump_task - its
                              // blocking socket send() must never share a task/watchdog with
                              // motion-critical polling (see app_main.cpp's sse_task comment).
