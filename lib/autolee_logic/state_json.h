// ============================================================================
//  autolee_logic/state_json.h
//  Pure serializer for the /api/state JSON — host-testable, no hardware.
//  Byte-for-byte matches buildStateJSON() in src/web_server.cpp, so the
//  firmware, the REST response, and the SSE state event share one contract
//  (see schemas/state.schema.json).
// ============================================================================
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>

namespace autolee {

struct ProfileView {
  const char *name;
  uint32_t hz;
  uint16_t sg;
};

struct DeviceState {
  const char *version;
  const char *state;  // "IDLE" | "RUNNING" | "STOPPING" | "CALIBRATING" | "STALLED" | "HOMING"
  // True while the web/API password is still the factory default (see
  // main/config.h's WEB_AUTH_DEFAULT_PASS). While true, the firmware refuses
  // every state-changing write except POST /api/v1/system/web_password itself -
  // this is the "force-change-on-first-use" half of docs/PLAN.md's #1c;
  // the dashboard uses this flag to show a "set a password" banner instead
  // of relying on the operator having read the boot log.
  bool defaultPassword;
  long counter;
  uint32_t speed;
  bool calibrated;
  // Calibration restored across a reboot but the axis not yet re-referenced
  // against the UP hard stop: the firmware refuses to start a run until a
  // Return Home clears it (see main/motion/motion_state.h).
  bool positionStale;
  long rawUp, rawDown, endpointUp, endpointDown;
  int32_t upOffset, downOffset;
  long position;
  uint16_t sgTrip;
  int32_t workZone;
  uint16_t currentMa;
  uint8_t profileIdx;
  const char *profileName;
  ProfileView profiles[3];
  const char *wifiStatus;
  const char *wifiSSID;
  const char *wifiIP;
  long batchTarget, batchCount;
  bool batchActive;
};

// SSIDs are attacker-influenceable (a nearby AP or a manually-typed SSID can
// contain '"' or '\'), unlike every other string field here (firmware
// constants or machine-generated values) - escape it so it can never break
// the JSON. Max WiFi SSID length is 32 bytes; sized for the worst case
// (every byte escaped) plus NUL.
inline void jsonEscape(const char *in, char *out, size_t outSize) {
  size_t o = 0;
  for (size_t i = 0; in[i] != '\0' && o + 2 < outSize; i++) {
    char c = in[i];
    // Drop raw control characters outright: they'd need \uXXXX escapes to be
    // valid JSON, and no legitimate SSID contains them (upstream v1.10.0 does
    // the same).
    if ((unsigned char)c < 0x20) continue;
    if (c == '"' || c == '\\') out[o++] = '\\';
    out[o++] = c;
  }
  out[o] = '\0';
}

// Serialize into `out` (size `n`). Returns the snprintf return value (number of
// chars that would have been written). The format mirrors the firmware exactly.
inline int buildStateJson(const DeviceState &s, char *out, size_t n) {
  char ssidEscaped[65];
  jsonEscape(s.wifiSSID, ssidEscaped, sizeof(ssidEscaped));
  return snprintf(
      out, n,
      "{\"version\":\"%s\",\"state\":\"%s\",\"defaultPassword\":%s,\"counter\":%ld,\"speed\":%lu,"
      "\"calibrated\":%s,"
      "\"positionStale\":%s,"
      "\"rawUp\":%ld,\"rawDown\":%ld,\"endpointUp\":%ld,\"endpointDown\":%ld,"
      "\"upOffset\":%ld,\"downOffset\":%ld,\"position\":%ld,\"sgTrip\":%u,"
      "\"workZone\":%ld,\"currentMa\":%u,"
      "\"profileIdx\":%u,\"profileName\":\"%s\","
      "\"profiles\":[{\"name\":\"%s\",\"hz\":%lu,\"sg\":%u},"
      "{\"name\":\"%s\",\"hz\":%lu,\"sg\":%u},"
      "{\"name\":\"%s\",\"hz\":%lu,\"sg\":%u}],"
      "\"wifiStatus\":\"%s\",\"wifiSSID\":\"%s\",\"wifiIP\":\"%s\","
      "\"batchTarget\":%ld,\"batchCount\":%ld,\"batchActive\":%s}",
      s.version, s.state, s.defaultPassword ? "true" : "false", s.counter, (unsigned long)s.speed,
      s.calibrated ? "true" : "false", s.positionStale ? "true" : "false", s.rawUp, s.rawDown,
      s.endpointUp, s.endpointDown, (long)s.upOffset, (long)s.downOffset, s.position, s.sgTrip,
      (long)s.workZone, s.currentMa, s.profileIdx, s.profileName, s.profiles[0].name,
      (unsigned long)s.profiles[0].hz, s.profiles[0].sg, s.profiles[1].name,
      (unsigned long)s.profiles[1].hz, s.profiles[1].sg, s.profiles[2].name,
      (unsigned long)s.profiles[2].hz, s.profiles[2].sg, s.wifiStatus, ssidEscaped, s.wifiIP,
      s.batchTarget, s.batchCount, s.batchActive ? "true" : "false");
}

}  // namespace autolee
