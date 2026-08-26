#include "wifi_mgr.h"

#include <cstring>
#include <vector>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "dns_server.h"

#include "config.h"     // DEFAULT_AP_SSID, WIFI_CONNECT_TIMEOUT_MS
#include "wifi_scan.h"  // autolee::Survey, strongest_per_ssid()
#include "globals.h"    // webLog(), uiRepaintRequested
#include "ui_touch.h"

namespace wifi_mgr {

static const char *TAG = "wifi_mgr";
static const char *kNvsNamespace = "autolee";

static EventGroupHandle_t s_wifi_event_group;
// Only a success bit: there is no failure event to signal. A disconnect is
// always retried (see wifi_event_handler), so the sole failure path is
// connect_sta()'s wait timing out without GOT_IP. A kFailBit existed here but
// was never set anywhere, which made the wait on it purely decorative.
static constexpr int kConnectedBit = BIT0;

// Latched the first time the device ever reaches GOT_IP on a real network, and
// never cleared afterwards (not even by clearCredentials() - see there). The web
// auth policy hangs off this: until the rig has been on a network it is
// physical-presence-only and needs no web password at all; once it has, a
// password change is required. A latch rather than a live isConnected() check so
// a router outage - which drops the device back to its own AP with the STA
// credentials still stored - cannot silently reopen an already-networked rig.
static const char *kNvsJoinedKey = "joined";
static bool s_ever_joined = false;

static const char *kNvsApKeyKey = "apkey";
// Per-device WPA2 key for the setup AP. Generated once and persisted, so it's
// stable across reboots and WiFi resets (only an NVS erase rotates it) - it's a
// device-identity value, like a router's sticker password, shown on the LCD +
// QR. 12 chars from an unambiguous charset (no 0/O/1/I/L) so it's legible if
// typed and comfortably above WPA2's 8-char minimum.
static std::string s_ap_key;

// Handle for the captive-portal DNS responder, non-null only in AP mode. Kept
// (rather than discarded at start_dns_server()) so stopForReboot() can shut its
// task down before the chip resets - see there.
static dns_server_handle_t s_dns_handle = nullptr;

static bool s_connected = false;
// The single gate on auto-reconnect: while we're in captive-portal AP fallback
// the STA interface exists only so esp_wifi_scan_start() works and must never
// reconnect (see wifi_event_handler). Outside AP mode - both during the initial
// connect and on an established link - a disconnect is always retried, and
// connect_sta()'s timeout is the only failure path.
static bool s_ap_mode = false;
// True while a live switch/reset task (switch_task / reset_task) is running -
// the in-flight guard serializing them. Claimed ONLY via claimTransition():
// a bare test-then-set would let the HTTP task, the LVGL task and sse_task
// race each other into two concurrent transitions.
static volatile bool s_switching = false;
static portMUX_TYPE s_switch_mux = portMUX_INITIALIZER_UNLOCKED;

static bool claimTransition() {
  bool claimed = false;
  portENTER_CRITICAL(&s_switch_mux);
  if (!s_switching) {
    s_switching = true;
    claimed = true;
  }
  portEXIT_CRITICAL(&s_switch_mux);
  return claimed;
}
// Retry override for wifi_event_handler, set ONLY while switch_task is
// actively trying to join: STA disconnects must then be retried even though
// s_ap_mode may still be true (the setup AP stays up until the join
// succeeds). Deliberately separate from s_switching: reset_task also runs
// with s_switching set, but a reset must NOT retry the STA - the first
// self-test run proved the difference on hardware, when the disconnect
// issued by the reset got auto-retried and the retry raced startSetupAp()'s
// empty-config write ("sta is connecting, cannot set config").
static volatile bool s_sta_retry = false;
// Retry budget for the INITIAL connect. A transient STA_DISCONNECTED during
// association (common, especially on a marginal/mesh AP) must not immediately
// drop us to the captive-portal AP - otherwise we can end up connected to the
// home network AND running the setup AP at once. See wifi_event_handler's
// STA_DISCONNECTED case: we retry unconditionally and let connect_sta()'s
// timeout be the sole failure path.
static esp_netif_t *s_sta_netif = nullptr;
static esp_netif_t *s_ap_netif = nullptr;
static std::string s_scanned_html;
static std::string s_connected_ssid;

// Defined below, but called from the event handler above it.
static void markEverJoined();
// Defined below start(), which calls it.
static void startSetupAp(bool wifi_already_started);

static void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    // Once we've committed to AP-fallback mode, the STA interface is only
    // enabled so esp_wifi_scan_start() works (see the APSTA comment in
    // start()) - it must NOT try to connect. Otherwise a stale STA config
    // still sitting in RAM from the failed initial attempt gets retried in
    // the background, and if that association eventually succeeds we end up
    // connected to the home network AND running the setup AP at once. That
    // was the APSTA bug: the STA side would silently reconnect with no
    // captive-portal flow at all.
    //
    // The condition, piece by piece: outside any transition (s_switching
    // false), retry whenever we are not in AP mode - the established-link
    // behavior. DURING a transition, retry only when switch_task has
    // explicitly enabled it (s_sta_retry) for its join attempt: the setup
    // phase of a switch (disconnect old link, apply new config) and the whole
    // of a reset must run with the handler suppressed, or its auto-reconnect
    // races the config change with the OLD credentials - the "sta is
    // connecting, cannot set config" failure the first hardware self-test
    // caught.
    if ((!s_ap_mode && !s_switching) || s_sta_retry) esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    // Clear the flag so isConnected() stops lying the moment the link drops.
    s_connected = false;
    // In both the established-link and initial-connect cases, just reconnect.
    // The initial connect used to bail to the AP on the first disconnect (or
    // after a small retry budget), but a flaky association drops several times
    // before DHCP completes - exhausting the budget and wrongly starting the
    // captive-portal AP even though the STA then connects. So retry
    // unconditionally (but only while we haven't given up and started the AP -
    // see the WIFI_EVENT_STA_START case above) and let connect_sta()'s timeout
    // be the ONLY failure path: GOT_IP within the window => success
    // (kConnectedBit), otherwise the wait times out and we fall back to the
    // AP. The supplicant rate-limits these, so this doesn't tight-loop.
    // Same condition as STA_START above, for the same reasons - in particular
    // the !s_switching arm: switch_task's own initial disconnect (issued
    // BEFORE the new config is applied) must not be auto-retried with the old
    // credentials.
    if ((!s_ap_mode && !s_switching) || s_sta_retry) esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    (void)data;
    // (Re)connected - restore the flag so a recovered link reads as connected.
    s_connected = true;
    markEverJoined();
    xEventGroupSetBits(s_wifi_event_group, kConnectedBit);
    // Every blank-panel report has clustered around WiFi lifecycle moments
    // (see sse_task in app_main.cpp) - a reconnect after an outage is one, so
    // queue a repaint. Just a flag: this runs on the WiFi event task, which
    // must never block on the LVGL lock.
    uiRepaintRequested = true;
  }
}

bool hasEverJoined() {
  return s_ever_joined;
}

static void loadEverJoined() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return;
  uint8_t raw = 0;
  if (nvs_get_u8(h, kNvsJoinedKey, &raw) == ESP_OK) s_ever_joined = (raw != 0);
  nvs_close(h);
}

// Called from the GOT_IP event handler, so it must stay cheap on the repeat
// path: the in-RAM flag short-circuits every reconnect, leaving exactly one
// flash write in the device's lifetime.
static void markEverJoined() {
  if (s_ever_joined) return;
  s_ever_joined = true;
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  if (nvs_set_u8(h, kNvsJoinedKey, 1) == ESP_OK) nvs_commit(h);
  nvs_close(h);
  ESP_LOGW(TAG, "First join to a network - a web password change is now required");
}

static bool load_credentials(std::string &ssid, std::string &pass) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return false;
  char buf[65] = {0};
  size_t len = sizeof(buf);
  bool have_ssid = nvs_get_str(h, "ssid", buf, &len) == ESP_OK && buf[0] != '\0';
  if (have_ssid) ssid = buf;
  len = sizeof(buf);
  if (nvs_get_str(h, "pass", buf, &len) == ESP_OK) pass = buf;
  nvs_close(h);
  return have_ssid;
}

bool saveCredentials(const std::string &ssid, const std::string &pass) {
  // Reject over-length credentials here rather than letting connect_sta()'s
  // strncpy truncate them silently: a 33-character SSID would be stored,
  // truncated at association time, and simply fail to connect forever with
  // nothing telling the operator why.
  if (ssid.empty() || ssid.size() > WIFI_SSID_MAX_LEN) {
    ESP_LOGW(TAG, "rejecting SSID of %u bytes (max %u)", (unsigned)ssid.size(),
             (unsigned)WIFI_SSID_MAX_LEN);
    return false;
  }
  if (pass.size() > WIFI_PASS_MAX_LEN) {
    ESP_LOGW(TAG, "rejecting password of %u bytes (max %u)", (unsigned)pass.size(),
             (unsigned)WIFI_PASS_MAX_LEN);
    return false;
  }

  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  nvs_set_str(h, "ssid", ssid.c_str());
  nvs_set_str(h, "pass", pass.c_str());
  nvs_commit(h);
  nvs_close(h);
  return true;
}

void clearCredentials() {
  // Key-scoped, and deliberately NOT touching kNvsJoinedKey: a WiFi reset puts
  // the device back on its own AP but must not discard a web password the
  // operator already set, nor silently drop the rig back to the
  // no-password-required policy it had before it was ever networked. Only an NVS
  // erase over USB resets the latch.
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, "ssid");
  nvs_erase_key(h, "pass");
  nvs_commit(h);
  nvs_close(h);
}

// Load the persisted setup-AP key, or generate + persist one on first boot.
// Populates s_ap_key. Call after esp_wifi_init() so esp_random() is well-seeded.
static void ensureApKey() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) == ESP_OK) {
    char buf[33] = {0};
    size_t len = sizeof(buf);
    if (nvs_get_str(h, kNvsApKeyKey, buf, &len) == ESP_OK && buf[0] != '\0') {
      s_ap_key = buf;
      nvs_close(h);
      return;
    }
    nvs_close(h);
  }

  // No key yet - generate one. Unambiguous charset: no 0/O, 1/I/L.
  static const char kCharset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  static constexpr int kKeyLen = 12;
  static constexpr int kCharsetSize = sizeof(kCharset) - 1;  // drop the NUL
  uint8_t rnd[kKeyLen];
  esp_fill_random(rnd, sizeof(rnd));
  s_ap_key.resize(kKeyLen);
  for (int i = 0; i < kKeyLen; i++) s_ap_key[i] = kCharset[rnd[i] % kCharsetSize];

  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_str(h, kNvsApKeyKey, s_ap_key.c_str());
    nvs_commit(h);
    nvs_close(h);
  }
  ESP_LOGI(TAG, "generated new setup-AP key");
}

std::string apPassword() {
  return s_ap_key;
}

// Blocking scan shared by the captive portal's HTML dropdown and the
// dashboard's JSON endpoint. Returns false only on scan-start failure; an
// empty result is a successful scan of a quiet band.
static bool doScan(std::vector<wifi_ap_record_t> &records) {
  wifi_scan_config_t scan_cfg = {};
  // Both explicit rather than inherited from the zero-init: show_hidden false
  // leaves a non-advertising AP out of the radio survey entirely, not merely
  // out of the list.
  scan_cfg.show_hidden = true;
  scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) return false;
  uint16_t count = 0;
  esp_wifi_scan_get_ap_num(&count);
  records.resize(count);
  if (count > 0) {
    esp_wifi_scan_get_ap_records(&count, records.data());
    records.resize(count);
  }
  return true;
}

// The survey: every radio the driver saw, one entry per BSSID, hidden SSIDs
// kept. Both views are derived from this, so a row missing from a view is a
// property of that view rather than of the scan.
static bool surveyNetworks(autolee::Survey &out) {
  std::vector<wifi_ap_record_t> records;
  if (!doScan(records)) return false;
  out.clear();
  out.reserve(records.size());
  for (const auto &r : records) {
    autolee::ApRecord ap;
    // The driver's ssid field is a fixed buffer, not guaranteed NUL-terminated
    // when the SSID uses all 32 bytes.
    ap.ssid.assign(reinterpret_cast<const char *>(r.ssid),
                   strnlen(reinterpret_cast<const char *>(r.ssid), sizeof(r.ssid)));
    memcpy(ap.bssid, r.bssid, sizeof(ap.bssid));
    ap.channel = r.primary;
    ap.rssi = r.rssi;
    ap.secure = (r.authmode != WIFI_AUTH_OPEN);
    out.push_back(std::move(ap));
  }
  return true;
}

// Shared post-processing for BOTH pickers (captive portal dropdown, dashboard
// /api/v1/wifi/scan), so they present the same list. The collapsing rule lives
// in strongest_per_ssid() where it is unit-tested; it is a property of the
// picker, not of scanning.
static bool collectNetworks(autolee::Survey &out) {
  autolee::Survey survey;
  if (!surveyNetworks(survey)) return false;
  out = autolee::strongest_per_ssid(survey);
  return true;
}

std::string scanNetworksJson() {
  if (s_switching) return "[]";
  autolee::Survey records;
  if (!collectNetworks(records)) return "[]";
  std::string json = "[";
  bool first = true;
  for (size_t i = 0; i < records.size(); i++) {
    std::string ssid;
    for (unsigned char c : records[i].ssid) {
      // JSON string escaping: the two mandatory metacharacters plus control
      // bytes; an SSID is arbitrary bytes and " or \ in one must not be able
      // to break the array open.
      if (c == '"' || c == '\\') {
        ssid += '\\';
        ssid += (char)c;
      } else if (c < 0x20) {
        char u[8];
        snprintf(u, sizeof(u), "\\u%04x", c);
        ssid += u;
      } else {
        ssid += (char)c;
      }
    }
    // `first`, not `i`: skipped records (hidden SSIDs, mesh duplicates) mean
    // the record index no longer tracks emitted entries - keying the comma on
    // it would emit a leading comma whenever record 0 was skipped.
    if (!first) json += ",";
    first = false;
    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + std::to_string(records[i].rssi) +
            ",\"secure\":" + (records[i].secure ? "true" : "false") + "}";
  }
  json += "]";
  return json;
}

static void scan_networks() {
  s_scanned_html = "<option value=''>-- Select WiFi --</option>";
  autolee::Survey records;
  if (!collectNetworks(records)) {
    s_scanned_html += "<option value=''>Scan failed</option>";
    return;
  }
  if (records.empty()) {
    s_scanned_html += "<option value=''>No networks found</option>";
    return;
  }
  const size_t count = records.size();
  for (uint16_t i = 0; i < count; i++) {
    const std::string &ssid = records[i].ssid;
    // Minimal HTML-escaping. '&' matters and was missing here (an SSID
    // containing it produced broken markup in the dropdown) - fixed upstream in
    // Karl's v1.10.0. Note his fix had to escape '&' *first* because it used
    // sequential String::replace() calls, which would otherwise double-escape
    // the entities it just inserted; this char-by-char build is immune to that,
    // so case order below is irrelevant.
    std::string escaped;
    for (char c : ssid) {
      switch (c) {
        case '&':
          escaped += "&amp;";
          break;
        case '"':
          escaped += "&quot;";
          break;
        case '\'':
          escaped += "&#39;";
          break;
        case '<':
          escaped += "&lt;";
          break;
        case '>':
          escaped += "&gt;";
          break;
        default:
          escaped += c;
      }
    }
    const char *sec = records[i].secure ? "SEC" : "OPEN";
    s_scanned_html += "<option value=\"" + escaped + "\">" + escaped + " (" +
                      std::to_string(records[i].rssi) + " dBm " + sec + ")</option>";
  }
}

static bool connect_sta(const std::string &ssid, const std::string &pass, uint32_t timeout_ms) {
  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char *)wifi_config.sta.password, pass.c_str(), sizeof(wifi_config.sta.password) - 1);

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  xEventGroupClearBits(s_wifi_event_group, kConnectedBit);
  esp_wifi_start();

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, kConnectedBit, pdFALSE, pdFALSE,
                                         pdMS_TO_TICKS(timeout_ms));
  return (bits & kConnectedBit) != 0;
}

void start() {
  // Before any connect attempt, so setupWebServer() sees the persisted value
  // even on a boot that never reaches GOT_IP.
  loadEverJoined();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  s_sta_netif = esp_netif_create_default_wifi_sta();
  s_ap_netif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

  // Default storage (WIFI_STORAGE_FLASH) lets the driver persist STA config to
  // its OWN flash blob, separate from our "autolee" NVS namespace, and
  // auto-reconnect to it on WIFI_EVENT_STA_START - independent of app logic.
  // That's how the APSTA bug happened: after "Reset WiFi" cleared our
  // credentials, the driver still had the old SSID/password cached and
  // reconnected to it the moment the STA interface came up for AP-mode
  // scanning, so the device ended up on the home network AND running the
  // setup AP at once (and reconnected with no captive-portal flow at all).
  // RAM storage makes us the single source of truth: STA config only ever
  // comes from our own esp_wifi_set_config() calls, sourced from our NVS.
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  // No modem power save. The ESP-IDF default in STA mode (WIFI_PS_MIN_MODEM)
  // sleeps the radio between DTIM beacons, and on this rig that surfaced as
  // the dashboard being visibly broken: 0.1-1.5s latency jitter and periodic
  // multi-second windows where every HTTP request timed out - measured with a
  // 1Hz probe as ~45s of jittery service followed by 8+ dead seconds, i.e.
  // the Diag page "showing nothing" whenever its fetches landed in a sleep
  // window. AP mode never sleeps, which is why none of this showed during
  // captive-portal operation. This is a mains-powered machine controller: a
  // responsive control surface is worth ~60mA of idle draw.
  esp_wifi_set_ps(WIFI_PS_NONE);

  s_wifi_event_group = xEventGroupCreate();
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr,
                                      nullptr);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr,
                                      nullptr);

  std::string ssid, pass;
  if (load_credentials(ssid, pass)) {
    ESP_LOGI(TAG, "connecting to '%s'...", ssid.c_str());
    if (connect_sta(ssid, pass, WIFI_CONNECT_TIMEOUT_MS)) {
      s_connected = true;
      s_ap_mode = false;
      s_connected_ssid = ssid;
      esp_netif_ip_info_t ip_info;
      esp_netif_get_ip_info(s_sta_netif, &ip_info);
      ESP_LOGI(TAG, "connected! IP=" IPSTR, IP2STR(&ip_info.ip));
    } else {
      ESP_LOGW(TAG, "STA failed, starting captive portal");
    }
  }

  if (!s_connected) {
    startSetupAp(false);
  }
}

// Bring up the WPA2 setup AP + captive-portal DNS. `wifi_already_started` is
// false only on the boot path (start() above), where esp_wifi_start() has not
// run yet; the live paths (switch failure fallback, WiFi reset) call it with
// true and only change mode/config on the running driver.
static void startSetupAp(bool wifi_already_started) {
  // WPA2-secured AP: the per-device key is shown on the LCD + as a join QR,
  // so it's discoverable to whoever is physically at the machine but not to
  // anyone just in range. This is the AP-side pairing of the web control
  // API's digest auth.
  ensureApKey();

  // Set BEFORE esp_wifi_start() below (which is what fires
  // WIFI_EVENT_STA_START): closes the race where the event could otherwise
  // be handled while s_ap_mode still reads false, letting the STA_START
  // handler call esp_wifi_connect() once more before we've committed to AP
  // mode.
  s_ap_mode = true;

  wifi_config_t ap_config = {};
  strncpy((char *)ap_config.ap.ssid, DEFAULT_AP_SSID, sizeof(ap_config.ap.ssid) - 1);
  ap_config.ap.ssid_len = strlen(DEFAULT_AP_SSID);
  ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  strncpy((char *)ap_config.ap.password, s_ap_key.c_str(), sizeof(ap_config.ap.password) - 1);
  ap_config.ap.max_connection = 4;
  ap_config.ap.channel = 1;

  // APSTA, not plain AP: esp_wifi_scan_start() requires the STA interface
  // to be active - a pure-AP mode fails every scan (found via hardware
  // testing: the WiFi setup page always showed "Scan failed"). The STA
  // side here is never connected, just enabled so scanning works.
  esp_wifi_set_mode(WIFI_MODE_APSTA);

  // esp_wifi_init() loads any STA config the driver previously persisted to
  // its own flash storage into its in-RAM copy, regardless of the
  // WIFI_STORAGE_RAM call above (that only stops FUTURE writes going to
  // flash). So a stale SSID/password from a prior boot can still be sitting
  // there even when we never called connect_sta() this boot (e.g. no app
  // credentials yet) - and the moment APSTA brings the STA interface up for
  // scanning, the driver tries to associate with it. Clearing it here is
  // what actually stops that. Same reasoning on the live paths: a failed
  // switch must not leave the bad config behind to be retried in the
  // background.
  wifi_config_t empty_sta_config = {};
  esp_wifi_set_config(WIFI_IF_STA, &empty_sta_config);

  esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  if (!wifi_already_started) esp_wifi_start();

  scan_networks();

  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(s_ap_netif, &ip_info);
  ESP_LOGI(TAG, "AP: %s @ " IPSTR " (WPA2, key=%s, captive portal)", DEFAULT_AP_SSID,
           IP2STR(&ip_info.ip), s_ap_key.c_str());

  // Redirect every DNS query to us, so any device connecting to the AP
  // is prompted into the captive portal.
  // DNS_SERVER_CONFIG_SINGLE leaves dns_entry_pair::ip uninitialized (correct
  // - it is only read when if_key is NULL, and it isn't here), which
  // ESP-IDF 6.0's -Werror=missing-field-initializers rejects in C++. Scoped
  // to this one expansion rather than fixed in lib/dns_server/ beyond the
  // graceful-stop patch documented there.
  if (!s_dns_handle) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
#pragma GCC diagnostic pop
    s_dns_handle = start_dns_server(&dns_cfg);
  }

  // The AP start is the one moment the panel has been directly observed dying
  // (three camera-verified reproductions - see sse_task in app_main.cpp), so
  // queue its recovery immediately rather than waiting for the 30s sweep:
  // without this, an out-of-box rig showed its join QR up to ~12 measured
  // seconds late.
  uiRepaintRequested = true;
}

// ==========================================================================
//  Live AP<->STA transitions (no reboot)
//
//  Rebooting to change WiFi state was inherited from the Arduino firmware and
//  turned out to be this project's single richest source of field bugs: the
//  GPIO8 strapping hang (TMC_CS low at reset -> chip does not boot), the
//  vTaskDelete-inside-lwIP deadlock, and the lost initial LVGL flush were ALL
//  reboot-path bugs. Switching the running driver avoids the whole category,
//  and is also simply better for the operator: the setup AP (and their portal
//  page) stays up until the join actually succeeds.
// ==========================================================================

static void switch_task(void *) {
  std::string ssid, pass;
  if (!load_credentials(ssid, pass)) {
    webLog("WiFi", "No stored credentials to connect with");
    s_switching = false;
    vTaskDelete(nullptr);
    return;
  }
  webLog("WiFi", "Connecting to '%s'...", ssid.c_str());

  wifi_config_t cfg = {};
  strncpy((char *)cfg.sta.ssid, ssid.c_str(), sizeof(cfg.sta.ssid) - 1);
  strncpy((char *)cfg.sta.password, pass.c_str(), sizeof(cfg.sta.password) - 1);
  // Let the HTTP response that triggered this switch drain before the link is
  // touched. Found in E2E testing: from an established STA connection, the
  // disconnect below raced the tail of /api/v1/wifi/save's own response - the
  // client got the 200 status with a truncated body. One second is harmless
  // on the AP path too (no link to drop there).
  vTaskDelay(pdMS_TO_TICKS(1000));
  // Disconnect FIRST, unconditionally. Review finding H2: if the link is
  // flapping, the event handler may have an esp_wifi_connect() in flight with
  // the OLD config, and set_config then fails with "sta is connecting" - the
  // in-flight retry would associate with the old network while this task
  // reports having joined the new one. A disconnect aborts any in-flight
  // attempt; only then is setting the new config reliable. s_sta_retry is not
  // set yet, so this disconnect is not auto-retried.
  esp_wifi_disconnect();
  // set_config can still transiently fail right after a disconnect while the
  // supplicant unwinds - bounded retry rather than silent divergence.
  esp_err_t cfg_err = ESP_FAIL;
  for (int i = 0; i < 10 && cfg_err != ESP_OK; i++) {
    cfg_err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (cfg_err != ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (cfg_err != ESP_OK) {
    webLog("WiFi", "Could not apply the new WiFi config (%s) - nothing changed",
           esp_err_to_name(cfg_err));
    s_switching = false;
    ui_update_wifi_label();
    vTaskDelete(nullptr);
    return;
  }
  xEventGroupClearBits(s_wifi_event_group, kConnectedBit);
  s_sta_retry = true;  // let the event handler retry disconnects for the whole attempt
  esp_wifi_connect();

  const EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, kConnectedBit, pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

  // Success needs BOTH the bit and the right network (review finding L1): a
  // stale GOT_IP queued from the old link (e.g. a DHCP renewal) can set the
  // bit without the new association having happened. Asking the driver which
  // AP it is actually on is authoritative.
  bool joined = (bits & kConnectedBit) != 0;
  if (joined) {
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
        strncmp((const char *)ap.ssid, ssid.c_str(), sizeof(ap.ssid)) != 0) {
      webLog("WiFi", "Connected bit set but not on '%s' - treating as failure", ssid.c_str());
      joined = false;
    }
  }

  if (joined) {
    // GOT_IP already set s_connected and latched hasEverJoined().
    s_connected_ssid = ssid;
    const bool wasAp = s_ap_mode;
    s_ap_mode = false;
    if (s_dns_handle) {
      stop_dns_server(s_dns_handle);  // graceful since the lib patch - see dns_server.c
      s_dns_handle = nullptr;
    }
    // Dropping to pure STA is what actually tears the setup AP down. Done
    // LAST, so an operator watching the portal page keeps their connection
    // until the outcome is real.
    if (wasAp) esp_wifi_set_mode(WIFI_MODE_STA);
    // Established-link reconnects are covered by !s_ap_mode from here on.
    s_sta_retry = false;
    webLog("WiFi", "Joined '%s' (IP %s)%s", ssid.c_str(), ipAddress().c_str(),
           wasAp ? " - setup AP stopped" : "");
  } else {
    // Order matters: stop the retry override BEFORE disconnecting, or the
    // STA_DISCONNECTED our own disconnect fires would immediately retry.
    s_sta_retry = false;
    wifi_config_t empty = {};
    esp_wifi_set_config(WIFI_IF_STA, &empty);
    esp_wifi_disconnect();
    if (!s_ap_mode) {
      // The switch was started from an established STA connection and the new
      // network never materialized - the old one's config is already gone, so
      // without this the rig would be on no network at all, headless. The
      // setup AP is the recoverable state.
      startSetupAp(true);
      webLog("WiFi", "Could not join '%s' - setup AP started", ssid.c_str());
    } else {
      webLog("WiFi", "Could not join '%s' - setup AP still active, try again", ssid.c_str());
    }
  }

  s_switching = false;
  ui_update_wifi_label();
  uiRepaintRequested = true;
  vTaskDelete(nullptr);
}

bool transitionInFlight() {
  return s_switching;
}

bool startLiveSwitch() {
  if (!claimTransition()) return false;
  if (xTaskCreate(switch_task, "wifi_switch", 6144, nullptr, 3, nullptr) != pdPASS) {
    s_switching = false;
    return false;
  }
  return true;
}

static void reset_task(void *) {
  clearCredentials();
  // Before the disconnect, for the same STA_START/STA_DISCONNECTED race
  // documented in startSetupAp(): the handler must never see "not AP mode"
  // between the disconnect and the AP coming up.
  s_ap_mode = true;
  s_connected = false;
  // Unconditional, not `if (s_connected)` - review finding H1: the operator
  // resets WiFi precisely when the link is broken, i.e. when s_connected is
  // false but the event handler may have a reconnect attempt in flight with
  // the old config. If that attempt is left running it can complete AFTER the
  // setup AP is up - the documented APSTA bug, resurrected. A disconnect also
  // aborts an in-flight connect. (Retries are suppressed throughout:
  // s_switching is set and s_sta_retry is not - see wifi_event_handler.)
  esp_wifi_disconnect();
  s_connected_ssid.clear();
  startSetupAp(true);
  webLog("WiFi", "Credentials cleared - setup AP active");
  s_switching = false;
  ui_update_wifi_label();
  uiRepaintRequested = true;
  vTaskDelete(nullptr);
}

bool requestResetToSetupAp() {
  if (!claimTransition()) return false;
  if (xTaskCreate(reset_task, "wifi_reset", 6144, nullptr, 3, nullptr) != pdPASS) {
    s_switching = false;
    return false;
  }
  return true;
}

void stopForReboot() {
  // Bring the network down in an orderly way before esp_restart().
  //
  // esp_restart() does not unwind anything: it halts the other core, disables
  // interrupts and resets. Doing that with the WiFi driver mid-operation - and
  // in AP mode, with the DNS responder task blocked in recvfrom() on a socket
  // that is about to vanish - is the documented way to get a reset that hangs
  // instead of completing, needing a power cycle to recover.
  //
  // This matters most on exactly the path where it was reported: saving WiFi
  // credentials from the captive portal is the one reboot that happens while
  // the AP, the DNS server and an active HTTP client are all up at once. The
  // dashboard's reboots (STA mode, no DNS task) never looked as bad.
  //
  // Best-effort throughout: every failure here is still followed by the reset,
  // since refusing to reboot would be strictly worse than an unclean one.
  //
  // The DNS responder is deliberately NOT stopped, even though it is the one
  // task most obviously in the way. stop_dns_server() is a bare
  // vTaskDelete(handle->task) (lib/dns_server/dns_server.c), and that task
  // spends effectively all its time blocked inside recvfrom(). Deleting a task
  // mid-syscall inside lwIP never releases the core lock it is holding, so the
  // esp_wifi_stop() below - which needs that lock - then blocks forever, in
  // pump_task, and esp_restart() is never reached at all. That turns "reboots
  // uncleanly" into "does not reboot", which is strictly worse and was
  // observed on hardware. The responder dies with the reset like everything
  // else; nothing needs it shut down in an orderly way first.
  esp_wifi_disconnect();
  esp_wifi_stop();
}

bool isConnected() {
  return s_connected;
}
bool isApMode() {
  return s_ap_mode;
}

std::string ipAddress() {
  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(s_ap_mode ? s_ap_netif : s_sta_netif, &ip_info);
  char buf[16];
  esp_ip4addr_ntoa(&ip_info.ip, buf, sizeof(buf));
  return buf;
}

std::string scannedOptionsHtml() {
  return s_scanned_html;
}

void rescanForPortal() {
  if (!s_switching) scan_networks();
}

std::string ssid() {
  return s_ap_mode ? DEFAULT_AP_SSID : s_connected_ssid;
}

}  // namespace wifi_mgr
