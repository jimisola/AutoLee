#pragma once

// Web server (native ESP-IDF via PsychicHttp), ported from src/web_server.cpp.
// See docs/PLAN.md Phase 5.
void setupWebServer();
void handleWebCalibration();  // pump: services webCalRequested (call every loop iteration)
void handleWebHome();         // pump: services webHomeRequested
void broadcastState();        // pump: SSE state + log push, rate-limited to SSE_INTERVAL_MS
