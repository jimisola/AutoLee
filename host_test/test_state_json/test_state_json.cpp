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
    "{\"version\":\"1.8\",\"state\":\"IDLE\",\"defaultPassword\":false,\"counter\":42,"
    "\"speed\":30000,\"calibrated\":true,"
    "\"positionStale\":false,"
    "\"rawUp\":0,\"rawDown\":10000,\"endpointUp\":0,\"endpointDown\":9500,"
    "\"upOffset\":0,\"downOffset\":-500,\"position\":0,\"sgTrip\":15,"
    "\"workZone\":5500,\"currentMa\":3500,"
    "\"profileIdx\":1,\"profileName\":\"Normal\","
    "\"profiles\":[{\"name\":\"Slow\",\"hz\":15000,\"sg\":41,\"floor\":false},"
    "{\"name\":\"Normal\",\"hz\":30000,\"sg\":15,\"floor\":false},"
    "{\"name\":\"Fast\",\"hz\":40000,\"sg\":2,\"floor\":true}],"
    "\"wifiStatus\":\"Connected\",\"wifiSSID\":\"MyNet\",\"wifiIP\":\"192.168.1.50\","
    "\"wifiHostname\":\"autolee.local\","
    "\"batchTarget\":100,\"batchCount\":7,\"batchActive\":true,"
    "\"sgSetUp\":true,\"sgCalCurrentMa\":3500,\"autoSgActive\":false,\"autoSgProfile\":0,"
    "\"autoSgStroke\":0,\"autoSgStrokes\":12}";

static DeviceState sample() {
  DeviceState s{};
  s.version = "1.8";
  s.state = "IDLE";
  s.defaultPassword = false;
  s.counter = 42;
  s.speed = 30000;
  s.calibrated = true;
  s.positionStale = false;
  s.rawUp = 0;
  s.rawDown = 10000;
  s.endpointUp = 0;
  s.endpointDown = 9500;
  s.upOffset = 0;
  s.downOffset = -500;
  s.position = 0;
  s.sgTrip = 15;
  s.workZone = 5500;
  s.currentMa = 3500;
  s.profileIdx = 1;
  s.profileName = "Normal";
  s.profiles[0] = {"Slow", 15000, 41, false};
  s.profiles[1] = {"Normal", 30000, 15, false};
  s.profiles[2] = {"Fast", 40000, 2, true};
  s.wifiStatus = "Connected";
  s.wifiSSID = "MyNet";
  s.wifiIP = "192.168.1.50";
  s.wifiHostname = "autolee.local";
  s.batchTarget = 100;
  s.batchCount = 7;
  s.batchActive = true;
  s.sgSetUp = true;
  s.sgCalCurrentMa = 3500;
  s.autoSgActive = false;
  s.autoSgProfile = 0;
  s.autoSgStroke = 0;
  s.autoSgStrokes = 12;
  return s;
}

void setUp() {}
void tearDown() {}

void test_ssid_with_quote_and_backslash_is_escaped() {
  DeviceState s = sample();
  s.wifiSSID = "My\"Net\\Home";
  char buf[1024];
  buildStateJson(s, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"wifiSSID\":\"My\\\"Net\\\\Home\""));
}

void test_matches_golden() {
  char buf[1024];
  buildStateJson(sample(), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(EXPECTED, buf);
}

// The real contract check: the serializer output must equal the shared example
// file, not just an inline literal that could drift alongside a bad change.
void test_matches_contract_file() {
  std::string contract = readContractExample();
  TEST_ASSERT_TRUE_MESSAGE(!contract.empty(),
                           "could not read STATE_EXAMPLE_JSON_PATH contract file");
  char buf[1024];
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
  char buf[1024];
  int n = buildStateJson(sample(), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_INT((int)strlen(EXPECTED), n);
}

void test_booleans_render_as_words() {
  DeviceState s = sample();
  s.calibrated = false;
  s.batchActive = false;
  s.positionStale = true;
  s.defaultPassword = true;
  char buf[1024];
  buildStateJson(s, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"calibrated\":false"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"positionStale\":true"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"batchActive\":false"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"defaultPassword\":true"));
}

// The firmware serializes into a fixed 1024-byte buffer (web_server.cpp) and
// sends a stub on truncation.
void test_worst_case_fits_the_firmware_buffer() {
  DeviceState s = sample();
  s.version = "2.10.100-1000-gabcdef0123-dirty";  // 31 chars, as web_server.cpp assumes
  s.state = "CALIBRATING";
  s.wifiSSID =
      "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"";  // 32 escaped bytes
  s.wifiIP = "255.255.255.255";
  s.wifiHostname = "autolee.local";
  s.wifiStatus = "Disconnected";
  s.counter = s.batchTarget = s.batchCount = -2147483647L;
  s.rawUp = s.rawDown = s.endpointUp = s.endpointDown = s.position = -2147483647L;
  s.upOffset = s.downOffset = s.workZone = -2147483647;
  for (auto &p : s.profiles) {
    p.hz = 4294967295u;
    p.sg = 65535;
  }
  s.sgTrip = s.currentMa = s.sgCalCurrentMa = s.autoSgStroke = s.autoSgStrokes = 65535;
  char buf[1024];
  const int n = buildStateJson(s, buf, sizeof(buf));
  // Every field at its type's limit (965 bytes today) - past anything the
  // firmware can report, so real payloads have more headroom than this.
  TEST_ASSERT_TRUE_MESSAGE(n < 1024, "worst-case state JSON overflows the firmware buffer");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_matches_golden);
  RUN_TEST(test_matches_contract_file);
  RUN_TEST(test_literal_matches_contract_file);
  RUN_TEST(test_returns_full_length);
  RUN_TEST(test_booleans_render_as_words);
  RUN_TEST(test_ssid_with_quote_and_backslash_is_escaped);
  RUN_TEST(test_worst_case_fits_the_firmware_buffer);
  return UNITY_END();
}
