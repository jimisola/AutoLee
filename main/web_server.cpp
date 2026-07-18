// ============================================================================
//  AutoLee - web_server.cpp (ESP-IDF port, via PsychicHttp)
//  Ported from src/web_server.cpp. Route-for-route match; ESPAsyncWebServer's
//  API swapped for PsychicHttp's, OTA swapped from Arduino's Update class to
//  esp_ota_ops. HTML is unchanged (main/index_html.h, extracted verbatim).
// ============================================================================
#include "web_server.h"
#include "globals.h"
#include "motion.h"
#include "tmc5160_ctrl.h"
#include "wifi_mgr.h"
#include "index_html.h"

#include <PsychicHttp.h>
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "state_json.h"
#include "endpoint_math.h"  // autolee::clamp_i32

static const char *TAG = "web_server";

static PsychicHttpServer server;
static PsychicEventSource events;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_ota_partition = nullptr;

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// ==========================================================================
//  STATE JSON BUILDER
// ==========================================================================
static std::string buildStateJSON() {
  const char *wfStat =
      wifi_mgr::isConnected() ? "Connected" : (wifi_mgr::isApMode() ? "AP Mode" : "Disconnected");
  std::string wfSSID =
      (wifi_mgr::isConnected() || wifi_mgr::isApMode()) ? wifi_mgr::ssid() : "-";
  std::string wfIP =
      (wifi_mgr::isConnected() || wifi_mgr::isApMode()) ? wifi_mgr::ipAddress() : "-";

  const char *stateStr = runState == RUNNING       ? "RUNNING"
                         : runState == STOPPING    ? "STOPPING"
                         : runState == CALIBRATING ? "CALIBRATING"
                         : runState == STALLED     ? "STALLED"
                         : runState == HOMING      ? "HOMING"
                                                   : "IDLE";

  autolee::DeviceState st{};
  st.version = FW_VERSION;
  st.state = stateStr;
  st.counter = counter;
  st.speed = ui_speed_hz;
  st.calibrated = endpointsCalibrated;
  st.rawUp = rawUp;
  st.rawDown = rawDown;
  st.endpointUp = endpointUp;
  st.endpointDown = endpointDown;
  st.upOffset = upOffsetSteps;
  st.downOffset = downOffsetSteps;
  st.position = 0;  // filled by stepper::getCurrentPosition() below once linked in main loop
  st.sgTrip = RUN_SG_TRIP;
  st.workZone = SG_WORK_ZONE_STEPS;
  st.currentMa = RUN_CURRENT_MA;
  st.profileIdx = activeProfile;
  st.profileName = profiles[activeProfile].name;
  for (int i = 0; i < 3; i++) {
    st.profiles[i] = {profiles[i].name, profiles[i].speed_hz, profiles[i].sg_trip};
  }
  st.wifiStatus = wfStat;
  st.wifiSSID = wfSSID.c_str();
  st.wifiIP = wfIP.c_str();
  st.batchTarget = batchTarget;
  st.batchCount = batchCount;
  st.batchActive = batchActive;

  char buf[768];
  autolee::buildStateJson(st, buf, sizeof(buf));
  return buf;
}

// ==========================================================================
//  WiFi setup page (captive portal)
// ==========================================================================
static const char WIFI_CSS[] =
    "body{font-family:-apple-system,sans-serif;background:#111;color:#eee;padding:20px;}"
    "h2{color:#7cf;}"
    "input,select{width:100%;padding:12px;margin:6px 0;border-radius:8px;"
    "border:1px solid #444;background:#222;color:#fff;box-sizing:border-box;}"
    "button{width:100%;padding:12px;margin-top:10px;border:none;border-radius:8px;"
    "color:#fff;font-size:16px;cursor:pointer;}"
    ".btnSave{background:#28a745;}.btnClear{background:#c0392b;}"
    ".box{max-width:420px;margin:auto;background:#1b1b1b;padding:20px;border-radius:12px;}"
    "label{display:block;margin-top:10px;font-size:14px;color:#aaa;}";

static std::string wifiConfigPage() {
  std::string html;
  html +=
      "<!DOCTYPE html><html><head><meta name='viewport' "
      "content='width=device-width,initial-scale=1'>";
  html += "<title>AutoLee WiFi Setup</title><style>";
  html += WIFI_CSS;
  html += "</style></head><body><div class='box'>";
  html += "<h2>AutoLee WiFi Setup</h2>";
  html += "<p style='color:#aaa;font-size:13px;'>by K.L Design</p>";
  html += "<form method='POST' action='/save'>";
  html += "<label>Select Network</label><select name='ssid_select'>" +
          wifi_mgr::scannedOptionsHtml() + "</select>";
  html +=
      "<label>Or type SSID manually</label><input name='ssid_manual' placeholder='SSID "
      "(optional)'>";
  html +=
      "<label>Password</label><input name='pass' type='password' placeholder='WiFi "
      "password'>";
  html += "<button class='btnSave' type='submit'>Save &amp; Reboot</button></form>";
  html +=
      "<form method='POST' action='/clear'><button class='btnClear' "
      "type='submit'>Clear Saved WiFi</button></form>";
  html += "</div></body></html>";
  return html;
}

static esp_err_t redirectToRoot(PsychicRequest *req, PsychicResponse *res) {
  res->setCode(302);
  res->addHeader("Location", "/");
  return res->send();
}

// ==========================================================================
//  OTA (esp_ota_ops, replacing Arduino's Update class)
// ==========================================================================
static esp_err_t handleOtaUpload(PsychicRequest *, const char *filename, uint64_t index,
                                 uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    ESP_LOGI(TAG, "OTA: upload '%s'", filename);
    if (runState == RUNNING) requestGracefulStop();
    s_ota_partition = esp_ota_get_next_update_partition(nullptr);
    if (!s_ota_partition ||
        esp_ota_begin(s_ota_partition, OTA_SIZE_UNKNOWN, &s_ota_handle) != ESP_OK) {
      ESP_LOGE(TAG, "OTA: begin failed");
      return ESP_FAIL;
    }
  }
  if (s_ota_handle && esp_ota_write(s_ota_handle, data, len) != ESP_OK) {
    ESP_LOGE(TAG, "OTA: write failed");
    return ESP_FAIL;
  }
  if (final) {
    if (esp_ota_end(s_ota_handle) == ESP_OK &&
        esp_ota_set_boot_partition(s_ota_partition) == ESP_OK) {
      ESP_LOGI(TAG, "OTA: success, %llu bytes", index + len);
      rebootRequested = true;
      rebootRequestMs = millis();
    } else {
      ESP_LOGE(TAG, "OTA: end/set-boot-partition failed");
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

// ==========================================================================
void setupWebServer() {
  server.config.max_uri_handlers = 24;

  server.on("/", HTTP_GET, [](PsychicRequest *req, PsychicResponse *res) {
    if (wifi_mgr::isApMode() && !wifi_mgr::isConnected()) {
      res->setCode(200);
      res->setContentType("text/html");
      res->setContent(wifiConfigPage().c_str());
    } else {
      res->setCode(200);
      res->setContentType("text/html");
      res->setContent(INDEX_HTML);
    }
    return res->send();
  });

  server.on("/save", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    std::string ss = req->getParam("ssid_select", "");
    std::string sm = req->getParam("ssid_manual", "");
    std::string pw = req->getParam("pass", "");
    std::string finalSSID = !sm.empty() ? sm : ss;
    if (finalSSID.empty()) {
      res->setCode(400);
      res->setContentType("text/html");
      res->setContent(
          "<html><body style='font-family:sans-serif;text-align:center;padding:40px;"
          "background:#111;color:#eee;'><h2>Missing SSID</h2><p><a href='/' "
          "style='color:#7cf;'>Go back</a></p></body></html>");
      return res->send();
    }
    wifi_mgr::saveCredentials(finalSSID, pw);
    res->setCode(200);
    res->setContentType("text/html");
    res->setContent(
        "<html><body style='font-family:sans-serif;text-align:center;padding:40px;"
        "background:#111;color:#eee;'><h2 style='color:#28a745;'>Saved!</h2>"
        "<p>Rebooting...</p></body></html>");
    rebootRequested = true;
    rebootRequestMs = millis();
    return res->send();
  });

  server.on("/clear", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    wifi_mgr::clearCredentials();
    res->setCode(200);
    res->setContentType("text/html");
    res->setContent(
        "<html><body style='font-family:sans-serif;text-align:center;padding:40px;"
        "background:#111;color:#eee;'><h2>WiFi Cleared</h2><p>Rebooting...</p></body></html>");
    rebootRequested = true;
    rebootRequestMs = millis();
    return res->send();
  });

  events.onOpen([](PsychicEventSourceClient *client) {
    std::string state = buildStateJSON();
    client->send(state.c_str(), nullptr, millis(), 500);
  });
  server.on("/api/v1/events", &events);

  server.on("/api/v1/state", HTTP_GET, [](PsychicRequest *req, PsychicResponse *res) {
    std::string state = buildStateJSON();
    return res->send(200, "application/json", state.c_str());
  });

  server.on("/api/v1/toggle_run", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (runState == IDLE) {
      startRunBetweenEndpoints();
    } else if (runState == RUNNING) {
      requestGracefulStop();
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/profile", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("idx")) {
      uint8_t idx = (uint8_t)atoi(req->getParam("idx", "0"));
      if (idx < NUM_PROFILES) setActiveProfile(idx);
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/current", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("ma")) {
      int ma = atoi(req->getParam("ma", "0"));
      if (ma < RUN_CURRENT_MIN) ma = RUN_CURRENT_MIN;
      if (ma > RUN_CURRENT_MAX) ma = RUN_CURRENT_MAX;
      RUN_CURRENT_MA = (uint16_t)ma;
      tmc5160::rms_current(RUN_CURRENT_MA);
      webLog("Current set to %u mA", RUN_CURRENT_MA);
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/endpoint", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("which") && req->hasParam("delta") && endpointsCalibrated) {
      std::string w = req->getParam("which", "");
      int32_t d = atoi(req->getParam("delta", "0"));
      if (w == "up")
        upOffsetSteps = autolee::clamp_i32(upOffsetSteps + d, OFFSET_MIN, OFFSET_MAX);
      else
        downOffsetSteps = autolee::clamp_i32(downOffsetSteps + d, OFFSET_MIN, OFFSET_MAX);
      recomputeEffectiveEndpoints();
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/sg_trip", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    uint8_t tgt = activeProfile;
    if (req->hasParam("profile")) {
      uint8_t p = (uint8_t)atoi(req->getParam("profile", "0"));
      if (p < NUM_PROFILES) tgt = p;
    }
    if (req->hasParam("value")) {
      int32_t v = atoi(req->getParam("value", "0"));
      if (v < RUN_SG_TRIP_MIN) v = RUN_SG_TRIP_MIN;
      if (v > RUN_SG_TRIP_MAX) v = RUN_SG_TRIP_MAX;
      profiles[tgt].sg_trip = (uint16_t)v;
    } else if (req->hasParam("delta")) {
      int32_t v = (int32_t)profiles[tgt].sg_trip + atoi(req->getParam("delta", "0"));
      if (v < RUN_SG_TRIP_MIN) v = RUN_SG_TRIP_MIN;
      if (v > RUN_SG_TRIP_MAX) v = RUN_SG_TRIP_MAX;
      profiles[tgt].sg_trip = (uint16_t)v;
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/work_zone", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("delta")) {
      int32_t v = SG_WORK_ZONE_STEPS + atoi(req->getParam("delta", "0"));
      if (v < SG_WORK_ZONE_MIN) v = SG_WORK_ZONE_MIN;
      if (v > SG_WORK_ZONE_MAX) v = SG_WORK_ZONE_MAX;
      SG_WORK_ZONE_STEPS = v;
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/batch", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("delta")) {
      int32_t v = batchTarget + atoi(req->getParam("delta", "0"));
      batchTarget = (v < 0) ? 0 : (v > 9999 ? 9999 : v);
    }
    if (req->hasParam("action")) {
      std::string a = req->getParam("action", "");
      if (a == "start" && batchTarget > 0 && runState == IDLE && endpointsCalibrated) {
        batchCount = 0;
        batchActive = true;
        startRunBetweenEndpoints();
      } else if (a == "clear") {
        batchTarget = 0;
        batchCount = 0;
        batchActive = false;
      }
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/action", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("do")) {
      std::string action = req->getParam("do", "");
      if (action == "calibrate" && runState == IDLE) {
        webCalRequested = true;
      } else if (action == "return_home" && runState == STALLED) {
        webHomeRequested = true;
      } else if (action == "reset_counter") {
        counter = 0;
      }
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/wifi", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("ssid")) {
      std::string ssid = req->getParam("ssid", "");
      std::string pass = req->getParam("pass", "");
      wifi_mgr::saveCredentials(ssid, pass);
      rebootRequested = true;
      rebootRequestMs = millis();
      return res->send(200, "text/plain", "saved");
    }
    return res->send(400, "text/plain", "ssid required");
  });

  server.on("/api/v1/wifi_reset", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    wifi_mgr::clearCredentials();
    rebootRequested = true;
    rebootRequestMs = millis();
    return res->send(200, "text/plain", "cleared");
  });

  server.on("/api/v1/log_clear", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    g_log = autolee::LogRing<LOG_LINES, LOG_LINE_LEN>();
    logSentSerial = 0;
    return res->send(200, "text/plain", "ok");
  });

  // OTA firmware upload. PsychicUploadHandler streams the body to
  // handleOtaUpload() and sends the HTTP response itself (200 "Upload
  // Successful." if the callback returned ESP_OK throughout, 500
  // otherwise) - rebootRequested is set from inside the callback's
  // `final` branch, since that's where success/failure is known.
  static PsychicUploadHandler ota_upload;
  ota_upload.onUpload(handleOtaUpload);
  server.on("/api/v1/ota", HTTP_POST, &ota_upload);

  // Captive portal probe endpoints redirect to root
  if (wifi_mgr::isApMode()) {
    static const char *probes[] = {"/generate_204",
                                   "/gen_204",
                                   "/hotspot-detect.html",
                                   "/library/test/success.html",
                                   "/ncsi.txt",
                                   "/connecttest.txt",
                                   "/fwlink"};
    for (auto p : probes) server.on(p, HTTP_GET, redirectToRoot);
    server.onNotFound(redirectToRoot);
  }

  server.begin();
  ESP_LOGI(TAG, "Web server started on port 80");
}

void handleWebCalibration() {
  if (!webCalRequested) return;
  webCalRequested = false;
  if (runState != IDLE) return;
  calibrateEndpointsSensorless();
  recomputeEffectiveEndpoints();
}

void handleWebHome() {
  if (!webHomeRequested) return;
  webHomeRequested = false;
  if (runState != STALLED) return;
  safeCreepHome();
}

static uint32_t s_lastSSEMs = 0;

void broadcastState() {
  uint32_t now = millis();
  if ((now - s_lastSSEMs) < SSE_INTERVAL_MS) return;
  s_lastSSEMs = now;

  std::string state = buildStateJSON();
  events.send(state.c_str(), nullptr, millis());

  uint32_t serial = g_log.serial();
  if (serial > logSentSerial) {
    uint32_t pending = serial - logSentSerial;
    if (pending > LOG_LINES) pending = LOG_LINES;
    if (pending > 20) pending = 20;

    std::string logJson = "{\"log\":[";
    uint16_t size = g_log.size();
    uint16_t start = size - (uint16_t)pending;
    for (uint16_t i = 0; i < (uint16_t)pending; i++) {
      if (i > 0) logJson += ',';
      logJson += '"';
      for (const char *p = g_log.at(start + i); *p; p++) {
        if (*p == '"')
          logJson += "\\\"";
        else if (*p == '\\')
          logJson += "\\\\";
        else
          logJson += *p;
      }
      logJson += '"';
    }
    logJson += "]}";
    events.send(logJson.c_str(), "log", millis());
    logSentSerial = serial;
  }
}
