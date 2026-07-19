#include <unity.h>
#include <cstring>
#include <cstdio>
#include <string>
#include "state_json.h"

using namespace autolee;

// Read the shared JSON contract example, stripping a single trailing newline so
// it can be compared byte-for-byte against the (unterminated) serializer output.
// STATE_EXAMPLE_JSON_PATH is injected by host_test/CMakeLists.txt.
static std::string readContractExample() {
#ifdef STATE_EXAMPLE_JSON_PATH
  FILE *f = fopen(STATE_EXAMPLE_JSON_PATH, "rb");
  if (!f) return {};
  std::string s;
  char chunk[512];
  size_t got;
  while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) s.append(chunk, got);
  fclose(f);
  if (!s.empty() && s.back() == '\n') s.pop_back();
  return s;
#else
  return {};
#endif
}

// Must byte-for-byte equal api/schemas/state.example.json (the shared contract that
// CI validates against api/schemas/state.schema.json). The inline literal is the
// readable reference; test_literal_matches_contract_file guards it against drift
// from the actual contract file.
static const char *EXPECTED =
    "{\"version\":\"1.8\",\"state\":\"IDLE\",\"counter\":42,\"speed\":35000,\"calibrated\":true,"
    "\"rawUp\":0,\"rawDown\":10000,\"endpointUp\":0,\"endpointDown\":9500,"
    "\"upOffset\":0,\"downOffset\":-500,\"position\":0,\"sgTrip\":15,"
    "\"workZone\":5500,\"currentMa\":3500,"
    "\"profileIdx\":1,\"profileName\":\"Normal\","
    "\"profiles\":[{\"name\":\"Slow\",\"hz\":15000,\"sg\":350},"
    "{\"name\":\"Normal\",\"hz\":35000,\"sg\":15},"
    "{\"name\":\"Fast\",\"hz\":45000,\"sg\":1}],"
    "\"wifiStatus\":\"Connected\",\"wifiSSID\":\"MyNet\",\"wifiIP\":\"192.168.1.50\","
    "\"batchTarget\":100,\"batchCount\":7,\"batchActive\":true}";

static DeviceState sample() {
  return DeviceState{
      "1.8",       "IDLE",   42,
      35000,       true,     0,
      10000,       0,        9500,
      0,           -500,     0,
      15,          5500,     3500,
      1,           "Normal", {{"Slow", 15000, 350}, {"Normal", 35000, 15}, {"Fast", 45000, 1}},
      "Connected", "MyNet",  "192.168.1.50",
      100,         7,        true};
}

void setUp() {}
void tearDown() {}

void test_ssid_with_quote_and_backslash_is_escaped() {
  DeviceState s = sample();
  s.wifiSSID = "My\"Net\\Home";
  char buf[900];
  buildStateJson(s, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"wifiSSID\":\"My\\\"Net\\\\Home\""));
}

void test_matches_golden() {
  char buf[900];
  buildStateJson(sample(), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(EXPECTED, buf);
}

// The real contract check: the serializer output must equal the shared example
// file, not just an inline literal that could drift alongside a bad change.
void test_matches_contract_file() {
  std::string contract = readContractExample();
  TEST_ASSERT_TRUE_MESSAGE(!contract.empty(),
                           "could not read STATE_EXAMPLE_JSON_PATH contract file");
  char buf[900];
  buildStateJson(sample(), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(contract.c_str(), buf);
}

// Guards the inline literal against silently drifting from the contract file.
void test_literal_matches_contract_file() {
  std::string contract = readContractExample();
  TEST_ASSERT_TRUE_MESSAGE(!contract.empty(),
                           "could not read STATE_EXAMPLE_JSON_PATH contract file");
  TEST_ASSERT_EQUAL_STRING(contract.c_str(), EXPECTED);
}

void test_returns_full_length() {
  char buf[900];
  int n = buildStateJson(sample(), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_INT((int)strlen(EXPECTED), n);
}

void test_booleans_render_as_words() {
  DeviceState s = sample();
  s.calibrated = false;
  s.batchActive = false;
  char buf[900];
  buildStateJson(s, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"calibrated\":false"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"batchActive\":false"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_matches_golden);
  RUN_TEST(test_matches_contract_file);
  RUN_TEST(test_literal_matches_contract_file);
  RUN_TEST(test_returns_full_length);
  RUN_TEST(test_booleans_render_as_words);
  RUN_TEST(test_ssid_with_quote_and_backslash_is_escaped);
  return UNITY_END();
}
