// ============================================================================
//  AutoLee - web_server.cpp (ESP-IDF port, via PsychicHttp)
//  Ported from src/web_server.cpp. Route-for-route match; ESPAsyncWebServer's
//  API swapped for PsychicHttp's, OTA swapped from Arduino's Update class to
//  esp_ota_ops. HTML is unchanged (main/index_html.h, extracted verbatim).
// ============================================================================
#include "web_server.h"
#include "globals.h"
#include "motion.h"
#include "motion_cmd.h"
#include "motion_state.h"
#include "settings_store.h"
#include "stepper.h"
#include "tmc5160_ctrl.h"
#include "wifi_mgr.h"
#include "index_html.h"

#include <PsychicHttp.h>
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include "esp_core_dump.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

#include "state_json.h"
#include "endpoint_math.h"  // autolee::clamp_i32

static const char *TAG = "web_server";
static const char *kNvsNamespace = "autolee";
static const char *kNvsWebPassKey = "webpass";

static PsychicHttpServer server;
static PsychicEventSource events;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_ota_partition = nullptr;

// ==========================================================================
//  WEB AUTHENTICATION (see config.h's WEB AUTHENTICATION block for the
//  rationale: Digest over plain HTTP, writes gated, reads left open)
// ==========================================================================
static AuthenticationMiddleware s_auth;

// True while the effective web password is still WEB_AUTH_DEFAULT_PASS. Set at
// boot from loadWebPassword() and kept in sync by the /api/v1/system/web_password
// handler. Read by buildStateJSON() (the "defaultPassword" field) and by the
// write-gate middleware below - see the #1c comment near s_auth.addMiddleware
// for why every state-changing route (bar the password endpoint itself) is
// refused while this is true.
static bool s_default_password_active = true;

// Current password: NVS value if one was ever saved, else the factory default.
static std::string loadWebPassword() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return WEB_AUTH_DEFAULT_PASS;
  char buf[WEB_AUTH_PASS_MAX + 1] = {0};
  size_t len = sizeof(buf);
  bool have = nvs_get_str(h, kNvsWebPassKey, buf, &len) == ESP_OK && buf[0] != '\0';
  nvs_close(h);
  return have ? std::string(buf) : std::string(WEB_AUTH_DEFAULT_PASS);
}

static bool saveWebPassword(const std::string &pass) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_str(h, kNvsWebPassKey, pass.c_str());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// ==========================================================================
//  DIAGNOSTICS (/api/v1/diagnostics/info, /api/v1/diagnostics/coredump)
// ==========================================================================
static const char *resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    case ESP_RST_EFUSE: return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default: return "UNKNOWN";
  }
}

// ==========================================================================
//  STATE JSON BUILDER
// ==========================================================================
// Called from the HTTP task and from sse_task - never from pump_task, which
// owns the motion state. One snapshot() up front (a struct copy taken under
// motion_state's spinlock) gives a self-consistent view of every motion /
// endpoint / batch / profile field; the rest of this function then works off
// that local copy, so no lock is held while the JSON is assembled and no
// field can change underneath a half-built payload.
static std::string buildStateJSON() {
  const MotionState ms = motion_state::snapshot();

  const char *wfStat =
      wifi_mgr::isConnected() ? "Connected" : (wifi_mgr::isApMode() ? "AP Mode" : "Disconnected");
  std::string wfSSID = (wifi_mgr::isConnected() || wifi_mgr::isApMode()) ? wifi_mgr::ssid() : "-";
  std::string wfIP =
      (wifi_mgr::isConnected() || wifi_mgr::isApMode()) ? wifi_mgr::ipAddress() : "-";

  const char *stateStr = ms.runState == RUNNING       ? "RUNNING"
                         : ms.runState == STOPPING    ? "STOPPING"
                         : ms.runState == CALIBRATING ? "CALIBRATING"
                         : ms.runState == STALLED     ? "STALLED"
                         : ms.runState == HOMING      ? "HOMING"
                                                      : "IDLE";

  autolee::DeviceState st{};
  // Same git-derived string /api/v1/diagnostics/info reports (esp_app_desc_t.version).
  st.version = esp_app_get_description()->version;
  st.state = stateStr;
  st.counter = ms.counter;
  st.speed = ms.profiles[ms.activeProfile].speed_hz;
  st.calibrated = ms.endpointsCalibrated;
  st.positionStale = ms.positionReferenceStale;
  st.rawUp = ms.rawUp;
  st.rawDown = ms.rawDown;
  st.endpointUp = ms.endpointUp;
  st.endpointDown = ms.endpointDown;
  st.upOffset = ms.upOffsetSteps;
  st.downOffset = ms.downOffsetSteps;
  st.position = stepper::getCurrentPosition();
  st.sgTrip = ms.profiles[ms.activeProfile].sg_trip;
  st.workZone = ms.sgWorkZoneSteps;
  st.currentMa = ms.runCurrentMa;
  st.profileIdx = ms.activeProfile;
  st.profileName = ms.profiles[ms.activeProfile].name;
  for (int i = 0; i < NUM_PROFILES; i++) {
    st.profiles[i] = {ms.profiles[i].name, ms.profiles[i].speed_hz, ms.profiles[i].sg_trip};
  }
  st.wifiStatus = wfStat;
  st.wifiSSID = wfSSID.c_str();
  st.wifiIP = wfIP.c_str();
  st.batchTarget = ms.batchTarget;
  st.batchCount = ms.batchCount;
  st.batchActive = ms.batchActive;
  st.defaultPassword = s_default_password_active;

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
  ESP_LOGI(TAG, "Captive portal redirect: host='%s' uri='%s' -> 302 /", req->host(), req->uri());
  res->setCode(302);
  res->addHeader("Location", "/");
  return res->send();
}

// ==========================================================================
//  OTA (esp_ota_ops, replacing Arduino's Update class)
// ==========================================================================
// Set for the duration of an upload; broadcastState() checks this to skip
// SSE sends. Originally added to fix a real watchdog crash reproduced
// during hardware testing (SSE send blocking pump_task long enough to
// starve the watchdog while an OTA POST was also in flight) - that crash
// is now fixed properly by moving broadcastState() off pump_task entirely
// (see app_main.cpp's sse_task), so this guard is no longer load-bearing
// for safety. Kept as a minor optimization: no reason to contend with the
// OTA upload's bandwidth/socket for state broadcasts nobody's reading
// during an upload anyway.
static volatile bool s_ota_in_progress = false;
// Wall-clock of the last received OTA chunk; drives the stale-upload watchdog
// (otaWatchdogTick). Written by the HTTP task, read by sse_task - a plain
// aligned uint32_t load/store, atomic on this target.
static volatile uint32_t s_ota_last_activity_ms = 0;

// Reset all OTA bookkeeping. Does NOT touch the driver - call after the handle
// has already been released (via esp_ota_end or esp_ota_abort).
static void otaClear() {
  s_ota_handle = 0;
  s_ota_partition = nullptr;
  s_ota_in_progress = false;
}

// Abort an OTA whose handle is still open (esp_ota_end not yet called) and
// clear state. MUST NOT run after esp_ota_end (which already frees the handle).
// Only ever called from the HTTP task, so it can't race the upload it aborts.
static void otaAbort(const char *why) {
  ESP_LOGE(TAG, "OTA: aborting (%s)", why);
  if (s_ota_handle) esp_ota_abort(s_ota_handle);
  otaClear();
}

static esp_err_t handleOtaUpload(PsychicRequest *, const char *filename, uint64_t index,
                                 uint8_t *data, size_t len, bool final) {
  s_ota_last_activity_ms = millis();
  if (index == 0) {
    ESP_LOGI(TAG, "OTA: upload '%s'", filename);
    // Reclaim any handle a previous upload leaked (e.g. a client that vanished
    // mid-transfer, which never delivers a `final` chunk). Runs on this same
    // HTTP task as the upload that leaked it, so no cross-task race on abort.
    if (s_ota_handle) otaAbort("leftover handle from a prior aborted upload");
    s_ota_in_progress = true;
    // Deferred: stepper calls must not run in the HTTP task. pump_task keeps
    // running during the upload, so the stop executes within milliseconds.
    if (motion_state::snapshot().runState == RUNNING) motion_cmd::requestStop();
    s_ota_partition = esp_ota_get_next_update_partition(nullptr);
    if (!s_ota_partition ||
        esp_ota_begin(s_ota_partition, OTA_SIZE_UNKNOWN, &s_ota_handle) != ESP_OK) {
      ESP_LOGE(TAG, "OTA: begin failed");
      s_ota_handle = 0;  // begin failure leaves no valid handle to abort
      otaClear();
      return ESP_FAIL;
    }
  }
  if (s_ota_handle && esp_ota_write(s_ota_handle, data, len) != ESP_OK) {
    otaAbort("write failed");
    return ESP_FAIL;
  }
  if (final) {
    // Identity check runs before esp_ota_end, while the handle is still
    // open, so a mismatch can be rejected via esp_ota_abort rather than
    // finalized first: reject an image from a different project before it
    // can ever be set as the boot partition, even transiently. This is one
    // half of the #1c hardening item (docs/PLAN.md) - a leaked/default web
    // password is otherwise enough to flash arbitrary firmware since secure
    // boot is off; this at least stops a wrong/foreign .bin from being
    // accepted as a same-project update.
    esp_app_desc_t written_desc{};
    esp_app_desc_t running_desc{};
    bool identity_ok = false;
    if (esp_ota_get_partition_description(s_ota_partition, &written_desc) == ESP_OK) {
      running_desc = *esp_app_get_description();
      identity_ok = (strncmp(written_desc.project_name, running_desc.project_name,
                             sizeof(written_desc.project_name)) == 0);
    }
    if (!identity_ok) {
      ESP_LOGE(TAG, "OTA: rejected - project name mismatch (image='%s', running='%s')",
               written_desc.project_name, running_desc.project_name);
      webLog("OTA rejected: firmware image identity mismatch");
      otaAbort("project name mismatch");  // handle still open - abort, not end
      return ESP_FAIL;
    }

    // esp_ota_end frees the handle on both success and failure, so neither
    // branch below may call esp_ota_abort - just clear the bookkeeping.
    bool ended = (esp_ota_end(s_ota_handle) == ESP_OK);
    if (ended && esp_ota_set_boot_partition(s_ota_partition) == ESP_OK) {
      ESP_LOGI(TAG, "OTA: success, %llu bytes, project='%s' version='%s'", index + len,
               written_desc.project_name, written_desc.version);
      webLog("OTA: new firmware verified and set to boot");
      // Lifetime OTA count. No guard is held here (this runs on the HTTP task,
      // which is not the owner of g_motion), so take one for the single store -
      // pump_task reads this state unlocked. Counted at "set boot partition
      // succeeded", i.e. the last point where the update can still fail: the
      // reboot below is only the device acting on a decision already made.
      {
        motion_state::Guard g;
        g_motion.otaCount++;
      }
      // Persist immediately rather than leaving it to tick()'s 5 s dirty check:
      // rebootRequested (set on the next line) has pump_task restarting the
      // chip ~500 ms from now, which would beat the throttle and lose the
      // count. Same reasoning settings_store::load() applies to resetCount.
      settings_store::saveNow();
      rebootRequested = true;
      rebootRequestMs = millis();
      otaClear();
    } else {
      ESP_LOGE(TAG, "OTA: end/set-boot-partition failed");
      otaClear();
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

// Periodic guard (called from sse_task) for an upload that died without a
// `final` chunk - the HTTP upload handler gives us no abort callback, so a
// vanished client would otherwise leave s_ota_in_progress stuck true, freezing
// SSE state/log broadcasts until reboot. We only clear the flag here (a bool,
// so no cross-task driver race); the leaked handle itself is reclaimed by the
// HTTP-task defensive abort on the next upload, or by a reboot.
void otaWatchdogTick() {
  if (s_ota_in_progress && (millis() - s_ota_last_activity_ms) > OTA_STALE_TIMEOUT_MS) {
    ESP_LOGW(TAG, "OTA: upload stale (no chunk in %lums) - releasing SSE",
             (unsigned long)OTA_STALE_TIMEOUT_MS);
    s_ota_in_progress = false;
  }
}

// ==========================================================================
void setupWebServer() {
  // Raised when the flat /api/v1/* routes were regrouped into nested paths and
  // the old /api/v1/action dispatcher was split into four standalone routes
  // (+3 handlers). Must stay above the real route count including the seven
  // captive-portal probe handlers registered in AP mode.
  server.config.max_uri_handlers = 32;

  // Default httpd config refuses a new connection outright once
  // max_open_sockets is hit, rather than recycling the oldest one. On the
  // setup AP that's easy to hit: a phone runs several background
  // connectivity-check requests against different domains in parallel (all
  // DNS-redirected to us), plus the dashboard's own long-lived SSE stream
  // holds a socket open indefinitely. A refused/starved connection can
  // surface as a truncated page load (e.g. cut off before </style>) -
  // reproduced on hardware as "sometimes the page has no CSS". LRU purge
  // makes a burst of setup-page traffic recycle old sockets instead of
  // failing new ones.
  server.config.lru_purge_enable = true;

  // Digest auth for every state-changing route (attached per-endpoint below).
  std::string webPass = loadWebPassword();
  s_auth.setUsername(WEB_AUTH_USER)
      .setPassword(webPass.c_str())
      .setRealm(WEB_AUTH_REALM)
      .setAuthMethod(DIGEST_AUTH)
      .setAuthFailureMessage("AutoLee: authentication required");
  s_default_password_active = (webPass == WEB_AUTH_DEFAULT_PASS);
  if (s_default_password_active) {
    webLog("SECURITY: web password is still the factory default - change it on the WiFi page");
  }
  // Attached once, server-wide, rather than per-endpoint: gating on the HTTP
  // method means every state-changing route is covered automatically - including
  // any added later - instead of relying on someone remembering to attach the
  // middleware to each new POST. GET (dashboard, /api/v1/state, SSE) passes
  // straight through. Delegates to AuthenticationMiddleware so the actual
  // digest challenge/nonce handling stays the library's, not hand-rolled.
  server.addMiddleware(
      [](PsychicRequest *req, PsychicResponse *res, PsychicMiddlewareNext next) -> esp_err_t {
        // GET is open by default (dashboard, /api/v1/state, /api/v1/diagnostics/info, SSE -
        // none of it is more sensitive than what's already on-screen). Coredump
        // is the one GET exception: a crash dump is a raw RAM snapshot and can
        // contain the WiFi password or web password in plaintext (both are held
        // in local C buffers - wifi_mgr.cpp's wifi_config.sta/ap.password,
        // s_auth's stored password) - "GET is harmless" doesn't hold for it, so
        // it's gated like a write despite being a read.
        std::string uri = req->uri();
        uri = uri.substr(0, uri.find('?'));  // drop any query string
        if (req->method() == HTTP_GET && uri != "/api/v1/diagnostics/coredump") return next();
        // Captive-portal setup endpoints are exempt while in AP-setup mode: a
        // fresh user doesn't have the (digest) web password, and WPA2 on the AP
        // is what gates access there instead. Only /save and /clear, and only
        // when unconfigured - every /api/v1/* control route stays gated.
        if (wifi_mgr::isApMode() && !wifi_mgr::isConnected()) {
          if (uri == "/save" || uri == "/clear") return next();
        }
        // #1c (docs/PLAN.md) "force-change-on-first-use": while the password
        // is still the factory default, every state-changing route is refused
        // with a friendly 403 telling the caller to set a real password first
        // - except /api/v1/system/web_password itself, which must stay reachable or
        // the operator could never get out of this state. This runs after the
        // digest challenge (wrapped as `next` below) so it only ever applies
        // to a caller who already authenticated with the (known-public)
        // default password - it is not a substitute for auth, just a second
        // gate on top of it: a leaked/default password otherwise still lets
        // the machine run, calibrate and accept OTA uploads forever, per the
        // #1c writeup.
        // NOTE: this literal MUST stay in lockstep with the route registration
        // below. If they ever diverge, the escape hatch stops matching and a
        // device on the factory-default password can never have it changed
        // over the network again (recoverable only over USB/serial).
        if (uri == "/api/v1/system/web_password") return s_auth.run(req, res, next);
        PsychicMiddlewareNext gated = [next, res]() -> esp_err_t {
          if (s_default_password_active) {
            return res->send(403, "application/json",
                              "{\"error\":\"default_password\",\"message\":\"Web password is "
                              "still the factory default - set a real one via POST "
                              "/api/v1/system/web_password before using this control.\"}");
          }
          return next();
        };
        return s_auth.run(req, res, gated);
      });

  server.on("/", HTTP_GET, [](PsychicRequest *req, PsychicResponse *res) {
    // Which page this route serves depends on live connection state (WiFi
    // setup vs. dashboard) and, on the setup AP, changes across sessions
    // (scanned network list, key). Without this, a phone that previously
    // loaded the page (this AP, or a stale captive-portal load from earlier
    // testing) can silently serve that cached copy on "refresh" instead of
    // hitting the device - reproduced on hardware as a garbled/stale page.
    res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    if (wifi_mgr::isApMode() && !wifi_mgr::isConnected()) {
      // PsychicResponse::setContent(const char*) stores the raw pointer, it
      // doesn't copy - a temporary std::string's buffer would already be
      // freed by the time send() reads it below (reproduced on hardware:
      // garbled/garbage bytes served instead of the page). Keep the string
      // alive in a named local for the rest of this scope.
      std::string page = wifiConfigPage();
      res->setCode(200);
      res->setContentType("text/html");
      res->setContent(page.c_str());
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

  // Diagnostics: firmware identity, reset cause, heap, uptime, running OTA
  // slot and whether a coredump is waiting to be pulled. A GET, so - like
  // every other read in this file - it's left open (no Digest challenge);
  // see the middleware comment above for the read/write auth split.
  server.on("/api/v1/diagnostics/info", HTTP_GET, [](PsychicRequest *req, PsychicResponse *res) {
    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    // Truncated hex (first 8 bytes / 16 hex chars) - matches the short SHA
    // ESP-IDF itself prints in the boot log, plenty to correlate against a
    // build without dumping the full 64-char digest.
    char sha[17];
    for (int i = 0; i < 8; i++) snprintf(sha + i * 2, 3, "%02x", desc->app_elf_sha256[i]);

    bool coredumpPresent = (esp_core_dump_image_check() == ESP_OK);

    // Lifetime health counters (persisted - see main/settings_blob.h). They
    // live here rather than in /api/v1/state on purpose: they are support
    // statistics that change at most once per stroke and are never needed to
    // render the dashboard, whereas /api/v1/state is polled and re-broadcast
    // over SSE continuously and is already close to its fixed 768-byte buffer.
    const MotionState ms = motion_state::snapshot();
    // Derived, not stored: the mean successful stroke time. `counter` is the
    // successful-cycle count, so it is the right denominator - but note it is
    // capped at COUNTER_MAX for the display and can be zeroed via
    // /api/v1/motion/reset_counter, either of which skews this average while
    // leaving totalCycleTimeMs/longestCycleMs exact. Prefer those two when the
    // number has to be trusted.
    const uint32_t avgCycleMs =
        (ms.counter > 0) ? (uint32_t)(ms.totalCycleTimeMs / (uint32_t)ms.counter) : 0;

    char buf[896];
    snprintf(buf, sizeof(buf),
             "{\"version\":\"%s\",\"idfVersion\":\"%s\",\"compileDate\":\"%s\","
             "\"compileTime\":\"%s\",\"elfSha256\":\"%s\",\"resetReason\":\"%s\","
             "\"freeHeap\":%u,\"minFreeHeap\":%u,\"uptimeMs\":%llu,"
             "\"runningPartition\":\"%s\",\"coredumpPresent\":%s,"
             "\"health\":{\"cycleCount\":%ld,\"stallCount\":%u,\"totalCycleTimeMs\":%lu,"
             "\"avgCycleMs\":%lu,\"longestCycleMs\":%lu,\"calibrationCount\":%u,"
             "\"otaCount\":%u,\"resetCount\":%u}}",
             desc->version, desc->idf_ver, desc->date, desc->time, sha,
             resetReasonStr(esp_reset_reason()), (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned long long)(esp_timer_get_time() / 1000),
             running ? running->label : "?", coredumpPresent ? "true" : "false",
             (long)ms.counter, (unsigned)ms.stallCount, (unsigned long)ms.totalCycleTimeMs,
             (unsigned long)avgCycleMs, (unsigned long)ms.longestCycleMs,
             (unsigned)ms.calibrationCount, (unsigned)ms.otaCount, (unsigned)ms.resetCount);
    return res->send(200, "application/json", buf);
  });

  // Streams the raw `coredump` partition as a file download so it's
  // retrievable over the network instead of only via `idf.py coredump-info`
  // over USB serial. esp_core_dump_image_check() validates the stored
  // checksum first - a corrupt/blank partition 404s instead of streaming
  // garbage. Unlike every other GET in this file, this one IS auth-gated (see
  // the middleware above) - a coredump is a raw RAM snapshot and can contain
  // the WiFi/web password in plaintext.
  server.on("/api/v1/diagnostics/coredump", HTTP_GET, [](PsychicRequest *req, PsychicResponse *res) -> esp_err_t {
    if (esp_core_dump_image_check() != ESP_OK) {
      return res->send(404, "text/plain", "no core dump present");
    }
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0) {
      return res->send(404, "text/plain", "no core dump present");
    }
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (!part) {
      return res->send(404, "text/plain", "no coredump partition");
    }

    res->setCode(200);
    res->setContentType("application/octet-stream");
    res->addHeader("Content-Disposition", "attachment; filename=\"autolee_coredump.bin\"");
    res->sendHeaders();

    uint8_t chunk[512];
    size_t offset = 0, remaining = size;
    esp_err_t err = ESP_OK;
    while (remaining > 0) {
      size_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
      err = esp_partition_read(part, offset, chunk, n);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "coredump: partition read failed at offset %u (%s)", (unsigned)offset,
                 esp_err_to_name(err));
        break;
      }
      err = res->sendChunk(chunk, n);
      if (err != ESP_OK) break;  // client likely gone; sendChunk already aborted the stream
      offset += n;
      remaining -= n;
    }
    if (err == ESP_OK) res->finishChunking();
    return err;
  });

  server.on("/api/v1/motion/toggle_run", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    // Deferred to pump_task: touches the stepper, TMC5160 SPI and LVGL.
    motion_cmd::requestToggleRun();
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/motion/profile", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("idx")) {
      uint8_t idx = (uint8_t)atoi(req->getParam("idx", "0"));
      // Deferred: setActiveProfile() drives the stepper and refreshes LVGL.
      if (idx < NUM_PROFILES) motion_cmd::requestProfile(idx);
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/motion/current", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("ma")) {
      // Deferred: rms_current() is an SPI write that would collide with
      // pump_task's StallGuard reads on the shared display/TMC bus.
      // Clamping happens at execution time.
      motion_cmd::requestCurrentMa(atoi(req->getParam("ma", "0")));
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/motion/endpoint", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("which") && req->hasParam("delta") &&
        motion_state::snapshot().endpointsCalibrated) {
      std::string w = req->getParam("which", "");
      int32_t d = atoi(req->getParam("delta", "0"));
      {
        // Read-modify-write of state pump_task also reads - must be guarded.
        motion_state::Guard g;
        if (w == "up")
          g_motion.upOffsetSteps =
              autolee::clamp_i32(g_motion.upOffsetSteps + d, OFFSET_MIN, OFFSET_MAX);
        else
          g_motion.downOffsetSteps =
              autolee::clamp_i32(g_motion.downOffsetSteps + d, OFFSET_MIN, OFFSET_MAX);
      }
      recomputeEffectiveEndpoints();   // pure math (takes the guard itself)
      motion_cmd::requestUiRefresh();  // LVGL label update deferred to pump_task
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/motion/sg_trip", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    const MotionState ms = motion_state::snapshot();
    uint8_t tgt = ms.activeProfile;
    if (req->hasParam("profile")) {
      uint8_t p = (uint8_t)atoi(req->getParam("profile", "0"));
      if (p < NUM_PROFILES) tgt = p;
    }
    if (req->hasParam("value")) {
      int32_t v = atoi(req->getParam("value", "0"));
      if (v < RUN_SG_TRIP_MIN) v = RUN_SG_TRIP_MIN;
      if (v > RUN_SG_TRIP_MAX) v = RUN_SG_TRIP_MAX;
      {
        motion_state::Guard g;
        g_motion.profiles[tgt].sg_trip = (uint16_t)v;
      }
      motion_cmd::requestUiRefresh();
    } else if (req->hasParam("delta")) {
      int32_t v = (int32_t)ms.profiles[tgt].sg_trip + atoi(req->getParam("delta", "0"));
      if (v < RUN_SG_TRIP_MIN) v = RUN_SG_TRIP_MIN;
      if (v > RUN_SG_TRIP_MAX) v = RUN_SG_TRIP_MAX;
      {
        motion_state::Guard g;
        g_motion.profiles[tgt].sg_trip = (uint16_t)v;
      }
      motion_cmd::requestUiRefresh();
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/motion/work_zone", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("delta")) {
      const int32_t d = atoi(req->getParam("delta", "0"));
      {
        motion_state::Guard g;
        int32_t v = g_motion.sgWorkZoneSteps + d;
        if (v < SG_WORK_ZONE_MIN) v = SG_WORK_ZONE_MIN;
        if (v > SG_WORK_ZONE_MAX) v = SG_WORK_ZONE_MAX;
        g_motion.sgWorkZoneSteps = v;
      }
      motion_cmd::requestUiRefresh();
    }
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/motion/batch", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    if (req->hasParam("delta")) {
      const int32_t d = atoi(req->getParam("delta", "0"));
      {
        motion_state::Guard g;
        int32_t v = g_motion.batchTarget + d;
        g_motion.batchTarget = (v < 0) ? 0 : (v > BATCH_TARGET_MAX ? BATCH_TARGET_MAX : v);
      }
      motion_cmd::requestUiRefresh();
    }
    if (req->hasParam("action")) {
      std::string a = req->getParam("action", "");
      if (a == "start") {
        // Deferred: startRunBetweenEndpoints() drives the stepper + TMC SPI.
        // Validity (target set, IDLE, calibrated) is re-checked at execution.
        motion_cmd::requestBatchStart();
      } else if (a == "clear") {
        {
          // One guard: readers must never see target/count/active disagree.
          motion_state::Guard g;
          g_motion.batchTarget = 0;
          g_motion.batchCount = 0;
          g_motion.batchActive = false;
        }
        motion_cmd::requestUiRefresh();
      }
    }
    return res->send(200, "text/plain", "ok");
  });

  // The four routes below were split out of the old single
  // POST /api/v1/action?do=<verb> dispatcher when the flat API was regrouped
  // into nested paths - same underlying motion_cmd calls, one route each, no
  // query parameter.
  server.on("/api/v1/motion/calibrate", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    motion_cmd::requestCalibrate();
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/motion/return_home", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    motion_cmd::requestReturnHome();
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/motion/reset_counter", HTTP_POST,
            [](PsychicRequest *req, PsychicResponse *res) {
              {
                motion_state::Guard g;
                g_motion.counter = 0;
              }
              return res->send(200, "text/plain", "ok");
            });

  server.on("/api/v1/settings/reset", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    // Deferred: erases the calibration/tuning NVS blob and re-programs the
    // TMC5160 over the display's SPI bus - pump_task work, not HTTP-task
    // work. Gated on IDLE at execution time (motion_cmd.cpp), not here.
    motion_cmd::requestResetSettings();
    return res->send(200, "text/plain", "ok");
  });

  server.on("/api/v1/wifi/save", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
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

  server.on("/api/v1/wifi/reset", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    wifi_mgr::clearCredentials();
    rebootRequested = true;
    rebootRequestMs = millis();
    return res->send(200, "text/plain", "cleared");
  });

  server.on("/api/v1/system/log_clear", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    // Deferred: reassigning the ring here would race webLog()'s push from
    // pump_task / the LVGL task.
    motion_cmd::requestLogClear();
    return res->send(200, "text/plain", "ok");
  });

  // Change the web password. Itself auth-gated (it's a POST), so only someone
  // who already knows the current password can set a new one. Takes effect
  // immediately - no reboot - since the middleware reads from s_auth.
  server.on("/api/v1/system/web_password", HTTP_POST, [](PsychicRequest *req, PsychicResponse *res) {
    std::string pass = req->getParam("pass", "");
    if (pass.empty()) return res->send(400, "text/plain", "password required");
    if (pass.length() > WEB_AUTH_PASS_MAX) return res->send(400, "text/plain", "password too long");
    if (!saveWebPassword(pass)) return res->send(500, "text/plain", "could not save password");
    s_auth.setPassword(pass.c_str());
    // Without this, the #1c write-gate above would keep 403ing every other
    // route forever after a real password is set - the flag was otherwise
    // only ever refreshed at boot, so an operator who follows the "set a
    // password" prompt would find nothing else works until a reboot.
    s_default_password_active = (pass == WEB_AUTH_DEFAULT_PASS);
    webLog("Web password changed");
    return res->send(200, "text/plain", "ok");
  });

  // OTA firmware upload. PsychicUploadHandler streams the body to
  // handleOtaUpload() and sends the HTTP response itself (200 "Upload
  // Successful." if the callback returned ESP_OK throughout, 500
  // otherwise) - rebootRequested is set from inside the callback's
  // `final` branch, since that's where success/failure is known.
  static PsychicUploadHandler ota_upload;
  ota_upload.onUpload(handleOtaUpload);
  server.on("/api/v1/system/ota", HTTP_POST, &ota_upload);

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

static uint32_t s_lastSSEMs = 0;
// Last state payload actually sent (empty until the first send) and when a
// message last went out (state or heartbeat) - see below: broadcastState()
// only pushes the full state when it changed, plus a heartbeat at a longer
// fixed cadence so a client can tell "nothing changed" apart from "connection
// died" without a full-state message every SSE_INTERVAL_MS regardless of
// whether anything moved.
static std::string s_lastSentState;
static uint32_t s_lastSSESendMs = 0;

void broadcastState() {
  if (s_ota_in_progress) return;
  uint32_t now = millis();
  if ((now - s_lastSSEMs) < SSE_INTERVAL_MS) return;
  s_lastSSEMs = now;

  std::string state = buildStateJSON();
  if (state != s_lastSentState) {
    events.send(state.c_str(), nullptr, millis());
    s_lastSentState = state;
    s_lastSSESendMs = now;
  } else if ((now - s_lastSSESendMs) >= SSE_HEARTBEAT_MS) {
    // Named event, not the default "message" - the dashboard's onmessage
    // handler only fires for the unnamed event, so this is inert to it
    // unless a client explicitly listens for "heartbeat".
    events.send("{}", "heartbeat", millis());
    s_lastSSESendMs = now;
  }

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
