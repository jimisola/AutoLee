#include "wifi_mgr.h"

#include <cstring>
#include <vector>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
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

static bool s_connected = false;
static bool s_ap_mode = false;
// True only once an initial STA connection has succeeded and we're committed to
// staying on the network. Gates auto-reconnect so it can't fire during the
// initial connect attempt (which must be allowed to time out and fall back to
// the captive-portal AP) or in AP fallback mode.
static bool s_sta_active = false;
static esp_netif_t *s_sta_netif = nullptr;
static esp_netif_t *s_ap_netif = nullptr;
static std::string s_scanned_html;
static std::string s_connected_ssid;

static void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    // Clear the flag so isConnected() stops lying the moment the link drops.
    s_connected = false;
    if (s_sta_active) {
      // Established link dropped - keep trying to get back on. The supplicant
      // spaces these internally, so this doesn't tight-loop; no explicit
      // esp_timer backoff (kept out deliberately - unverifiable without WiFi
      // hardware access this session, and the driver already rate-limits).
      esp_wifi_connect();
    } else {
      // Still in the initial connect attempt (or AP fallback's idle STA):
      // let connect_sta() time out on kFailBit and fall back to the AP.
      xEventGroupSetBits(s_wifi_event_group, kFailBit);
    }
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
    // Open AP (no password - matches the original's "easier to connect").
    wifi_config_t ap_config = {};
    strncpy((char *)ap_config.ap.ssid, DEFAULT_AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(DEFAULT_AP_SSID);
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    ap_config.ap.channel = 1;

    // APSTA, not plain AP: esp_wifi_scan_start() requires the STA interface
    // to be active - a pure-AP mode fails every scan (found via hardware
    // testing: the WiFi setup page always showed "Scan failed"). The STA
    // side here is never connected, just enabled so scanning works.
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    s_ap_mode = true;

    scan_networks();

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(s_ap_netif, &ip_info);
    ESP_LOGI(TAG, "AP: %s @ " IPSTR " (captive portal)", DEFAULT_AP_SSID, IP2STR(&ip_info.ip));

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
