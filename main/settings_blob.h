#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "config.h"  // NUM_PROFILES + the bounds validate() enforces

// ============================================================================
//  On-flash layout of the persisted settings blob, and the version-migration
//  chain that turns an OLD blob into the CURRENT one.
//
//  Deliberately split out of settings_store.cpp and kept free of ESP-IDF: this
//  header touches no NVS handle, no MotionState and no logging, so the parsing
//  and migration logic is exercised by host tests (host_test/test_settings_blob)
//  exactly as the firmware runs it. settings_store.cpp keeps the I/O half
//  (nvs_open/get/set/erase) and the MotionState apply half.
//
//  ------------------------------------------------------------------------
//  HOW TO ADD A PERSISTED FIELD (the whole point of this file)
//  ------------------------------------------------------------------------
//  Say the current version is N and you want to add a field.
//
//   1. Leave `PersistedV<N>` EXACTLY as it is - never edit an old version's
//      struct, it describes bytes that already exist on real devices' flash.
//      Its static_assert on sizeof() is the tripwire that stops you.
//   2. Define `struct PersistedV<N+1>` below it: copy the old fields in the
//      same order, append the new one(s), keep the layout padding-free and
//      add its own `static_assert(sizeof(...) == ..., ...)`.
//   3. Bump `kVersion` to N+1 and repoint `using Persisted = PersistedV<N+1>;`.
//   4. Copy `validateV<N>()` to `validateV<N+1>()` and add the bounds check for
//      the new field. Old validators stay as they are: each version validates
//      its OWN layout, so a corrupt old blob is rejected BEFORE it is migrated
//      forward into something that would otherwise look valid.
//   5. Write `migrate_v<N>_to_v<N+1>(const PersistedV<N> &in, PersistedV<N+1> &out)`:
//      copy every carried-over field and pick a sane default for the new one.
//   6. In `parseAndMigrate()`, extend `case <N>:` so that after validateV<N>()
//      it runs the new step, and add a fresh `case <N+1>:` for the direct load.
//      Blobs several versions old are handled by CHAINING the steps
//      (v1 -> v2 -> v3), not by writing a v1 -> v3 shortcut.
//   7. Add a host-test case that builds a raw v<N> byte buffer and asserts the
//      migrated result, alongside the existing ones.
//
//  A `static_assert` on kVersion at the bottom of parseAndMigrate() fails the
//  build if step 3 is done without step 6, so a bump can never silently ship
//  without its migration.
//
//  Failure policy is unchanged and still fails safe: anything this file does
//  not recognise, cannot size, or cannot validate is reported as a failure and
//  settings_store falls back to the compiled-in defaults with the calibration
//  cleared. A partially-trusted calibration is worse than none.
// ============================================================================
namespace settings_store {

// Bump whenever the persisted field set changes - and follow the recipe above.
constexpr uint16_t kVersion = 1;

// ---------------------------------------------------------------------------
//  Version 1 (shipped 2026-07)
// ---------------------------------------------------------------------------
// Fixed-layout mirror of the persistable subset of MotionState. Deliberately
// NOT `MotionState` itself:
//   - MotionState carries runtime-only fields that must never be restored.
//   - SpeedProfile::name is a `const char *`; persisting a pointer would store
//     a flash address that means nothing after a firmware update. The profile
//     names (and speed_hz, which nothing writes at runtime - grep confirms it
//     is read-only outside config.h) stay compiled in; only the user-tweakable
//     sg_trip is persisted per profile.
//   - `long` is 32-bit on this target but the width should not be implied by
//     the ABI in something stored on flash, hence the explicit int32_t.
// Field order is chosen so there is no implicit padding; the static_assert
// below is the tripwire if that ever stops being true (a padding byte would
// leak into the blob and, worse, into the memcmp() dirty check).
struct PersistedV1 {
  uint16_t version;
  uint16_t runCurrentMa;
  int32_t rawUp;
  int32_t rawDown;
  int32_t upOffsetSteps;
  int32_t downOffsetSteps;
  int32_t sgWorkZoneSteps;
  int32_t counter;
  uint16_t sgTrip[NUM_PROFILES];
  uint8_t activeProfile;
  uint8_t endpointsCalibrated;  // uint8_t, not bool: fixed on-flash width
};
static_assert(sizeof(PersistedV1) == 36,
              "PersistedV1 is a shipped on-flash layout - never edit it");
static_assert(NUM_PROFILES == 3, "PersistedV1::sgTrip sizing assumes 3 profiles");

// The layout the firmware works with in RAM. Repoint this (not PersistedV1)
// when a new version is introduced.
using Persisted = PersistedV1;

// Largest blob any known version occupies - the read buffer settings_store.cpp
// sizes its nvs_get_blob() against. Extend with max(...) when a longer version
// is added; a stored blob bigger than this is rejected as a size mismatch.
constexpr size_t kMaxBlobBytes = sizeof(PersistedV1);

// ---------------------------------------------------------------------------
//  Per-version validation
// ---------------------------------------------------------------------------
// Same bounds the live setters (web_server/ui_touch/motion_cmd) enforce, so a
// blob can never smuggle in a value the running firmware would have rejected.
// All-or-nothing: one bad field rejects the whole blob (see settings_store.h).
//
// This validates the V1 LAYOUT specifically. When v2 arrives it gets its own
// validateV2(); this one stays as the gate a v1 blob must pass *before* it is
// migrated forward, so corruption cannot be laundered into a valid-looking
// current-version struct by the migration step.
inline bool validateV1(const PersistedV1 &p) {
  if (p.runCurrentMa < RUN_CURRENT_MIN || p.runCurrentMa > RUN_CURRENT_MAX) return false;
  if (p.sgWorkZoneSteps < SG_WORK_ZONE_MIN || p.sgWorkZoneSteps > SG_WORK_ZONE_MAX) return false;
  if (p.activeProfile >= NUM_PROFILES) return false;
  for (uint8_t i = 0; i < NUM_PROFILES; i++) {
    if (p.sgTrip[i] < RUN_SG_TRIP_MIN || p.sgTrip[i] > RUN_SG_TRIP_MAX) return false;
  }
  if (p.upOffsetSteps < OFFSET_MIN || p.upOffsetSteps > OFFSET_MAX) return false;
  if (p.downOffsetSteps < OFFSET_MIN || p.downOffsetSteps > OFFSET_MAX) return false;
  if (p.counter < 0) return false;
  if (p.endpointsCalibrated > 1) return false;

  // Geometry sanity. calibrateEndpointsSensorless() zeroes the position at the
  // UP hard stop (rawUp == 0) and measures rawDown as the travel, bounded by
  // the search distance it is allowed to move. Anything outside that is not a
  // calibration this firmware could have produced.
  if (p.rawUp < -CAL_SEARCH_STEPS || p.rawUp > CAL_SEARCH_STEPS) return false;
  if (p.rawDown < -CAL_SEARCH_STEPS || p.rawDown > CAL_SEARCH_STEPS) return false;
  if (p.endpointsCalibrated) {
    const int32_t travel = p.rawDown - p.rawUp;
    if (travel <= 2 * ENDPOINT_GUARD || travel > CAL_SEARCH_STEPS) return false;
  }
  return true;
}

// Validation of the CURRENT layout, used by the direct (version == kVersion)
// load path. Repoint at validateV<new> together with `Persisted`.
inline bool validate(const Persisted &p) {
  return validateV1(p);
}

// ---------------------------------------------------------------------------
//  Migration steps
// ---------------------------------------------------------------------------
// One function per adjacent version pair, e.g.
//
//   inline void migrate_v1_to_v2(const PersistedV1 &in, PersistedV2 &out) {
//     out = PersistedV2{};          // zero-init: no byte left indeterminate
//     out.version   = 2;
//     out.rawUp     = in.rawUp;     // ... every carried-over field ...
//     out.stallCount = 0;           // new field: explicit, sane default
//   }
//
// None exist yet: kVersion is 1, so there is nothing older to come from. The
// mechanism below is what makes adding the first one a one-function change.

// ---------------------------------------------------------------------------
//  Dispatch
// ---------------------------------------------------------------------------
enum class ParseResult : uint8_t {
  Ok,              // `out` holds a validated, current-version struct
  TooShort,        // fewer bytes than the version field itself
  SizeMismatch,    // version is known, but the blob is not that version's size
  UnknownVersion,  // no migration defined (too old) or written by newer firmware
  Invalid,         // parsed fine, failed its own version's validation
};

// Read just the leading version field, without deserializing anything else.
// The version is the first member of every PersistedV* by construction, and
// this is what lets an old blob be interpreted with the struct it was written
// with rather than with today's.
inline bool peekBlobVersion(const void *data, size_t len, uint16_t &out) {
  static_assert(offsetof(PersistedV1, version) == 0, "version must lead every blob layout");
  if (data == nullptr || len < sizeof(uint16_t)) return false;
  std::memcpy(&out, data, sizeof(uint16_t));
  return true;
}

// Parse a raw NVS blob into the CURRENT layout, migrating it forward if it was
// written by an older firmware. `out` is only written on ParseResult::Ok.
//
// `fromVersion` (optional) reports the version actually found on flash - set
// even on failure, so the caller can log *which* version it refused, and on
// success can tell a migration apart from a same-version load.
inline ParseResult parseAndMigrate(const void *data, size_t len, Persisted &out,
                                   uint16_t *fromVersion = nullptr) {
  uint16_t version = 0;
  if (!peekBlobVersion(data, len, version)) return ParseResult::TooShort;
  if (fromVersion) *fromVersion = version;

  switch (version) {
    case 1: {
      PersistedV1 v1{};
      if (len != sizeof(v1)) return ParseResult::SizeMismatch;
      std::memcpy(&v1, data, sizeof(v1));
      // Validate the v1 layout on its OWN terms before anything downstream
      // trusts it. Every future step in the chain does the same for its input.
      if (!validateV1(v1)) return ParseResult::Invalid;
      // ---- migration chain starts here -------------------------------------
      // v1 IS the current version today, so the chain is empty and this is the
      // plain direct load. When kVersion becomes 2 this case becomes:
      //     PersistedV2 v2{}; migrate_v1_to_v2(v1, v2);
      //     ... (further steps) ...
      //     out = v<kVersion>;
      // and a new `case 2:` below handles the direct load.
      out = v1;
      return ParseResult::Ok;
    }
    default:
      // Either older than any layout we still know how to read, or written by
      // a newer firmware (an OTA rollback). Neither is guessable: fail safe.
      return ParseResult::UnknownVersion;
  }
}

// Step 6 of the recipe above, enforced by the compiler: bumping kVersion
// without extending parseAndMigrate()'s switch breaks the build here.
static_assert(
    kVersion == 1,
    "kVersion was bumped - add the new `case` and the migrate_vN_to_vN+1() step to "
    "parseAndMigrate(), then update this assert. See the recipe at the top of this file.");

}  // namespace settings_store
