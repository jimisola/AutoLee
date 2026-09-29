#include <string>
#include <unity.h>
#include "console_command.h"

using autolee::console::Command;
using autolee::console::first_word;
using autolee::console::parse_command;

void setUp() {}
void tearDown() {}

void test_commands_are_recognised() {
  TEST_ASSERT_EQUAL(Command::kWifiScan, parse_command("wifi-scan"));
  TEST_ASSERT_EQUAL(Command::kWifiInfo, parse_command("wifi-info"));
  TEST_ASSERT_EQUAL(Command::kHelp, parse_command("help"));
  TEST_ASSERT_EQUAL(Command::kHelp, parse_command("?"));
}

void test_surrounding_whitespace_is_ignored() {
  TEST_ASSERT_EQUAL(Command::kWifiScan, parse_command("  wifi-scan  "));
  TEST_ASSERT_EQUAL(Command::kWifiInfo, parse_command("\twifi-info\r"));
}

void test_capitalisation_is_ignored() {
  TEST_ASSERT_EQUAL(Command::kWifiScan, parse_command("WIFI-SCAN"));
  TEST_ASSERT_EQUAL(Command::kWifiInfo, parse_command("Wifi-Info"));
}

void test_trailing_words_do_not_prevent_a_match() {
  TEST_ASSERT_EQUAL(Command::kWifiScan, parse_command("wifi-scan now"));
}

// Pressing enter is how people check the device is listening; it must not be
// answered with a complaint.
void test_a_blank_line_is_not_an_error() {
  TEST_ASSERT_EQUAL(Command::kNone, parse_command(""));
  TEST_ASSERT_EQUAL(Command::kNone, parse_command("   "));
  TEST_ASSERT_EQUAL(Command::kNone, parse_command("\r\n"));
}

void test_prefixes_and_near_misses_are_unknown() {
  TEST_ASSERT_EQUAL(Command::kUnknown, parse_command("wifi"));
  TEST_ASSERT_EQUAL(Command::kUnknown, parse_command("wifi-scanner"));
  TEST_ASSERT_EQUAL(Command::kUnknown, parse_command("wifi_scan"));
  TEST_ASSERT_EQUAL(Command::kUnknown, parse_command("??"));
}

void test_long_noise_is_unknown() {
  TEST_ASSERT_EQUAL(Command::kUnknown, parse_command(std::string(500, 'x')));
}

void test_first_word_comes_back_for_echoing() {
  TEST_ASSERT_EQUAL_STRING("reboot", first_word("  reboot now ").c_str());
  TEST_ASSERT_EQUAL_STRING("", first_word("   ").c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_commands_are_recognised);
  RUN_TEST(test_surrounding_whitespace_is_ignored);
  RUN_TEST(test_capitalisation_is_ignored);
  RUN_TEST(test_trailing_words_do_not_prevent_a_match);
  RUN_TEST(test_a_blank_line_is_not_an_error);
  RUN_TEST(test_prefixes_and_near_misses_are_unknown);
  RUN_TEST(test_long_noise_is_unknown);
  RUN_TEST(test_first_word_comes_back_for_echoing);
  return UNITY_END();
}
