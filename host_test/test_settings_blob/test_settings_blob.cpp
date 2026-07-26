// ============================================================================
//  host_test/test_settings_blob - the persisted-settings on-flash layout and
//  the version-migration chain (main/settings_blob.h).
//
//  Two things are under test here:
//
//   1. The REAL production path, byte for byte. Every "stored blob" in this
//      file is assembled as raw little-endian bytes at hand-written offsets,
//      NOT by memcpy-ing a PersistedV1 - so the test is an independent
//      description of what is on a deployed device's flash. If someone edits
//      PersistedV1 (which the recipe forbids), these tests fail even though
//      the struct's own static_assert on sizeof() might still pass.
//
//   2. That the migration chain actually carries an old device forward.
//      kVersion is 2 (the health counters of #9), so the v1 -> v2 step is real
//      and is exercised here through the shipped parseAndMigrate(): a raw v1
//      byte buffer in, a PersistedV2 out, every carried-over field asserted and
//      every new field asserted to be 0. This replaces the stand-in "recipe
//      rehearsal" the file used to carry while kVersion was still 1.
// ============================================================================
#include <unity.h>

#include <cstring>
#include <vector>

#include "settings_blob.h"

using namespace settings_store;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
//  Raw v1 blob builder - the on-flash layout written out by hand.
//    off  0 u16 version        off 16 i32 downOffsetSteps
//    off  2 u16 runCurrentMa   off 20 i32 sgWorkZoneSteps
//    off  4 i32 rawUp          off 24 i32 counter
//    off  8 i32 rawDown        off 28 u16 sgTrip[3]
//    off 12 i32 upOffsetSteps  off 34 u8  activeProfile, 35 u8 endpointsCalibrated
// ---------------------------------------------------------------------------
namespace {

struct V1Fields {
  uint16_t version = 1;
  uint16_t runCurrentMa = 2200;
  int32_t rawUp = 0;
  int32_t rawDown = 41000;
  int32_t upOffsetSteps = -250;
  int32_t downOffsetSteps = 375;
  int32_t sgWorkZoneSteps = 6000;
  int32_t counter = 1234;
  uint16_t sgTrip[3] = {80, 95, 130};
  uint8_t activeProfile = 2;
  uint8_t endpointsCalibrated = 1;
};

// Raw v2 blob: the v1 bytes above, unchanged and at the same offsets, plus
//    off 36 u32 totalCycleTimeMs   off 44 u16 stallCount
//    off 40 u32 longestCycleMs     off 46 u16 calibrationCount
//                                  off 48 u16 otaCount
//                                  off 50 u16 resetCount
struct V2Extra {
  uint32_t totalCycleTimeMs = 812345;
  uint32_t longestCycleMs = 3100;
  uint16_t stallCount = 17;
  uint16_t calibrationCount = 4;
  uint16_t otaCount = 9;
  uint16_t resetCount = 302;
};

void putU16(std::vector<uint8_t> &b, size_t off, uint16_t v) {
  b[off + 0] = (uint8_t)(v & 0xFF);
  b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

void putI32(std::vector<uint8_t> &b, size_t off, int32_t v) {
  const uint32_t u = (uint32_t)v;  // two's complement, little-endian
  b[off + 0] = (uint8_t)(u & 0xFF);
  b[off + 1] = (uint8_t)((u >> 8) & 0xFF);
  b[off + 2] = (uint8_t)((u >> 16) & 0xFF);
  b[off + 3] = (uint8_t)((u >> 24) & 0xFF);
}

std::vector<uint8_t> buildV1(const V1Fields &f) {
  std::vector<uint8_t> b(36, 0);
  putU16(b, 0, f.version);
  putU16(b, 2, f.runCurrentMa);
  putI32(b, 4, f.rawUp);
  putI32(b, 8, f.rawDown);
  putI32(b, 12, f.upOffsetSteps);
  putI32(b, 16, f.downOffsetSteps);
  putI32(b, 20, f.sgWorkZoneSteps);
  putI32(b, 24, f.counter);
  putU16(b, 28, f.sgTrip[0]);
  putU16(b, 30, f.sgTrip[1]);
  putU16(b, 32, f.sgTrip[2]);
  b[34] = f.activeProfile;
  b[35] = f.endpointsCalibrated;
  return b;
}

void putU32(std::vector<uint8_t> &b, size_t off, uint32_t u) {
  b[off + 0] = (uint8_t)(u & 0xFF);
  b[off + 1] = (uint8_t)((u >> 8) & 0xFF);
  b[off + 2] = (uint8_t)((u >> 16) & 0xFF);
  b[off + 3] = (uint8_t)((u >> 24) & 0xFF);
}

std::vector<uint8_t> buildV2(const V1Fields &f, const V2Extra &e) {
  std::vector<uint8_t> b = buildV1(f);  // identical prefix, by construction
  b.resize(52, 0);
  putU16(b, 0, 2);  // ... except the version field
  putU32(b, 36, e.totalCycleTimeMs);
  putU32(b, 40, e.longestCycleMs);
  putU16(b, 44, e.stallCount);
  putU16(b, 46, e.calibrationCount);
  putU16(b, 48, e.otaCount);
  putU16(b, 50, e.resetCount);
  return b;
}

// Parse a blob built from `f` and assert only that it was rejected the way we
// expect, leaving `out` untouched. Used by all the negative cases.
void expectRejected(const V1Fields &f, ParseResult expected) {
  const std::vector<uint8_t> raw = buildV1(f);
  Persisted out{};
  out.counter = -777;  // sentinel: must survive a rejected parse
  const ParseResult r = parseAndMigrate(raw.data(), raw.size(), out);
  TEST_ASSERT_EQUAL_INT((int)expected, (int)r);
  TEST_ASSERT_EQUAL_INT32(-777, out.counter);
}

}  // namespace

// ---------------------------------------------------------------------------
//  The layout itself
// ---------------------------------------------------------------------------
void test_v1_layout_is_frozen() {
  // The numbers the builder above hard-codes must be the struct's real offsets.
  TEST_ASSERT_EQUAL_UINT32(36, (uint32_t)sizeof(PersistedV1));
  TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)offsetof(PersistedV1, version));
  TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)offsetof(PersistedV1, runCurrentMa));
  TEST_ASSERT_EQUAL_UINT32(4, (uint32_t)offsetof(PersistedV1, rawUp));
  TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)offsetof(PersistedV1, rawDown));
  TEST_ASSERT_EQUAL_UINT32(12, (uint32_t)offsetof(PersistedV1, upOffsetSteps));
  TEST_ASSERT_EQUAL_UINT32(16, (uint32_t)offsetof(PersistedV1, downOffsetSteps));
  TEST_ASSERT_EQUAL_UINT32(20, (uint32_t)offsetof(PersistedV1, sgWorkZoneSteps));
  TEST_ASSERT_EQUAL_UINT32(24, (uint32_t)offsetof(PersistedV1, counter));
  TEST_ASSERT_EQUAL_UINT32(28, (uint32_t)offsetof(PersistedV1, sgTrip));
  TEST_ASSERT_EQUAL_UINT32(34, (uint32_t)offsetof(PersistedV1, activeProfile));
  TEST_ASSERT_EQUAL_UINT32(35, (uint32_t)offsetof(PersistedV1, endpointsCalibrated));
  // The read buffer settings_store.cpp sizes against must fit every version.
  TEST_ASSERT_TRUE(kMaxBlobBytes >= sizeof(PersistedV1));
  TEST_ASSERT_TRUE(kMaxBlobBytes >= sizeof(PersistedV2));
}

// v2 must be a strict superset of v1: same bytes at the same offsets, with the
// health counters appended. That is what lets migrate_v1_to_v2() be a plain
// field-by-field copy - and what the raw builders above assume.
void test_v2_layout_extends_v1_without_moving_anything() {
  TEST_ASSERT_EQUAL_UINT32(52, (uint32_t)sizeof(PersistedV2));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, version), offsetof(PersistedV2, version));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, runCurrentMa),
                           offsetof(PersistedV2, runCurrentMa));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, rawUp), offsetof(PersistedV2, rawUp));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, rawDown), offsetof(PersistedV2, rawDown));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, upOffsetSteps),
                           offsetof(PersistedV2, upOffsetSteps));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, downOffsetSteps),
                           offsetof(PersistedV2, downOffsetSteps));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, sgWorkZoneSteps),
                           offsetof(PersistedV2, sgWorkZoneSteps));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, counter), offsetof(PersistedV2, counter));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, sgTrip), offsetof(PersistedV2, sgTrip));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, activeProfile),
                           offsetof(PersistedV2, activeProfile));
  TEST_ASSERT_EQUAL_UINT32(offsetof(PersistedV1, endpointsCalibrated),
                           offsetof(PersistedV2, endpointsCalibrated));
  // The appended block, at the offsets buildV2() hard-codes.
  TEST_ASSERT_EQUAL_UINT32(36, (uint32_t)offsetof(PersistedV2, totalCycleTimeMs));
  TEST_ASSERT_EQUAL_UINT32(40, (uint32_t)offsetof(PersistedV2, longestCycleMs));
  TEST_ASSERT_EQUAL_UINT32(44, (uint32_t)offsetof(PersistedV2, stallCount));
  TEST_ASSERT_EQUAL_UINT32(46, (uint32_t)offsetof(PersistedV2, calibrationCount));
  TEST_ASSERT_EQUAL_UINT32(48, (uint32_t)offsetof(PersistedV2, otaCount));
  TEST_ASSERT_EQUAL_UINT32(50, (uint32_t)offsetof(PersistedV2, resetCount));
  // `Persisted` (what settings_store.cpp works with) must be the current one.
  TEST_ASSERT_EQUAL_UINT16(2, kVersion);
  TEST_ASSERT_EQUAL_UINT32(sizeof(PersistedV2), (uint32_t)sizeof(Persisted));
}

void test_peek_version_reads_only_the_leading_field() {
  const std::vector<uint8_t> raw = buildV1(V1Fields{});
  uint16_t v = 0xFFFF;
  TEST_ASSERT_TRUE(peekBlobVersion(raw.data(), raw.size(), v));
  TEST_ASSERT_EQUAL_UINT16(1, v);

  // Two bytes is enough to know the version; the rest is the version's problem.
  const uint8_t twoBytes[2] = {0x07, 0x00};
  TEST_ASSERT_TRUE(peekBlobVersion(twoBytes, sizeof(twoBytes), v));
  TEST_ASSERT_EQUAL_UINT16(7, v);

  TEST_ASSERT_FALSE(peekBlobVersion(twoBytes, 1, v));
  TEST_ASSERT_FALSE(peekBlobVersion(nullptr, 36, v));
}

// ---------------------------------------------------------------------------
//  THE migration: a genuine v1 blob - the 36 bytes really sitting in NVS on a
//  device flashed before the health counters existed - carried forward to v2
//  through the shipped parseAndMigrate(). Nothing here is a stand-in.
// ---------------------------------------------------------------------------
void test_v1_blob_migrates_to_v2_carrying_every_field() {
  V1Fields f;
  const std::vector<uint8_t> raw = buildV1(f);
  TEST_ASSERT_EQUAL_UINT32(36, (uint32_t)raw.size());  // it really is the v1 length

  Persisted out{};
  uint16_t from = 0;
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Ok,
                        (int)parseAndMigrate(raw.data(), raw.size(), out, &from));

  // A migration ran: found v1, produced the current version.
  TEST_ASSERT_EQUAL_UINT16(1, from);
  TEST_ASSERT_NOT_EQUAL_UINT16(kVersion, from);
  TEST_ASSERT_EQUAL_UINT16(2, out.version);
  TEST_ASSERT_EQUAL_UINT16(kVersion, out.version);

  // Every field that existed in v1 survived, byte for byte - this is the whole
  // point: an upgrade must not cost the operator their calibration.
  TEST_ASSERT_EQUAL_UINT16(2200, out.runCurrentMa);
  TEST_ASSERT_EQUAL_INT32(0, out.rawUp);
  TEST_ASSERT_EQUAL_INT32(41000, out.rawDown);
  TEST_ASSERT_EQUAL_INT32(-250, out.upOffsetSteps);  // negative offset survives
  TEST_ASSERT_EQUAL_INT32(375, out.downOffsetSteps);
  TEST_ASSERT_EQUAL_INT32(6000, out.sgWorkZoneSteps);
  TEST_ASSERT_EQUAL_INT32(1234, out.counter);
  TEST_ASSERT_EQUAL_UINT16(80, out.sgTrip[0]);
  TEST_ASSERT_EQUAL_UINT16(95, out.sgTrip[1]);
  TEST_ASSERT_EQUAL_UINT16(130, out.sgTrip[2]);
  TEST_ASSERT_EQUAL_UINT8(2, out.activeProfile);
  TEST_ASSERT_EQUAL_UINT8(1, out.endpointsCalibrated);

  // ... and all six fields that did not exist in v1 are 0, not garbage: a
  // migrating device has no recorded history to backfill.
  TEST_ASSERT_EQUAL_UINT32(0, out.totalCycleTimeMs);
  TEST_ASSERT_EQUAL_UINT32(0, out.longestCycleMs);
  TEST_ASSERT_EQUAL_UINT16(0, out.stallCount);
  TEST_ASSERT_EQUAL_UINT16(0, out.calibrationCount);
  TEST_ASSERT_EQUAL_UINT16(0, out.otaCount);
  TEST_ASSERT_EQUAL_UINT16(0, out.resetCount);
}

// The migration must not depend on `out` happening to arrive zeroed: a caller
// reusing a struct from an earlier parse must not leak old counters into it.
void test_migration_overwrites_a_dirty_output_struct() {
  Persisted out{};
  out.totalCycleTimeMs = 999999;
  out.longestCycleMs = 4242;
  out.stallCount = 55;
  out.calibrationCount = 66;
  out.otaCount = 77;
  out.resetCount = 88;
  out.counter = 4321;

  const std::vector<uint8_t> raw = buildV1(V1Fields{});
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Ok, (int)parseAndMigrate(raw.data(), raw.size(), out));

  TEST_ASSERT_EQUAL_UINT32(0, out.totalCycleTimeMs);
  TEST_ASSERT_EQUAL_UINT32(0, out.longestCycleMs);
  TEST_ASSERT_EQUAL_UINT16(0, out.stallCount);
  TEST_ASSERT_EQUAL_UINT16(0, out.calibrationCount);
  TEST_ASSERT_EQUAL_UINT16(0, out.otaCount);
  TEST_ASSERT_EQUAL_UINT16(0, out.resetCount);
  TEST_ASSERT_EQUAL_INT32(1234, out.counter);  // from the blob, not the leftover
}

// A corrupt v1 blob must be rejected on V1's terms BEFORE it is migrated -
// otherwise the migration would launder it into a v2 struct that passes every
// v2 check (the new fields being zeroed makes it look immaculate).
void test_corrupt_v1_blob_is_rejected_before_migrating() {
  V1Fields f;
  f.counter = -1;  // a piece count can never be negative
  expectRejected(f, ParseResult::Invalid);

  V1Fields g;
  g.runCurrentMa = 9000;  // > RUN_CURRENT_MAX
  expectRejected(g, ParseResult::Invalid);
}

// ---------------------------------------------------------------------------
//  The direct (current-version) load path
// ---------------------------------------------------------------------------
void test_v2_blob_loads_unchanged() {
  const std::vector<uint8_t> raw = buildV2(V1Fields{}, V2Extra{});
  TEST_ASSERT_EQUAL_UINT32(52, (uint32_t)raw.size());

  Persisted out{};
  uint16_t from = 0;
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Ok,
                        (int)parseAndMigrate(raw.data(), raw.size(), out, &from));

  TEST_ASSERT_EQUAL_UINT16(2, from);
  TEST_ASSERT_EQUAL_UINT16(kVersion, from);  // no migration ran: same version
  // Carried-over half still parses at the v1 offsets.
  TEST_ASSERT_EQUAL_UINT16(2200, out.runCurrentMa);
  TEST_ASSERT_EQUAL_INT32(41000, out.rawDown);
  TEST_ASSERT_EQUAL_INT32(1234, out.counter);
  TEST_ASSERT_EQUAL_UINT8(2, out.activeProfile);
  // ... and the health counters round-trip exactly, including a value past
  // 16 bits in the 32-bit fields.
  TEST_ASSERT_EQUAL_UINT32(812345, out.totalCycleTimeMs);
  TEST_ASSERT_EQUAL_UINT32(3100, out.longestCycleMs);
  TEST_ASSERT_EQUAL_UINT16(17, out.stallCount);
  TEST_ASSERT_EQUAL_UINT16(4, out.calibrationCount);
  TEST_ASSERT_EQUAL_UINT16(9, out.otaCount);
  TEST_ASSERT_EQUAL_UINT16(302, out.resetCount);
}

// Deliberate design decision (see validateV2()): the counters themselves have
// no bounds - saturated ones must NOT cost the operator their calibration.
void test_v2_extreme_counters_are_still_valid() {
  V2Extra e;
  e.totalCycleTimeMs = 0xFFFFFFFFu;
  e.longestCycleMs = 0xFFFFFFFFu;  // > totalCycleTimeMs is possible after a wrap
  e.stallCount = 0xFFFF;
  e.calibrationCount = 0xFFFF;
  e.otaCount = 0xFFFF;
  e.resetCount = 0xFFFF;
  const std::vector<uint8_t> raw = buildV2(V1Fields{}, e);

  Persisted out{};
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Ok, (int)parseAndMigrate(raw.data(), raw.size(), out));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, out.totalCycleTimeMs);
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, out.resetCount);
  TEST_ASSERT_EQUAL_UINT8(1, out.endpointsCalibrated);  // calibration untouched
}

// The carried-over half of a v2 blob is validated exactly as v1's is.
void test_v2_blob_with_a_bad_carried_field_is_refused() {
  V1Fields f;
  f.activeProfile = 3;  // == NUM_PROFILES
  const std::vector<uint8_t> raw = buildV2(f, V2Extra{});
  Persisted out{};
  out.counter = -777;
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Invalid,
                        (int)parseAndMigrate(raw.data(), raw.size(), out));
  TEST_ASSERT_EQUAL_INT32(-777, out.counter);
}

void test_uncalibrated_v1_blob_loads() {
  // endpointsCalibrated == 0 exempts the geometry check, so raws that would
  // otherwise be nonsense are fine (this is what a factory-fresh save holds).
  V1Fields f;
  f.endpointsCalibrated = 0;
  f.rawUp = 0;
  f.rawDown = 0;
  f.counter = 0;
  const std::vector<uint8_t> raw = buildV1(f);
  Persisted out{};
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Ok, (int)parseAndMigrate(raw.data(), raw.size(), out));
  TEST_ASSERT_EQUAL_UINT8(0, out.endpointsCalibrated);
}

// ---------------------------------------------------------------------------
//  Rejection paths - each must fail safe and leave `out` untouched
// ---------------------------------------------------------------------------
void test_unknown_versions_are_refused() {
  V1Fields f;
  f.version = 0;  // predates any layout we know
  expectRejected(f, ParseResult::UnknownVersion);
  f.version = 3;  // from the future: an OTA rollback to this firmware
  expectRejected(f, ParseResult::UnknownVersion);
  f.version = 0xFFFF;  // erased flash / garbage
  expectRejected(f, ParseResult::UnknownVersion);
}

void test_size_mismatches_are_refused() {
  const std::vector<uint8_t> raw = buildV1(V1Fields{});
  Persisted out{};

  // Fewer bytes than the version field itself.
  TEST_ASSERT_EQUAL_INT((int)ParseResult::TooShort, (int)parseAndMigrate(raw.data(), 0, out));
  TEST_ASSERT_EQUAL_INT((int)ParseResult::TooShort, (int)parseAndMigrate(raw.data(), 1, out));

  // Version 1 is known, but 35 or 37 bytes is not v1's shape. This is the case
  // that matters most: a future v2 blob that somehow kept version==1.
  TEST_ASSERT_EQUAL_INT((int)ParseResult::SizeMismatch, (int)parseAndMigrate(raw.data(), 35, out));
  std::vector<uint8_t> longer = raw;
  longer.push_back(0);
  TEST_ASSERT_EQUAL_INT((int)ParseResult::SizeMismatch,
                        (int)parseAndMigrate(longer.data(), longer.size(), out));

  // The mirror image now that v2 exists: v2's length carrying v1's version
  // (and vice versa) is a size mismatch, never a silent reinterpretation.
  std::vector<uint8_t> v2AsV1 = buildV2(V1Fields{}, V2Extra{});
  putU16(v2AsV1, 0, 1);
  TEST_ASSERT_EQUAL_INT((int)ParseResult::SizeMismatch,
                        (int)parseAndMigrate(v2AsV1.data(), v2AsV1.size(), out));
  std::vector<uint8_t> v1AsV2 = buildV1(V1Fields{});
  putU16(v1AsV2, 0, 2);
  TEST_ASSERT_EQUAL_INT((int)ParseResult::SizeMismatch,
                        (int)parseAndMigrate(v1AsV2.data(), v1AsV2.size(), out));
}

void test_out_of_range_fields_are_refused() {
  {
    V1Fields f;
    f.runCurrentMa = 9000;  // > RUN_CURRENT_MAX
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;
    f.runCurrentMa = 10;  // < RUN_CURRENT_MIN
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;
    f.activeProfile = 3;  // == NUM_PROFILES
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;
    f.sgTrip[1] = 5000;  // > RUN_SG_TRIP_MAX
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;
    f.upOffsetSteps = 99999;  // > OFFSET_MAX
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;
    f.sgWorkZoneSteps = -1;  // < SG_WORK_ZONE_MIN
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;
    f.counter = -1;  // a piece count can never be negative
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;
    f.endpointsCalibrated = 2;  // not a bool byte
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;  // calibrated, but the travel is inside the endpoint guards
    f.rawDown = 60;
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;  // calibrated, but travel is inverted
    f.rawUp = 41000;
    f.rawDown = 0;
    expectRejected(f, ParseResult::Invalid);
  }
  {
    V1Fields f;  // beyond anything calibrateEndpointsSensorless() could produce
    f.rawDown = 500000;
    expectRejected(f, ParseResult::Invalid);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_v1_layout_is_frozen);
  RUN_TEST(test_v2_layout_extends_v1_without_moving_anything);
  RUN_TEST(test_peek_version_reads_only_the_leading_field);
  RUN_TEST(test_v1_blob_migrates_to_v2_carrying_every_field);
  RUN_TEST(test_migration_overwrites_a_dirty_output_struct);
  RUN_TEST(test_corrupt_v1_blob_is_rejected_before_migrating);
  RUN_TEST(test_v2_blob_loads_unchanged);
  RUN_TEST(test_v2_extreme_counters_are_still_valid);
  RUN_TEST(test_v2_blob_with_a_bad_carried_field_is_refused);
  RUN_TEST(test_uncalibrated_v1_blob_loads);
  RUN_TEST(test_unknown_versions_are_refused);
  RUN_TEST(test_size_mismatches_are_refused);
  RUN_TEST(test_out_of_range_fields_are_refused);
  return UNITY_END();
}
