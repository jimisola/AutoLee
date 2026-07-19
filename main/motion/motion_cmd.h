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

// --- Consumer: pump_task ONLY --------------------------------------------
// Executes whatever was requested since the last call. Must not be called
// from any other task.
void processPendingCommands();

}  // namespace motion_cmd
