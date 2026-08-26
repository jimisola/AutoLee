// One radio survey, two views of it.
//
// The scan serves a picker (captive-portal dropdown, /api/v1/wifi/scan) and a
// diagnostic. Collapsing to one row per SSID is right for the first and hides
// exactly what the second is for, so the collapse lives here as a pure
// function over the survey rather than in the scan itself.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace autolee {

// A portable mirror of the driver's per-BSSID record, so the picker logic is
// testable off-target.
struct ApRecord {
  std::string ssid;  // empty means the AP is not advertising, not that it is absent
  uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
  uint8_t channel = 0;
  int8_t rssi = 0;
  bool secure = false;

  bool hidden() const { return ssid.empty(); }
};

using Survey = std::vector<ApRecord>;

// The picker view: one row per named SSID, keeping the strongest radio for
// each, with hidden APs dropped (nothing selectable to show; the manual-entry
// field covers them).
//
// SSIDs come out in order of first appearance in the survey. The driver
// already returns strongest-first, so this preserves the dropdown's existing
// order -- but the strongest record is chosen explicitly rather than inherited
// from that ordering, because nothing in the API promises it.
inline Survey strongest_per_ssid(const Survey &survey) {
  Survey out;
  for (const auto &ap : survey) {
    if (ap.hidden()) continue;
    auto it = std::find_if(out.begin(), out.end(),
                           [&ap](const ApRecord &kept) { return kept.ssid == ap.ssid; });
    if (it == out.end()) {
      out.push_back(ap);
    } else if (ap.rssi > it->rssi) {
      *it = ap;  // stronger radio wins; the SSID keeps its original position
    }
  }
  return out;
}

// Lowercase colon-separated BSSID, for diagnostic output.
inline std::string bssid_to_string(const uint8_t bssid[6]) {
  static const char kHex[] = "0123456789abcdef";
  std::string s;
  s.reserve(17);
  for (int i = 0; i < 6; i++) {
    if (i) s += ':';
    s += kHex[(bssid[i] >> 4) & 0x0F];
    s += kHex[bssid[i] & 0x0F];
  }
  return s;
}

}  // namespace autolee
