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

#include "config.h"  // DEFAULT_AP_SSID

namespace wifi_mgr {

static const char *TAG = "wifi_mgr";
static const char *kNvsNamespace = "autolee";

static EventGroupHandle_t s_wifi_event_group;
static constexpr int kConnectedBit = BIT0;
static constexpr int kFailBit = BIT1;

static const char *kNvsApKeyKey = "apkey";
// Per-device WPA2 key for the setup AP. Generated once and persisted, so it's
// stable across reboots and WiFi resets (only an NVS erase rotates it) - it's a
// device-identity value, like a router's sticker password, shown on the LCD +
// QR. 12 chars from an unambiguous charset (no 0/O/1/I/L) so it's legible if
// typed and comfortably above WPA2's 8-char minimum.
static std::string s_ap_key;

static bool s_connected = false;
static bool s_ap_mode = false;
// True only once an initial STA connection has succeeded and we're committed to
// staying on the network. Gates auto-reconnect so it can't fire during the
// initial connect attempt (which must be allowed to time out and fall back to
// the captive-portal AP) or in AP fallback mode.
static bool s_sta_active = false;
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
    if (!s_ap_mode) esp_wifi_connect();
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
    if (!s_ap_mode) esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    (void)data;
    // (Re)connected - restore the flag so a recovered link reads as connected.
    s_connected = true;
    xEventGroupSetBits(s_wifi_event_group, kConnectedBit);
  }
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

void saveCredentials(const std::string &ssid, const std::string &pass) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, "ssid", ssid.c_str());
  nvs_set_str(h, "pass", pass.c_str());
  nvs_commit(h);
  nvs_close(h);
}

void clearCredentials() {
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

static void scan_networks() {
  s_scanned_html = "<option value=''>-- Select WiFi --</option>";
  wifi_scan_config_t scan_cfg = {};
  if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
    s_scanned_html += "<option value=''>Scan failed</option>";
    return;
  }
  uint16_t count = 0;
  esp_wifi_scan_get_ap_num(&count);
  if (count == 0) {
    s_scanned_html += "<option value=''>No networks found</option>";
    return;
  }
  std::vector<wifi_ap_record_t> records(count);
  esp_wifi_scan_get_ap_records(&count, records.data());
  for (uint16_t i = 0; i < count; i++) {
    std::string ssid(reinterpret_cast<char *>(records[i].ssid));
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
    const char *sec = records[i].authmode == WIFI_AUTH_OPEN ? "OPEN" : "SEC";
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
  xEventGroupClearBits(s_wifi_event_group, kConnectedBit | kFailBit);
  esp_wifi_start();

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, kConnectedBit | kFailBit, pdFALSE,
                                         pdFALSE, pdMS_TO_TICKS(timeout_ms));
  return (bits & kConnectedBit) != 0;
}

void start() {
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

  s_wifi_event_group = xEventGroupCreate();
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr,
                                      nullptr);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr,
                                      nullptr);

  std::string ssid, pass;
  if (load_credentials(ssid, pass)) {
    ESP_LOGI(TAG, "connecting to '%s'...", ssid.c_str());
    if (connect_sta(ssid, pass, 10000)) {
      s_connected = true;
      s_sta_active = true;  // from now on, auto-reconnect if the link drops
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
    // what actually stops that.
    wifi_config_t empty_sta_config = {};
    esp_wifi_set_config(WIFI_IF_STA, &empty_sta_config);

    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    scan_networks();

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(s_ap_netif, &ip_info);
    ESP_LOGI(TAG, "AP: %s @ " IPSTR " (WPA2, key=%s, captive portal)", DEFAULT_AP_SSID,
             IP2STR(&ip_info.ip), s_ap_key.c_str());

    // Redirect every DNS query to us, so any device connecting to the AP
    // is prompted into the captive portal.
    dns_server_config_t dns_cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    start_dns_server(&dns_cfg);
  }
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

std::string ssid() {
  return s_ap_mode ? DEFAULT_AP_SSID : s_connected_ssid;
}

}  // namespace wifi_mgr
