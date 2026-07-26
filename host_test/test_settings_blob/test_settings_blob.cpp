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
//   2. That the documented "add a field" recipe actually works. kVersion is
//      still 1, so there is no real v1 -> v2 step to exercise yet; the last
//      test rehearses the recipe on stand-in structs - old layout frozen,
//      input validated on its own version's terms, fields carried over, steps
//      chained - to prove the shape is sound before #9 (health monitoring)
//      relies on it. Replace it with the real migration's test when that lands.
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
//  The common case: a real v1 blob loading while kVersion == 1. Behaviour here
//  must be exactly what it was before migrations existed.
// ---------------------------------------------------------------------------
void test_v1_blob_loads_unchanged() {
  V1Fields f;
  const std::vector<uint8_t> raw = buildV1(f);

  Persisted out{};
  uint16_t from = 0;
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Ok,
                        (int)parseAndMigrate(raw.data(), raw.size(), out, &from));

  TEST_ASSERT_EQUAL_UINT16(1, from);
  TEST_ASSERT_EQUAL_UINT16(kVersion, from);  // no migration ran: same version
  TEST_ASSERT_EQUAL_UINT16(1, out.version);
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
  f.version = 2;  // from the future: an OTA rollback to this firmware
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

// ---------------------------------------------------------------------------
//  Recipe rehearsal (see the file header): kVersion is still 1, so there is no
//  real migration to run yet. These stand-ins follow settings_blob.h's recipe
//  step for step and assert the properties the real chain must have:
//    - the old layout is frozen and still parseable from raw bytes,
//    - the old blob is validated as ITS version before being migrated,
//    - carried-over fields survive and new fields get an explicit default,
//    - multi-version jumps are handled by chaining single steps.
// ---------------------------------------------------------------------------
namespace recipe {

// Step 1: the shipped layout, never edited (stands in for PersistedV1).
struct FakeV1 {
  uint16_t version;
  uint16_t runCurrentMa;
  int32_t counter;
};
static_assert(sizeof(FakeV1) == 8, "FakeV1 is a shipped layout - never edit it");

// Step 2: the new layout, old fields in the same order plus the new one.
struct FakeV2 {
  uint16_t version;
  uint16_t runCurrentMa;
  int32_t counter;
  int32_t stallCount;  // the added field
};
static_assert(sizeof(FakeV2) == 12, "FakeV2 layout changed");

// Step 2 again: v3 adds one more, so there is a two-step chain to exercise.
struct FakeV3 {
  uint16_t version;
  uint16_t runCurrentMa;
  int32_t counter;
  int32_t stallCount;
  int32_t cycleMs;
};

constexpr uint16_t kFakeCurrent = 3;
using FakeCurrent = FakeV3;

// Step 4: each version validates its OWN layout.
bool validateFakeV1(const FakeV1 &p) {
  return p.runCurrentMa >= RUN_CURRENT_MIN && p.runCurrentMa <= RUN_CURRENT_MAX && p.counter >= 0;
}
bool validateFakeV2(const FakeV2 &p) {
  return p.runCurrentMa >= RUN_CURRENT_MIN && p.runCurrentMa <= RUN_CURRENT_MAX && p.counter >= 0 &&
         p.stallCount >= 0;
}

// Step 5: one function per adjacent pair.
void migrate_fake_v1_to_v2(const FakeV1 &in, FakeV2 &out) {
  out = FakeV2{};
  out.version = 2;
  out.runCurrentMa = in.runCurrentMa;
  out.counter = in.counter;
  out.stallCount = 0;  // new field: explicit default, never indeterminate
}
void migrate_fake_v2_to_v3(const FakeV2 &in, FakeV3 &out) {
  out = FakeV3{};
  out.version = 3;
  out.runCurrentMa = in.runCurrentMa;
  out.counter = in.counter;
  out.stallCount = in.stallCount;
  out.cycleMs = 0;
}

// Step 6: dispatch, with the chain running through every intermediate step.
ParseResult fakeParse(const void *data, size_t len, FakeCurrent &out) {
  uint16_t version = 0;
  if (len < sizeof(uint16_t)) return ParseResult::TooShort;
  std::memcpy(&version, data, sizeof(version));
  switch (version) {
    case 1: {
      FakeV1 v1{};
      if (len != sizeof(v1)) return ParseResult::SizeMismatch;
      std::memcpy(&v1, data, sizeof(v1));
      if (!validateFakeV1(v1)) return ParseResult::Invalid;
      FakeV2 v2{};
      migrate_fake_v1_to_v2(v1, v2);
      FakeV3 v3{};
      migrate_fake_v2_to_v3(v2, v3);
      out = v3;
      return ParseResult::Ok;
    }
    case 2: {
      FakeV2 v2{};
      if (len != sizeof(v2)) return ParseResult::SizeMismatch;
      std::memcpy(&v2, data, sizeof(v2));
      if (!validateFakeV2(v2)) return ParseResult::Invalid;
      FakeV3 v3{};
      migrate_fake_v2_to_v3(v2, v3);
      out = v3;
      return ParseResult::Ok;
    }
    case 3: {
      FakeV3 v3{};
      if (len != sizeof(v3)) return ParseResult::SizeMismatch;
      std::memcpy(&v3, data, sizeof(v3));
      out = v3;
      return ParseResult::Ok;
    }
    default:
      return ParseResult::UnknownVersion;
  }
}

// A raw v1-shaped blob, built byte by byte like the real one above.
std::vector<uint8_t> buildFakeV1(uint16_t version, uint16_t runCurrentMa, int32_t counter) {
  std::vector<uint8_t> b(8, 0);
  putU16(b, 0, version);
  putU16(b, 2, runCurrentMa);
  putI32(b, 4, counter);
  return b;
}

}  // namespace recipe

void test_recipe_migrates_an_old_blob_forward() {
  using namespace recipe;
  const std::vector<uint8_t> raw = buildFakeV1(1, 2400, 4321);

  FakeCurrent out{};
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Ok, (int)fakeParse(raw.data(), raw.size(), out));

  // Two chained steps ran; the carried-over fields survived both.
  TEST_ASSERT_EQUAL_UINT16(kFakeCurrent, out.version);
  TEST_ASSERT_EQUAL_UINT16(2400, out.runCurrentMa);
  TEST_ASSERT_EQUAL_INT32(4321, out.counter);
  // Fields that did not exist in v1 got their explicit defaults, not garbage.
  TEST_ASSERT_EQUAL_INT32(0, out.stallCount);
  TEST_ASSERT_EQUAL_INT32(0, out.cycleMs);
}

void test_recipe_rejects_a_corrupt_old_blob_before_migrating() {
  using namespace recipe;
  // The whole reason each step validates its own input: without validateFakeV1
  // this blob would migrate into a v3 struct that passes every v3 check.
  const std::vector<uint8_t> bad = buildFakeV1(1, 2400, -5);
  FakeCurrent out{};
  out.counter = -999;  // sentinel
  TEST_ASSERT_EQUAL_INT((int)ParseResult::Invalid, (int)fakeParse(bad.data(), bad.size(), out));
  TEST_ASSERT_EQUAL_INT32(-999, out.counter);

  // ... and an old blob of the wrong length never reaches a migration either.
  const std::vector<uint8_t> shortBlob = {0x01, 0x00, 0x60, 0x09};
  TEST_ASSERT_EQUAL_INT((int)ParseResult::SizeMismatch,
                        (int)fakeParse(shortBlob.data(), shortBlob.size(), out));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_v1_layout_is_frozen);
  RUN_TEST(test_peek_version_reads_only_the_leading_field);
  RUN_TEST(test_v1_blob_loads_unchanged);
  RUN_TEST(test_uncalibrated_v1_blob_loads);
  RUN_TEST(test_unknown_versions_are_refused);
  RUN_TEST(test_size_mismatches_are_refused);
  RUN_TEST(test_out_of_range_fields_are_refused);
  RUN_TEST(test_recipe_migrates_an_old_blob_forward);
  RUN_TEST(test_recipe_rejects_a_corrupt_old_blob_before_migrating);
  return UNITY_END();
}
