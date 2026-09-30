#include "console.h"

#include <string>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "console_command.h"
#include "wifi_mgr.h"
#include "wifi_report.h"

namespace console {

namespace {

const char *TAG = "console";

// Loops because usb_serial_jtag_write_bytes() takes only what fits in the TX
// ring. Gives up when nothing is taken within the timeout - no host attached,
// or one that has stopped reading - rather than blocking the task forever.
void say(const std::string &text) {
  std::string crlf;
  crlf.reserve(text.size() + text.size() / 16);
  for (char c : text) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  size_t sent = 0;
  while (sent < crlf.size()) {
    const int wrote =
        usb_serial_jtag_write_bytes(crlf.data() + sent, crlf.size() - sent, pdMS_TO_TICKS(200));
    if (wrote <= 0) return;
    sent += static_cast<size_t>(wrote);
  }
}

const char *kWifiOff = "WiFi is switched off - turn it on from the panel (Config -> WiFi)\n";

void handle_wifi_scan() {
  if (!wifi_mgr::isEnabled()) {
    say(kWifiOff);
    return;
  }
  if (wifi_mgr::transitionInFlight()) {
    say("a WiFi change is in progress - try again in a few seconds\n");
    return;
  }
  // Fresh rather than cached: the question is usually "what does it hear now",
  // asked while someone moves the rig or an access point.
  say("scanning all channels (about 2s)...\n");
  autolee::Survey found;
  if (!wifi_mgr::survey(found)) {
    say("the scan did not run - the radio refused it or a WiFi change started\n");
    return;
  }
  say("\n" + autolee::format_survey(found));
}

void handle_wifi_info() {
  if (!wifi_mgr::isEnabled()) {
    say(kWifiOff);
    return;
  }
  autolee::LinkInfo link;
  if (wifi_mgr::staLink(link)) {
    say(autolee::format_link(link));
    return;
  }
  if (wifi_mgr::isApMode()) {
    say("not joined to a network - serving the setup AP '" + wifi_mgr::ssid() + "' at " +
        wifi_mgr::ipAddress() + "\n");
  } else {
    say("not joined to a network\n");
  }
}

void handle(const std::string &line) {
  using autolee::console::Command;
  switch (autolee::console::parse_command(line)) {
    case Command::kNone:
      break;
    case Command::kWifiScan:
      handle_wifi_scan();
      break;
    case Command::kWifiInfo:
      handle_wifi_info();
      break;
    case Command::kHelp:
      say("wifi-scan   every radio in range, one row per BSSID: signal, channel,\n"
          "            security, BSSID, and hidden networks too\n"
          "wifi-info   the current join: SSID, BSSID, channel, signal, IP,\n"
          "            netmask, gateway, DNS, MAC\n"
          "help        this\n");
      break;
    case Command::kUnknown:
      say("unknown command '" + autolee::console::first_word(line) + "' - try 'help'\n");
      break;
  }
  say("autolee> ");
}

void task(void *) {
  say("\nAutoLee console - type 'help'\nautolee> ");

  std::string line;
  bool last_was_cr = false;
  for (;;) {
    uint8_t byte = 0;
    if (usb_serial_jtag_read_bytes(&byte, 1, portMAX_DELAY) <= 0) continue;

    if (byte == '\r' || byte == '\n') {
      // A terminal sending CRLF is one line, not a line and a blank one.
      const bool crlf_tail = (byte == '\n' && last_was_cr);
      last_was_cr = (byte == '\r');
      if (crlf_tail) continue;
      say("\n");
      handle(line);
      line.clear();
      continue;
    }
    last_was_cr = false;

    if (byte == 0x7F || byte == '\b') {
      if (!line.empty()) {
        line.pop_back();
        say("\b \b");
      }
      continue;
    }

    // Bounded, so noise on the line cannot grow the heap.
    if (line.size() < 80 && byte >= 0x20 && byte < 0x7F) {
      line.push_back(static_cast<char>(byte));
      say(std::string(1, static_cast<char>(byte)));  // the port does not echo
    }
  }
}

}  // namespace

void start() {
  usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  // A wifi-scan table is a few KB; the default 256-byte ring would make every
  // reply several round trips.
  config.tx_buffer_size = 2048;
  const esp_err_t err = usb_serial_jtag_driver_install(&config);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "console unavailable (%s)", esp_err_to_name(err));
    return;
  }
  // The log's secondary console writes to this same port. Routed through the
  // driver, log lines and replies share one ring instead of racing each other
  // into the hardware FIFO.
  usb_serial_jtag_vfs_use_driver();

  if (xTaskCreate(task, "console", 3072, nullptr, 1, nullptr) != pdPASS) {
    ESP_LOGW(TAG, "console task would not start");
  }
}

}  // namespace console
