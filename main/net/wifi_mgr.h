#pragma once

#include <cstdint>
#include <string>

// WiFi connection management + captive portal, ported from src/wifi_ota.cpp.
// ArduinoOTA is dropped (Arduino-only); OTA is via the web UI upload endpoint
// (see docs/PLAN.md Phase 5) using esp_ota directly.
namespace wifi_mgr {

void start();  // load saved creds, try STA, fall back to AP + captive portal

// Orderly network shutdown, to be called immediately before esp_restart().
// Stops the captive-portal DNS responder (AP mode only) and the WiFi driver, so
// the reset doesn't land mid-operation - which could leave the chip hung and
// needing a power cycle. Best-effort: never blocks the reboot on failure.
void stopForReboot();

bool isConnected();
bool isApMode();
// True once the device has ever reached GOT_IP on a real network. Latched in
// NVS and never cleared, including by clearCredentials() - a WiFi reset must not
// discard a web password the operator already set. Drives the web auth policy:
// false => no web password required at all (physical presence at the rig is the
// gate), true => digest auth + force-change-on-first-use apply.
bool hasEverJoined();
std::string ipAddress();   // STA or AP IP, whichever is active
std::string ssid();        // connected STA SSID, or the AP's SSID in AP mode
std::string apPassword();  // the setup AP's WPA2 key (per-device, persisted in NVS)

// Returns false (storing nothing) if the SSID is empty or either value exceeds
// what the WiFi driver's fixed-size config fields can hold - see
// WIFI_SSID_MAX_LEN / WIFI_PASS_MAX_LEN in config.h.
bool saveCredentials(const std::string &ssid, const std::string &pass);
void clearCredentials();

// HTML <option> list from the last scan (AP mode only - see wifi_mgr.cpp).
std::string scannedOptionsHtml();

}  // namespace wifi_mgr
