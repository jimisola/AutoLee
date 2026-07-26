#include "settings_store.h"

#include <cstring>

#include "esp_timer.h"
#include "nvs.h"

#include "config.h"
#include "globals.h"       // webLog()
#include "motion.h"        // recomputeEffectiveEndpoints()
#include "motion_state.h"  // g_motion, motion_state::Guard/snapshot()

namespace settings_store {
namespace {

// NOTE: this namespace is SHARED with wifi_mgr.cpp ("ssid"/"pass"/"apkey") and
// web_server.cpp ("webpass"). Only the key below belongs to this module, so
// every destructive operation here must be key-scoped (nvs_erase_key), never
// namespace-scoped (nvs_erase_all) - see resetToDefaults(). The two stores have
// independent lifetimes: clearing WiFi must not touch the calibration, and
// resetting the calibration must not lock the device off the network.
constexpr char kNamespace[] = "autolee";
constexpr char kKey[] = "settings";

// Flash-wear budget: at most one write per this interval, and only when
// something in the persistable set actually changed. A UI knob held down
// therefore costs one write, not one per step.
constexpr uint32_t kSaveIntervalMs = 5000;

// The `Persisted` layout, every older PersistedV* snapshot, validate() and the
// version dispatch live in settings_blob.h - pure, ESP-IDF-free and covered by
// host_test/test_settings_blob. This file owns the NVS I/O and the MotionState
// half only.

Persisted s_lastSaved{};
uint32_t s_lastCheckMs = 0;

inline uint32_t now_ms() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// Snapshot-based on purpose: correct from any task (see motion_state.h), and
// the copy is a handful of words.
Persisted capture() {
  const MotionState ms = motion_state::snapshot();
  Persisted p{};  // zero-init so no byte of the blob is ever indeterminate
  p.version = kVersion;
  p.runCurrentMa = ms.runCurrentMa;
  p.rawUp = (int32_t)ms.rawUp;
  p.rawDown = (int32_t)ms.rawDown;
  p.upOffsetSteps = ms.upOffsetSteps;
  p.downOffsetSteps = ms.downOffsetSteps;
  p.sgWorkZoneSteps = ms.sgWorkZoneSteps;
  p.counter = (int32_t)ms.counter;
  for (uint8_t i = 0; i < NUM_PROFILES; i++) p.sgTrip[i] = ms.profiles[i].sg_trip;
  p.activeProfile = ms.activeProfile;
  p.endpointsCalibrated = ms.endpointsCalibrated ? 1 : 0;
  return p;
}

void apply(const Persisted &p) {
  motion_state::Guard g;
  g_motion.runCurrentMa = p.runCurrentMa;
  g_motion.rawUp = p.rawUp;
  g_motion.rawDown = p.rawDown;
  g_motion.upOffsetSteps = p.upOffsetSteps;
  g_motion.downOffsetSteps = p.downOffsetSteps;
  g_motion.sgWorkZoneSteps = p.sgWorkZoneSteps;
  g_motion.counter = p.counter;
  for (uint8_t i = 0; i < NUM_PROFILES; i++) g_motion.profiles[i].sg_trip = p.sgTrip[i];
  g_motion.activeProfile = p.activeProfile;
  g_motion.endpointsCalibrated = p.endpointsCalibrated != 0;
  // Restoring endpoints can never restore the *position reference* they are
  // measured against: the stepper counter starts at 0 while the carriage sits
  // wherever power was lost. Latch it stale so startRunBetweenEndpoints()
  // refuses until a Return Home (or a fresh calibration) re-zeroes at the UP
  // hard stop. Same guard as the fields above so a snapshotting reader never
  // sees "calibrated" without the matching staleness.
  if (g_motion.endpointsCalibrated) g_motion.positionReferenceStale = true;
}

// Restore exactly the fields capture()/apply() cover to MotionState's
// compiled-in initializers, plus the derived "not calibrated" half. The
// defaults come from a default-constructed MotionState rather than from
// literals repeated here, so motion_state.h stays the single source of truth
// and adding a persisted field cannot leave this list quietly behind.
//
// Shared by the rejected-load path (fallbackToDefaults(), where the assignments
// are a no-op because nothing has touched g_motion yet at boot - they just make
// the guarantee explicit instead of merely inherited) and by resetToDefaults(),
// where they are the whole point: at runtime the live values ARE dirty.
//
// Callers must not already hold the guard, and must call
// recomputeEffectiveEndpoints() afterwards if they need the derived endpoints
// re-derived (this function only zeroes them).
void applyCompiledDefaults() {
  const MotionState def{};  // constructed outside the critical section
  motion_state::Guard g;
  g_motion.runCurrentMa = def.runCurrentMa;
  g_motion.rawUp = def.rawUp;
  g_motion.rawDown = def.rawDown;
  g_motion.upOffsetSteps = def.upOffsetSteps;
  g_motion.downOffsetSteps = def.downOffsetSteps;
  g_motion.sgWorkZoneSteps = def.sgWorkZoneSteps;
  g_motion.counter = def.counter;
  for (uint8_t i = 0; i < NUM_PROFILES; i++) g_motion.profiles[i].sg_trip = def.profiles[i].sg_trip;
  g_motion.activeProfile = def.activeProfile;
  // Whatever else happened, the machine must not think it knows its endpoints.
  g_motion.endpointsCalibrated = false;
  g_motion.endpointUp = 0;
  g_motion.endpointDown = 0;
  // Moot while endpointsCalibrated is false (a run is refused on that alone),
  // but set anyway: the position reference is unknown whatever else we decided.
  g_motion.positionReferenceStale = true;
}

void fallbackToDefaults(const char *why) {
  applyCompiledDefaults();
  webLog("Settings: %s - using defaults, calibration cleared", why);
}

bool writeBlob(const Persisted &p) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    webLog("Settings: NVS open failed (%d)", (int)err);
    return false;
  }
  err = nvs_set_blob(h, kKey, &p, sizeof(p));
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) {
    webLog("Settings: NVS write failed (%d)", (int)err);
    return false;
  }
  return true;
}

// Key-scoped on purpose (see kNamespace): nvs_erase_all() here would also wipe
// the WiFi credentials, the AP key and the web password, which live in the same
// namespace under their own keys.
bool eraseBlob() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    webLog("Settings: NVS open failed (%d)", (int)err);
    return false;
  }
  err = nvs_erase_key(h, kKey);
  if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;  // nothing stored: already reset
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) {
    webLog("Settings: NVS erase failed (%d)", (int)err);
    return false;
  }
  return true;
}

}  // namespace

void load() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) {
    fallbackToDefaults("no stored settings");
    s_lastSaved = capture();
    s_lastCheckMs = now_ms();
    return;
  }

  // Read into a raw byte buffer sized for the LARGEST known version, not
  // straight into `Persisted`: an older blob may be a different length, and it
  // is parseAndMigrate() - not nvs_get_blob() - that decides what the bytes
  // mean. A stored blob longer than any version we know returns
  // ESP_ERR_NVS_INVALID_LENGTH here, which is a size mismatch by another name.
  uint8_t raw[kMaxBlobBytes];
  size_t len = sizeof(raw);
  const esp_err_t err = nvs_get_blob(h, kKey, raw, &len);
  nvs_close(h);

  Persisted p{};
  uint16_t foundVersion = 0;
  const char *why = nullptr;
  bool migrated = false;

  if (err != ESP_OK) {
    why = (err == ESP_ERR_NVS_NOT_FOUND) ? "no stored settings" : "stored settings unreadable";
  } else {
    switch (parseAndMigrate(raw, len, p, &foundVersion)) {
      case ParseResult::Ok:
        migrated = (foundVersion != kVersion);
        break;
      case ParseResult::TooShort:
      case ParseResult::SizeMismatch:
        why = "stored settings size mismatch";
        break;
      case ParseResult::UnknownVersion:
        why = "stored settings version mismatch";
        break;
      case ParseResult::Invalid:
        why = "stored settings failed validation";
        break;
    }
  }

  if (why) {
    fallbackToDefaults(why);
  } else {
    apply(p);
    if (migrated) {
      webLog("Settings: migrated from v%u to v%u", (unsigned)foundVersion, (unsigned)kVersion);
    }
    webLog("Settings: restored (cal=%s up=%ld dn=%ld cur=%umA prof=%u cnt=%ld)",
           p.endpointsCalibrated ? "yes" : "no", (long)p.rawUp, (long)p.rawDown,
           (unsigned)p.runCurrentMa, (unsigned)p.activeProfile, (long)p.counter);
    if (p.endpointsCalibrated) {
      // The stepper's position counter starts at 0 on every boot while the
      // carriage is physically wherever the last power cut left it. 0 is the UP
      // endpoint (where a completed run, a stop and a home all park it), so it
      // is right in the normal case - but after a mid-stroke power loss or
      // watchdog reset it is not, and the restored endpoints are then offset
      // from reality. apply() therefore latched g_motion.positionReferenceStale,
      // which startRunBetweenEndpoints() enforces: a Return Home (or a fresh
      // calibration) must re-reference the axis before a run is allowed.
      webLog("Settings: position reference unknown after reboot - return home before running");
    }
  }

  // Effective endpoints are derived state, never persisted; recompute from what
  // we just restored (this also zeroes them when calibration is absent).
  recomputeEffectiveEndpoints();

  s_lastSaved = capture();
  s_lastCheckMs = now_ms();

  // Rewrite the blob in the current format straight away after a migration, so
  // the old-format bytes stop being what a subsequent boot has to interpret and
  // the upgrade cannot be lost to a power cut before the next settings change.
  // Only after s_lastSaved is baselined, so a failed write simply leaves the old
  // blob in place and the next tick() retries. Boot-time and a few ms, well
  // inside the task-WDT budget (load() runs before motion_init()).
  if (migrated && writeBlob(s_lastSaved)) {
    webLog("Settings: rewritten in v%u format", (unsigned)kVersion);
  }
}

void tick() {
  const uint32_t now = now_ms();
  if ((uint32_t)(now - s_lastCheckMs) < kSaveIntervalMs) return;
  s_lastCheckMs = now;  // throttle the retry too, on failure as well as success

  const Persisted cur = capture();
  if (memcmp(&cur, &s_lastSaved, sizeof(cur)) == 0) return;  // nothing changed
  if (writeBlob(cur)) s_lastSaved = cur;
}

void saveNow() {
  const Persisted cur = capture();
  s_lastCheckMs = now_ms();
  if (memcmp(&cur, &s_lastSaved, sizeof(cur)) == 0) return;
  if (writeBlob(cur)) s_lastSaved = cur;
}

void resetToDefaults() {
  applyCompiledDefaults();
  // Derived state, exactly as at the end of load(): zeroes the effective
  // endpoints now that endpointsCalibrated is false.
  recomputeEffectiveEndpoints();

  const Persisted def = capture();
  s_lastCheckMs = now_ms();  // restart the throttle window either way

  if (eraseBlob()) {
    // Flash holds nothing and RAM holds exactly the compiled-in defaults, so
    // there is nothing left to write. Re-baselining the dirty check to those
    // defaults is what stops tick() from immediately re-creating the blob two
    // lines later and undoing half the reset: the next boot then takes the same
    // "no stored settings" path as a factory-fresh device. Any genuine change
    // an operator makes afterwards still differs from this baseline, so normal
    // persistence resumes with the very next tick().
    s_lastSaved = def;
    webLog("Settings: calibration and tuning reset to defaults (WiFi/password untouched)");
  } else {
    // Erase failed, so RAM and flash disagree and the stale blob would come
    // back on the next boot - which is the one outcome a reset must not have.
    // Overwrite it with the defaults instead. If that fails too, deliberately
    // leave s_lastSaved as the pre-reset copy: it now differs from the live
    // state, so tick()'s dirty check retries the write every kSaveIntervalMs
    // until one succeeds.
    if (writeBlob(def)) {
      s_lastSaved = def;
      webLog("Settings: reset to defaults (erase failed, defaults written instead)");
    } else {
      webLog("Settings: reset applied in RAM but NVS write failed - will retry");
    }
  }
}

}  // namespace settings_store
