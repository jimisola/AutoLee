// Shared by every serializer in this directory. Lifted out of state_json.h
// when boot_report.h needed the same rule; the behaviour is unchanged.
#pragma once

#include <cstddef>

namespace autolee {

// Escapes a string for inclusion in a JSON string literal. Some fields here are
// attacker-influenceable (a nearby AP's SSID, a device name) unlike the
// firmware constants around them, so this can never be skipped "because the
// caller controls it".
inline void jsonEscape(const char *in, char *out, size_t outSize) {
  size_t o = 0;
  for (size_t i = 0; in[i] != '\0' && o + 2 < outSize; i++) {
    char c = in[i];
    // Drop raw control characters outright: they'd need \uXXXX escapes to be
    // valid JSON, and no legitimate value here contains them (upstream v1.10.0
    // does the same).
    if ((unsigned char)c < 0x20) continue;
    if (c == '"' || c == '\\') out[o++] = '\\';
    out[o++] = c;
  }
  out[o] = '\0';
}

}  // namespace autolee
