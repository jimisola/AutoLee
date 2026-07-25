#pragma once

#include <cstdint>

// ============================================================================
//  Persistent settings / calibration store (NVS)
//
//  Everything in MotionState (main/motion/motion_state.h) used to be RAM-only:
//  every reboot - including the watchdog-forced ones that really happened
//  during bring-up (see docs/PLAN.md, Phase 6) - threw away the endpoint
//  calibration, the per-profile SG trips, the run current, the work-zone width
//  and the piece counter, silently reverting the press to compiled-in defaults.
//
//  This module persists ONLY the calibration/tuning subset as one versioned,
//  fixed-layout blob. Runtime state (runState, currentTarget, batch*, the SG
//  telemetry counters) is deliberately NOT persisted: those must always come up
//  at their safe defaults after a reset, whatever caused it.
//
//  Failure policy is all-or-nothing and fails safe: a missing blob, a size or
//  version mismatch, or ANY field failing the same validation the live setters
//  apply makes load() discard the whole blob, keep MotionState's compiled-in
//  defaults and force `endpointsCalibrated = false`. A partially-trusted
//  calibration is worse than none - the machine must never believe it knows
//  where its endpoints are on the strength of data that failed a check.
// ============================================================================
namespace settings_store {

// Bump whenever the persisted field set changes. load() refuses to interpret a
// blob written by a different version (it does not guess); add an explicit
// migration here if one is ever wanted.
constexpr uint16_t kVersion = 1;

// Restore the persisted subset into g_motion, then recompute the effective
// endpoints. Call from app_main BEFORE motion_init() so the restored
// runCurrentMa is what motion_init() pushes to the TMC5160.
// Falls back to the compiled-in defaults (and forces endpointsCalibrated=false)
// on anything unexpected; logs via webLog() either way.
void load();

// Dirty-check + throttled write. pump_task ONLY (called from
// motion_cmd::processPendingCommands()): compares the persistable subset
// against the last-saved copy and writes at most once every few seconds, so
// nothing here spins the flash on every UI nudge.
void tick();

// Force an immediate write if anything changed (ignores the throttle).
// Safe to call from any task.
void saveNow();

// Deliberate "reset calibration": erase the persisted blob and restore every
// field this module persists (endpoints/offsets, per-profile sg_trip,
// runCurrentMa, sgWorkZoneSteps, activeProfile, counter) to MotionState's
// compiled-in defaults, forcing `endpointsCalibrated = false` and latching
// `positionReferenceStale`. Finishes with recomputeEffectiveEndpoints().
//
// Narrowly scoped: it erases ONLY this module's key, so the WiFi credentials,
// the AP key and the web password - which share the `autolee` NVS namespace
// under their own keys (wifi_mgr.cpp, web_server.cpp) - are untouched and a
// reset can never lock the device off the network.
//
// pump_task ONLY (via motion_cmd::requestResetSettings()): it rewrites motion
// state that pump_task owns, and its caller re-programs the TMC5160 over the
// SPI bus shared with the display. Never call it from an HTTP or LVGL handler.
void resetToDefaults();

}  // namespace settings_store
