#include <cstring>
#include <string>
#include <unity.h>
#include "boot_report.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

static std::string json(const BootReport &r) {
  char buf[2048];
  int n = r.toJson(buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE((size_t)n < sizeof(buf));
  return std::string(buf);
}

void test_empty_report_is_valid_and_says_so() {
  BootReport r;
  TEST_ASSERT_EQUAL_UINT32(0, r.size());
  TEST_ASSERT_EQUAL_STRING("{\"frozen\":false,\"dropped\":0,\"issues\":[]}", json(r).c_str());
}

void test_freeze_is_reported() {
  BootReport r;
  r.freeze();
  TEST_ASSERT_TRUE(r.frozen());
  TEST_ASSERT_TRUE(json(r).find("\"frozen\":true") != std::string::npos);
}

void test_issue_round_trips() {
  BootReport r;
  r.add("nvs-erased", "ESP_ERR_NVS_NEW_VERSION_FOUND");
  TEST_ASSERT_EQUAL_UINT32(1, r.size());
  TEST_ASSERT_EQUAL_STRING("nvs-erased", r.at(0).code);
  TEST_ASSERT_EQUAL_STRING(
      "{\"frozen\":false,\"dropped\":0,\"issues\":["
      "{\"code\":\"nvs-erased\",\"detail\":\"ESP_ERR_NVS_NEW_VERSION_FOUND\"}]}",
      json(r).c_str());
}

void test_multiple_issues_are_comma_separated() {
  BootReport r;
  r.add("a", "one");
  r.add("b", "two");
  TEST_ASSERT_TRUE(json(r).find("\"code\":\"a\",\"detail\":\"one\"},{\"code\":\"b\"") !=
                   std::string::npos);
}

// The whole point of freezing: nothing recorded after boot can appear in a
// report that the UI tells the user will not change until restart.
void test_frozen_report_refuses_new_issues() {
  BootReport r;
  r.add("early", "kept");
  r.freeze();
  r.add("late", "dropped-entirely");
  TEST_ASSERT_EQUAL_UINT32(1, r.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.dropped());  // not overflow - refused
  TEST_ASSERT_TRUE(json(r).find("late") == std::string::npos);
}

// Overflow must be visible. A report that silently drops entries reads as
// complete when it is not.
void test_overflow_is_counted_not_hidden() {
  BootReport r;
  for (size_t i = 0; i < BootReport::kCapacity + 3; i++) r.add("x", "y");
  TEST_ASSERT_EQUAL_UINT32(BootReport::kCapacity, r.size());
  TEST_ASSERT_EQUAL_UINT32(3, r.dropped());
  TEST_ASSERT_TRUE(json(r).find("\"dropped\":3") != std::string::npos);
}

void test_quotes_and_backslashes_cannot_break_the_json() {
  BootReport r;
  r.add("bad", "he said \"hi\" and \\ left");
  std::string s = json(r);
  TEST_ASSERT_TRUE(s.find("\\\"hi\\\"") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\\\\ left") != std::string::npos);
}

void test_control_characters_are_dropped() {
  BootReport r;
  r.add("ctl", "a\nb\tc");
  std::string s = json(r);
  TEST_ASSERT_TRUE(s.find("abc") != std::string::npos);
  TEST_ASSERT_TRUE(s.find('\n') == std::string::npos);
}

void test_oversized_strings_are_truncated_not_overflowed() {
  BootReport r;
  std::string huge(500, 'z');
  r.add(huge.c_str(), huge.c_str());
  TEST_ASSERT_TRUE(strlen(r.at(0).code) < sizeof(r.at(0).code));
  TEST_ASSERT_TRUE(strlen(r.at(0).detail) < sizeof(r.at(0).detail));
}

void test_null_arguments_do_not_crash() {
  BootReport r;
  r.add(nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT32(1, r.size());
  TEST_ASSERT_EQUAL_STRING("", r.at(0).code);
}

// A too-small buffer must report the length it needed, the way buildStateJson()
// does, so the caller can detect the truncation instead of shipping half a
// document.
void test_short_buffer_reports_needed_length() {
  BootReport r;
  r.add("some-code", "some detail here");
  char small[16];
  int n = r.toJson(small, sizeof(small));
  TEST_ASSERT_TRUE(n >= (int)sizeof(small));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_report_is_valid_and_says_so);
  RUN_TEST(test_freeze_is_reported);
  RUN_TEST(test_issue_round_trips);
  RUN_TEST(test_multiple_issues_are_comma_separated);
  RUN_TEST(test_frozen_report_refuses_new_issues);
  RUN_TEST(test_overflow_is_counted_not_hidden);
  RUN_TEST(test_quotes_and_backslashes_cannot_break_the_json);
  RUN_TEST(test_control_characters_are_dropped);
  RUN_TEST(test_oversized_strings_are_truncated_not_overflowed);
  RUN_TEST(test_null_arguments_do_not_crash);
  RUN_TEST(test_short_buffer_reports_needed_length);
  return UNITY_END();
}
