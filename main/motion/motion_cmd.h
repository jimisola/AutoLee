#pragma once

#include <cstdint>

// ============================================================================
//  Deferred motion commands - the single safe way for any task other than
//  pump_task to affect motion.
//
//  The Arduino original was cooperative: web callbacks and UI callbacks all
//  ran from loop(), so calling startRunBetweenEndpoints() straight out of a
//  handler was safe. This port has real preemption - the HTTP server task,
//  the LVGL task and pump_task all run concurrently - so a direct call from a
//  handler races pump_task's handleMotion() over the stepper, the TMC5160 SPI
//  bus (shared with the display) and LVGL, none of which are thread-safe.
//
//  Karl's upstream v1.10.0 hit and fixed the same class of bug in the Arduino
//  build (see docs/upstream-v1.10.0-diff.md) - independent confirmation this
//  is real, and its deferred-flag list is effectively the checklist below.
//
//  Rule: handlers call request*() (cheap, non-blocking, any task); ONLY
//  pump_task calls processPendingCommands(), and only pump_task ever touches
//  the stepper/TMC/motion FSM. Requests are validity-checked at execution
//  time, not at request time, since state can change in between.
// ============================================================================
namespace motion_cmd {

// --- Producers: safe to call from any task -------------------------------
void requestToggleRun();            // RUN/STOP button (web + touch UI)
void requestStop();                 // stop only (used by the OTA upload path)
void requestCalibrate();            // begin sensorless calibration
void requestReturnHome();           // jam-screen "return home"
void requestBatchStart();           // start a batch run
void requestProfile(uint8_t idx);   // switch speed profile
void requestCurrentMa(int32_t ma);  // set motor run current (SPI write)
void requestUiRefresh();            // refresh LVGL labels from pump_task
void requestLogClear();             // clear the log ring
void requestResetSettings();        // discard stored calibration/tuning (IDLE only)

// Cancel an in-progress calibration or creep-home.
//
// The odd one out: every other request above is consumed by
// processPendingCommands(), which cannot run while a calibration or a home is
// in progress - those block pump_task inside motion.cpp for tens of seconds,
// which is exactly why they were uninterruptible. So this one is not a queued
// command but a flag the blocking search loops poll directly, and it is
// therefore the only request with a reader visible outside pump_task's command
// pump. It cannot start, stop or move anything; the worst a spurious set can do
// is end a search early, leaving the axis unreferenced and the press idle.
void requestAbort();

// --- Consumer: pump_task ONLY --------------------------------------------
// True while an abort is pending. Polled by motion.cpp's search/wait loops.
bool abortRequested();
// Consumes the flag. Called by motion.cpp once a search has finished unwinding,
// and at the start of every calibration/home so a stale request from a search
// that already ended cannot cancel the next one before it begins.
void clearAbort();

// Executes whatever was requested since the last call. Must not be called
// from any other task.
void processPendingCommands();

}  // namespace motion_cmd
