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
constexpr uint16_t kVersion = 2;

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

// ---------------------------------------------------------------------------
//  Version 2 (shipped 2026-07) - adds the lifetime health counters
// ---------------------------------------------------------------------------
// Every V1 field, in the same order and at the same offsets (so a v1 blob's
// bytes still mean what they meant), followed by the six lifetime counters the
// health-monitoring work added. These are diagnostics for support: how often
// the press has jammed, how long a stroke takes on average and at worst, how
// often it has been calibrated, OTA-updated and reset.
//
// Append order is chosen for a padding-free layout: V1 ends at offset 36, which
// is 4-aligned, so the two uint32_t fields go first (36, 40) and the four
// uint16_t ones after (44, 46, 48, 50) - total 52, a multiple of the struct's
// 4-byte alignment, so there is no trailing padding either. The static_assert
// below is the tripwire if that ever stops being true.
struct PersistedV2 {
  // --- carried over from V1, byte-identical ---
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
  // --- new in V2: lifetime health counters ---
  uint32_t totalCycleTimeMs;  // summed duration of every successful UP->DOWN stroke
  uint32_t longestCycleMs;    // longest single successful stroke seen
  uint16_t stallCount;        // lifetime jams (StallGuard trips that latched STALLED)
  uint16_t calibrationCount;  // lifetime successful calibrations
  uint16_t otaCount;          // lifetime successful OTA updates
  uint16_t resetCount;        // lifetime boots (incremented by settings_store::load())
};
static_assert(sizeof(PersistedV2) == 52,
              "PersistedV2 must stay padding-free - see the offsets comment above");
static_assert(offsetof(PersistedV2, totalCycleTimeMs) == sizeof(PersistedV1),
              "V2's new fields must start exactly where V1 ended");
static_assert(NUM_PROFILES == 3, "PersistedV2::sgTrip sizing assumes 3 profiles");

// The layout the firmware works with in RAM. Repoint this (not PersistedV1)
// when a new version is introduced.
using Persisted = PersistedV2;

// Largest blob any known version occupies - the read buffer settings_store.cpp
// sizes its nvs_get_blob() against. Extend with max(...) when a longer version
// is added; a stored blob bigger than this is rejected as a size mismatch.
constexpr size_t kMaxBlobBytes =
    sizeof(PersistedV1) > sizeof(PersistedV2) ? sizeof(PersistedV1) : sizeof(PersistedV2);

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

// Validates the V2 LAYOUT. Deliberately a copy of validateV1()'s body rather
// than a shared helper: each version must validate its own layout on its own
// terms and stay frozen once shipped, so a later change to v3's bounds can
// never retroactively change how a v1 or v2 blob is judged.
//
// The six counters added in v2 get NO bounds check, on purpose:
//   - They are unsigned, so "not negative" is already guaranteed by the type.
//   - Unlike every field above them, no live setter constrains them - nothing
//     writes them but a ++ - so there is no "value the running firmware would
//     have rejected" to mirror, which is what these validators exist for.
//   - Any upper bound would be an invented number, and getting it wrong is
//     expensive: this is an all-or-nothing validator, so a device that had
//     simply been running for a long time would have its CALIBRATION thrown
//     away over a cosmetic statistic.
//   - The tempting cross-field invariant (longestCycleMs <= totalCycleTimeMs)
//     is not actually invariant: totalCycleTimeMs is a uint32_t of milliseconds
//     and wraps after ~49.7 days of accumulated stroke time, after which the
//     "corruption" it would flag is just a long-lived press.
// Bit-level corruption is not this function's job either - NVS stores its own
// CRC per entry and refuses to hand back a blob that fails it.
inline bool validateV2(const PersistedV2 &p) {
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

  // Geometry sanity - identical to V1's (see there for the reasoning).
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
  return validateV2(p);
}

// ---------------------------------------------------------------------------
//  Migration steps
// ---------------------------------------------------------------------------
// One function per adjacent version pair. A blob several versions old is
// carried forward by CHAINING these (v1 -> v2 -> v3), never by a shortcut.

// v1 -> v2: every V1 field is carried over unchanged; the six health counters
// added in v2 start at 0. A device upgrading from v1 firmware has no recorded
// history to backfill - there is nothing truthful to seed them with, and
// inventing a number would be worse than a visibly fresh count.
inline void migrate_v1_to_v2(const PersistedV1 &in, PersistedV2 &out) {
  out = PersistedV2{};  // zero-init first: no byte left indeterminate
  out.version = 2;
  out.runCurrentMa = in.runCurrentMa;
  out.rawUp = in.rawUp;
  out.rawDown = in.rawDown;
  out.upOffsetSteps = in.upOffsetSteps;
  out.downOffsetSteps = in.downOffsetSteps;
  out.sgWorkZoneSteps = in.sgWorkZoneSteps;
  out.counter = in.counter;
  for (uint8_t i = 0; i < NUM_PROFILES; i++) out.sgTrip[i] = in.sgTrip[i];
  out.activeProfile = in.activeProfile;
  out.endpointsCalibrated = in.endpointsCalibrated;
  // New in v2 - explicit, not merely inherited from the zero-init above.
  out.totalCycleTimeMs = 0;
  out.longestCycleMs = 0;
  out.stallCount = 0;
  out.calibrationCount = 0;
  out.otaCount = 0;
  out.resetCount = 0;
}

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
  static_assert(offsetof(PersistedV2, version) == 0, "version must lead every blob layout");
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
      // Add each further step below this one (v2 -> v3, ...) so an ancient blob
      // walks the whole chain; `out` is only ever assigned the current layout.
      PersistedV2 v2{};
      migrate_v1_to_v2(v1, v2);
      out = v2;
      return ParseResult::Ok;
    }
    case 2: {
      PersistedV2 v2{};
      if (len != sizeof(v2)) return ParseResult::SizeMismatch;
      std::memcpy(&v2, data, sizeof(v2));
      if (!validateV2(v2)) return ParseResult::Invalid;
      out = v2;  // current version: nothing to migrate
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
    kVersion == 2,
    "kVersion was bumped - add the new `case` and the migrate_vN_to_vN+1() step to "
    "parseAndMigrate(), then update this assert. See the recipe at the top of this file.");

}  // namespace settings_store
