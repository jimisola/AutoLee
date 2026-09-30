#pragma once

// Serial command console on the USB serial/JTAG port - the one cable that
// still reaches the rig when its WiFi is what is broken. Read-only
// diagnostics; see console_command.h for the commands.
namespace console {

// Installs the USB serial/JTAG driver and starts the reader task. A failure is
// logged and otherwise ignored: the press runs without a console.
void start();

}  // namespace console
