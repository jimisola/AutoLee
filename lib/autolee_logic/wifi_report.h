// Plain-text WiFi reports for the serial console: the full survey as a table,
// and the current station link. Lines end in "\n"; the transport owns CRLF.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "wifi_scan.h"

namespace autolee {

// A portable mirror of what the driver and netif report about the current
// station link.
struct LinkInfo {
  std::string ssid;
  uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
  uint8_t channel = 0;
  int8_t rssi = 0;
  std::string security;
  std::string ip;
  std::string netmask;
  std::string gateway;
  std::string dns;
  std::string mac;
  std::string mdns;  // "<name>.local", or "" when mDNS is not up
};

// An SSID is up to 32 arbitrary bytes from whoever is transmitting nearby.
// Control bytes are escaped so a crafted name cannot drive the operator's
// terminal; bytes >= 0x80 pass through, since UTF-8 names are legitimate.
inline std::string printable_ssid(const std::string &ssid) {
  if (ssid.empty()) return "(hidden)";
  std::string out;
  for (unsigned char c : ssid) {
    if (c < 0x20 || c == 0x7F) {
      char esc[5];
      snprintf(esc, sizeof(esc), "\\x%02x", c);
      out += esc;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

inline std::string signal_meter(int rssi) {
  std::string meter = "[....]";
  for (int i = 0; i < signal_bars(rssi); i++) meter[1 + i] = '#';
  return meter;
}

// One row per BSSID, in survey order. Two rows sharing an SSID are two radios
// (a mesh, or one box with two bands), which is the thing the pickers'
// one-row-per-SSID view cannot show.
inline std::string format_survey(const Survey &survey) {
  if (survey.empty()) return "no networks found - nothing on air, or the radio is down\n";

  std::string out;
  out += "signal   dBm  ch  security    bssid              ssid\n";
  out += "------  ----  --  ----------  -----------------  --------------------------------\n";
  size_t hidden = 0;
  for (const ApRecord &ap : survey) {
    if (ap.hidden()) hidden++;
    char row[96];
    snprintf(row, sizeof(row), "%s  %4d  %2u  %-10s  %s  ", signal_meter(ap.rssi).c_str(),
             static_cast<int>(ap.rssi), static_cast<unsigned>(ap.channel),
             ap.security.empty() ? "?" : ap.security.c_str(), bssid_to_string(ap.bssid).c_str());
    out += row;
    out += printable_ssid(ap.ssid);
    out += '\n';
  }

  char summary[128];
  snprintf(summary, sizeof(summary), "\n%u radio(s): %u named network(s), %u hidden.\n",
           static_cast<unsigned>(survey.size()),
           static_cast<unsigned>(strongest_per_ssid(survey).size()), static_cast<unsigned>(hidden));
  out += summary;
  out += "The setup portal offers the named ones, strongest radio per name.\n";
  return out;
}

inline std::string format_link(const LinkInfo &link) {
  std::string out;
  auto line = [&out](const char *label, const std::string &value) {
    if (value.empty()) return;
    out += label;
    out += value;
    out += '\n';
  };
  char buf[48];

  line("ssid       ", printable_ssid(link.ssid));
  line("bssid      ", bssid_to_string(link.bssid));
  snprintf(buf, sizeof(buf), "%d dBm (%d/4 bars)", static_cast<int>(link.rssi),
           signal_bars(link.rssi));
  line("signal     ", buf);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(link.channel));
  line("channel    ", buf);
  line("security   ", link.security);
  line("ip         ", link.ip);
  line("netmask    ", link.netmask);
  line("gateway    ", link.gateway);
  line("dns        ", link.dns);
  line("mac        ", link.mac);
  line("mdns       ", link.mdns);
  return out;
}

}  // namespace autolee
