#include <unity.h>
#include <cstring>
#include "log_ring.h"

using namespace autolee;

void setUp() {}
void tearDown() {}

void test_push_below_capacity() {
  LogRing<3, 8> ring;
  ring.push("a");
  ring.push("b");
  TEST_ASSERT_EQUAL_UINT32(2, ring.serial());
  TEST_ASSERT_EQUAL_UINT16(2, ring.size());
  TEST_ASSERT_EQUAL_STRING("a", ring.at(0));
  TEST_ASSERT_EQUAL_STRING("b", ring.at(1));
}

void test_truncates_long_lines() {
  LogRing<2, 4> ring;  // max 3 chars + NUL
  ring.push("abcdef");
  TEST_ASSERT_EQUAL_STRING("abc", ring.at(0));
}

void test_wraparound_keeps_newest() {
  LogRing<3, 8> ring;
  ring.push("1");
  ring.push("2");
  ring.push("3");
  ring.push("4");  // evicts "1"
  ring.push("5");  // evicts "2"
  TEST_ASSERT_EQUAL_UINT32(5, ring.serial());
  TEST_ASSERT_EQUAL_UINT16(3, ring.size());
  TEST_ASSERT_EQUAL_STRING("3", ring.at(0));  // oldest retained
  TEST_ASSERT_EQUAL_STRING("4", ring.at(1));
  TEST_ASSERT_EQUAL_STRING("5", ring.at(2));  // newest
}

// copyLine() must agree with at() on which line is which, and always
// NUL-terminate - it exists so readers don't dereference at()'s raw pointer
// while another task recycles the slot.
void test_copy_line_matches_at() {
  LogRing<3, 8> ring;
  ring.push("alpha");
  ring.push("beta");
  char buf[8] = {};
  ring.copyLine(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("alpha", buf);
  ring.copyLine(1, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("beta", buf);
}

void test_copy_line_truncates_into_a_short_buffer() {
  LogRing<2, 16> ring;
  ring.push("abcdefghij");
  char buf[5] = {};
  ring.copyLine(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("abcd", buf);  // 4 chars + NUL
}

// The whole point of jsonEscapeLog over the old '"'/'\\'-only escaper: a raw
// control byte must not reach the payload verbatim, because that invalidates
// the JSON for every consumer, not just the offending line.
void test_json_escape_log_handles_quotes_and_backslashes() {
  char out[64];
  jsonEscapeLog("say \"hi\" c:\\path", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("say \\\"hi\\\" c:\\\\path", out);
}

void test_json_escape_log_uses_short_escapes_for_common_controls() {
  char out[64];
  jsonEscapeLog("a\nb\tc\rd", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("a\\nb\\tc\\rd", out);
}

void test_json_escape_log_uses_u_escapes_for_other_controls() {
  char out[64];
  const char in[] = {'a', (char)0x01, (char)0x1f, (char)0x7f, 'b', '\0'};
  jsonEscapeLog(in, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("a\\u0001\\u001f\\u007fb", out);
}

// Truncation must never split an escape sequence in half - that would itself
// be invalid JSON.
void test_json_escape_log_truncates_without_splitting_an_escape() {
  char out[6];
  jsonEscapeLog("ab\ncd", out, sizeof(out));
  // "ab" + "\\n" fits (4 chars); 'c' fits as the 5th, leaving the NUL.
  TEST_ASSERT_EQUAL_STRING("ab\\nc", out);

  char tiny[4];
  jsonEscapeLog("\n\n", tiny, sizeof(tiny));
  TEST_ASSERT_EQUAL_STRING("\\n", tiny);  // second escape doesn't fit, dropped whole
}

void test_json_escape_log_passes_plain_text_through() {
  char out[32];
  jsonEscapeLog("Motion: SG HIGH=42", out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("Motion: SG HIGH=42", out);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_push_below_capacity);
  RUN_TEST(test_truncates_long_lines);
  RUN_TEST(test_wraparound_keeps_newest);
  RUN_TEST(test_copy_line_matches_at);
  RUN_TEST(test_copy_line_truncates_into_a_short_buffer);
  RUN_TEST(test_json_escape_log_handles_quotes_and_backslashes);
  RUN_TEST(test_json_escape_log_uses_short_escapes_for_common_controls);
  RUN_TEST(test_json_escape_log_uses_u_escapes_for_other_controls);
  RUN_TEST(test_json_escape_log_truncates_without_splitting_an_escape);
  RUN_TEST(test_json_escape_log_passes_plain_text_through);
  return UNITY_END();
}
