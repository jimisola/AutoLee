#include <string>
#include <unity.h>
#include "wifi_report.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

static ApRecord ap(const char *ssid, int8_t rssi, uint8_t channel, const char *security,
                   uint8_t last_bssid_byte) {
  ApRecord r;
  r.ssid = ssid;
  r.rssi = rssi;
  r.channel = channel;
  r.security = security;
  r.secure = std::string(security) != "open";
  const uint8_t bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, last_bssid_byte};
  for (int i = 0; i < 6; i++) r.bssid[i] = bssid[i];
  return r;
}

static bool contains(const std::string &haystack, const char *needle) {
  return haystack.find(needle) != std::string::npos;
}

void test_bars_follow_the_thresholds() {
  TEST_ASSERT_EQUAL(4, signal_bars(-40));
  TEST_ASSERT_EQUAL(4, signal_bars(-55));
  TEST_ASSERT_EQUAL(3, signal_bars(-56));
  TEST_ASSERT_EQUAL(3, signal_bars(-67));
  TEST_ASSERT_EQUAL(2, signal_bars(-75));
  TEST_ASSERT_EQUAL(1, signal_bars(-85));
  TEST_ASSERT_EQUAL(0, signal_bars(-86));
}

void test_empty_survey_says_so() {
  TEST_ASSERT_TRUE(contains(format_survey({}), "no networks found"));
}

void test_row_carries_every_field() {
  const std::string out = format_survey({ap("home", -60, 6, "WPA2", 0x01)});
  TEST_ASSERT_TRUE(contains(out, "[###.]   -60   6  WPA2        aa:bb:cc:dd:ee:01  home\n"));
}

// The point of the survey over the picker: every radio is a row, including
// hidden ones and a second BSSID for the same name.
void test_hidden_and_duplicate_radios_each_get_a_row() {
  const Survey survey = {ap("mesh", -50, 1, "WPA2", 0x01), ap("mesh", -70, 11, "WPA2", 0x02),
                         ap("", -65, 6, "WPA2/WPA3", 0x03)};
  const std::string out = format_survey(survey);
  TEST_ASSERT_TRUE(contains(out, "aa:bb:cc:dd:ee:01  mesh\n"));
  TEST_ASSERT_TRUE(contains(out, "aa:bb:cc:dd:ee:02  mesh\n"));
  TEST_ASSERT_TRUE(contains(out, "aa:bb:cc:dd:ee:03  (hidden)\n"));
  TEST_ASSERT_TRUE(contains(out, "3 radio(s): 1 named network(s), 1 hidden."));
}

void test_unknown_security_is_marked() {
  TEST_ASSERT_TRUE(contains(format_survey({ap("x", -60, 6, "", 0x01)}), "  ?           aa:"));
}

// A nearby transmitter chooses the SSID; an escape sequence in it must reach
// the operator's terminal as text, not as a command.
void test_control_bytes_in_an_ssid_are_escaped() {
  const std::string out = format_survey({ap("evil\x1b[2Jname", -60, 6, "open", 0x01)});
  TEST_ASSERT_FALSE(contains(out, "\x1b"));
  TEST_ASSERT_TRUE(contains(out, "evil\\x1b[2Jname\n"));
}

void test_utf8_ssid_passes_through() {
  TEST_ASSERT_TRUE(
      contains(format_survey({ap("k\xc3\xa5k", -60, 6, "WPA2", 0x01)}), "k\xc3\xa5k\n"));
}

void test_link_report_lists_the_join() {
  LinkInfo link;
  link.ssid = "home";
  const uint8_t bssid[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  for (int i = 0; i < 6; i++) link.bssid[i] = bssid[i];
  link.channel = 11;
  link.rssi = -62;
  link.security = "WPA2";
  link.ip = "192.168.1.66";
  link.netmask = "255.255.255.0";
  link.gateway = "192.168.1.1";
  link.dns = "192.168.1.1";
  link.mac = "de:ad:be:ef:00:01";
  link.mdns = "autolee.local";
  TEST_ASSERT_EQUAL_STRING(
      "ssid       home\n"
      "bssid      11:22:33:44:55:66\n"
      "signal     -62 dBm (3/4 bars)\n"
      "channel    11\n"
      "security   WPA2\n"
      "ip         192.168.1.66\n"
      "netmask    255.255.255.0\n"
      "gateway    192.168.1.1\n"
      "dns        192.168.1.1\n"
      "mac        de:ad:be:ef:00:01\n"
      "mdns       autolee.local\n",
      format_link(link).c_str());
}

// Fields the netif could not supply are left out rather than printed blank.
void test_link_report_skips_what_is_unknown() {
  LinkInfo link;
  link.ssid = "home";
  link.rssi = -50;
  const std::string out = format_link(link);
  TEST_ASSERT_FALSE(contains(out, "dns"));
  TEST_ASSERT_FALSE(contains(out, "mdns"));
  TEST_ASSERT_TRUE(contains(out, "ssid       home\n"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_bars_follow_the_thresholds);
  RUN_TEST(test_empty_survey_says_so);
  RUN_TEST(test_row_carries_every_field);
  RUN_TEST(test_hidden_and_duplicate_radios_each_get_a_row);
  RUN_TEST(test_unknown_security_is_marked);
  RUN_TEST(test_control_bytes_in_an_ssid_are_escaped);
  RUN_TEST(test_utf8_ssid_passes_through);
  RUN_TEST(test_link_report_lists_the_join);
  RUN_TEST(test_link_report_skips_what_is_unknown);
  return UNITY_END();
}
