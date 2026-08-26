#include <unity.h>
#include "wifi_scan.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

static ApRecord ap(const char *ssid, int8_t rssi, uint8_t channel = 6, bool secure = true,
                   uint8_t last_bssid_byte = 0x01) {
  ApRecord r;
  r.ssid = ssid;
  r.rssi = rssi;
  r.channel = channel;
  r.secure = secure;
  r.bssid[0] = 0xAA;
  r.bssid[1] = 0xBB;
  r.bssid[2] = 0xCC;
  r.bssid[3] = 0xDD;
  r.bssid[4] = 0xEE;
  r.bssid[5] = last_bssid_byte;
  return r;
}

void test_empty_survey_gives_empty_picker() {
  TEST_ASSERT_EQUAL_UINT32(0, strongest_per_ssid({}).size());
}

void test_hidden_ssids_are_dropped_from_the_picker() {
  Survey survey = {ap("visible", -40), ap("", -30), ap("also-visible", -50)};
  Survey picker = strongest_per_ssid(survey);
  TEST_ASSERT_EQUAL_UINT32(2, picker.size());
  TEST_ASSERT_EQUAL_STRING("visible", picker[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("also-visible", picker[1].ssid.c_str());
}

// The whole point of the split: the survey keeps the hidden radio that the
// picker drops. A dropped row must not mean an unseen radio.
void test_survey_keeps_what_the_picker_drops() {
  Survey survey = {ap("visible", -40), ap("", -30)};
  TEST_ASSERT_EQUAL_UINT32(2, survey.size());
  TEST_ASSERT_TRUE(survey[1].hidden());
  TEST_ASSERT_EQUAL_UINT32(1, strongest_per_ssid(survey).size());
}

void test_duplicate_ssid_collapses_to_one_row() {
  Survey survey = {ap("mesh", -40, 1, true, 0x01), ap("mesh", -70, 11, true, 0x02)};
  Survey picker = strongest_per_ssid(survey);
  TEST_ASSERT_EQUAL_UINT32(1, picker.size());
  TEST_ASSERT_EQUAL_STRING("mesh", picker[0].ssid.c_str());
}

// The old code kept the FIRST record per SSID and relied on the driver
// returning strongest-first. Nothing in the API promises that ordering, so the
// strongest is now chosen explicitly - this is the case that regresses if that
// ever goes back to first-wins.
void test_strongest_wins_even_when_it_is_not_first() {
  Survey survey = {ap("mesh", -80, 1, true, 0x01), ap("mesh", -35, 11, true, 0x02)};
  Survey picker = strongest_per_ssid(survey);
  TEST_ASSERT_EQUAL_UINT32(1, picker.size());
  TEST_ASSERT_EQUAL_INT(-35, picker[0].rssi);
  TEST_ASSERT_EQUAL_UINT8(11, picker[0].channel);
  TEST_ASSERT_EQUAL_UINT8(0x02, picker[0].bssid[5]);
}

// A stronger duplicate replaces the record but must not reorder the list; the
// dropdown's order is first-appearance.
void test_stronger_duplicate_keeps_the_ssid_position() {
  Survey survey = {ap("first", -60), ap("second", -70, 1, true, 0x01),
                   ap("first", -20, 11, true, 0x09)};
  Survey picker = strongest_per_ssid(survey);
  TEST_ASSERT_EQUAL_UINT32(2, picker.size());
  TEST_ASSERT_EQUAL_STRING("first", picker[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("second", picker[1].ssid.c_str());
  TEST_ASSERT_EQUAL_INT(-20, picker[0].rssi);
}

void test_equal_rssi_keeps_the_first_seen() {
  Survey survey = {ap("tie", -50, 1, true, 0x01), ap("tie", -50, 11, true, 0x02)};
  Survey picker = strongest_per_ssid(survey);
  TEST_ASSERT_EQUAL_UINT32(1, picker.size());
  TEST_ASSERT_EQUAL_UINT8(0x01, picker[0].bssid[5]);
}

// Two APs, same SSID, same channel - the real fault this split was built to
// surface. Both stay in the survey; the picker still shows one row.
void test_same_ssid_same_channel_both_survive_in_the_survey() {
  Survey survey = {ap("home", -45, 6, true, 0x01), ap("home", -47, 6, true, 0x02)};
  TEST_ASSERT_EQUAL_UINT32(2, survey.size());
  TEST_ASSERT_EQUAL_UINT8(survey[0].channel, survey[1].channel);
  TEST_ASSERT_EQUAL_UINT32(1, strongest_per_ssid(survey).size());
}

void test_open_and_secure_are_carried_through() {
  Survey survey = {ap("open-net", -40, 6, false), ap("locked", -40, 6, true)};
  Survey picker = strongest_per_ssid(survey);
  TEST_ASSERT_FALSE(picker[0].secure);
  TEST_ASSERT_TRUE(picker[1].secure);
}

void test_ssid_that_is_only_whitespace_is_not_hidden() {
  // Only an empty SSID means "not advertising". A space is a real, selectable
  // name and must survive.
  Survey survey = {ap(" ", -40)};
  TEST_ASSERT_FALSE(survey[0].hidden());
  TEST_ASSERT_EQUAL_UINT32(1, strongest_per_ssid(survey).size());
}

void test_bssid_to_string_is_lowercase_colon_separated() {
  ApRecord r = ap("x", -40, 6, true, 0x0f);
  TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:0f", bssid_to_string(r.bssid).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_survey_gives_empty_picker);
  RUN_TEST(test_hidden_ssids_are_dropped_from_the_picker);
  RUN_TEST(test_survey_keeps_what_the_picker_drops);
  RUN_TEST(test_duplicate_ssid_collapses_to_one_row);
  RUN_TEST(test_strongest_wins_even_when_it_is_not_first);
  RUN_TEST(test_stronger_duplicate_keeps_the_ssid_position);
  RUN_TEST(test_equal_rssi_keeps_the_first_seen);
  RUN_TEST(test_same_ssid_same_channel_both_survive_in_the_survey);
  RUN_TEST(test_open_and_secure_are_carried_through);
  RUN_TEST(test_ssid_that_is_only_whitespace_is_not_hidden);
  RUN_TEST(test_bssid_to_string_is_lowercase_colon_separated);
  return UNITY_END();
}
