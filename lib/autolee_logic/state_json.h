// ============================================================================
//  autolee_logic/state_json.h
//  Pure serializer for the /api/state JSON — host-testable, no hardware.
//  Byte-for-byte matches buildStateJSON() in AutoLee/web_server.h, so the
//  firmware, the REST response, and the SSE state event share one contract
//  (see schemas/state.schema.json).
// ============================================================================
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>

namespace autolee {

struct ProfileView {
  const char* name;
  uint32_t    hz;
  uint16_t    sg;
};

struct DeviceState {
  const char* version;
  const char* state;        // "IDLE" | "RUNNING" | "STOPPING" | "CALIBRATING" | "STALLED" | "HOMING"
  long        counter;
  uint32_t    speed;
  bool        calibrated;
  long        rawUp, rawDown, endpointUp, endpointDown;
  int32_t     upOffset, downOffset;
  long        position;
  uint16_t    sgTrip;
  int32_t     workZone;
  uint16_t    currentMa;
  uint8_t     profileIdx;
  const char* profileName;
  ProfileView profiles[3];
  const char* wifiStatus;
  const char* wifiSSID;
  const char* wifiIP;
  long        batchTarget, batchCount;
  bool        batchActive;
};

// Serialize into `out` (size `n`). Returns the snprintf return value (number of
// chars that would have been written). The format mirrors the firmware exactly.
inline int buildStateJson(const DeviceState& s, char* out, size_t n) {
  return snprintf(out, n,
    "{\"version\":\"%s\",\"state\":\"%s\",\"counter\":%ld,\"speed\":%lu,\"calibrated\":%s,"
    "\"rawUp\":%ld,\"rawDown\":%ld,\"endpointUp\":%ld,\"endpointDown\":%ld,"
    "\"upOffset\":%ld,\"downOffset\":%ld,\"position\":%ld,\"sgTrip\":%u,"
    "\"workZone\":%ld,\"currentMa\":%u,"
    "\"profileIdx\":%u,\"profileName\":\"%s\","
    "\"profiles\":[{\"name\":\"%s\",\"hz\":%lu,\"sg\":%u},"
    "{\"name\":\"%s\",\"hz\":%lu,\"sg\":%u},"
    "{\"name\":\"%s\",\"hz\":%lu,\"sg\":%u}],"
    "\"wifiStatus\":\"%s\",\"wifiSSID\":\"%s\",\"wifiIP\":\"%s\","
    "\"batchTarget\":%ld,\"batchCount\":%ld,\"batchActive\":%s}",
    s.version, s.state, s.counter, (unsigned long)s.speed, s.calibrated ? "true" : "false",
    s.rawUp, s.rawDown, s.endpointUp, s.endpointDown,
    (long)s.upOffset, (long)s.downOffset, s.position, s.sgTrip,
    (long)s.workZone, s.currentMa,
    s.profileIdx, s.profileName,
    s.profiles[0].name, (unsigned long)s.profiles[0].hz, s.profiles[0].sg,
    s.profiles[1].name, (unsigned long)s.profiles[1].hz, s.profiles[1].sg,
    s.profiles[2].name, (unsigned long)s.profiles[2].hz, s.profiles[2].sg,
    s.wifiStatus, s.wifiSSID, s.wifiIP,
    s.batchTarget, s.batchCount, s.batchActive ? "true" : "false");
}

}  // namespace autolee
