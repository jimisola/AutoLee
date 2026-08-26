#pragma once

#include <cstdint>
#include "driver/gpio.h"

// Native RMT/PCNT-based STEP/DIR pulse generator, replacing FastAccelStepper
// (which turned out to require Arduino-as-an-ESP-IDF-component - see ADR
// 0001 and docs/PLAN.md Phase 4). Exposes the small subset of
// FastAccelStepper's API the original motion logic actually used.
//
// *** UNVERIFIED ON HARDWARE - build-verified only. No motor/TMC5160 rig was
// connected while this was written. Before trusting this on the real press,
// bench-verify: a move actually reaches its target, isRunning()/
// getCurrentPosition() track reality (via the PCNT hardware step counter),
// and forceStop() halts pulse output within its documented latency bound
// (one cruise-phase chunk, see stepper.cpp's kCruiseChunkMs). ***
namespace stepper {

// `enable_gpio` drives the TMC5160's DRV_ENN input (active low - see
// docs/wiring.md). init() configures it as an output and asserts it, so the
// driver is energised from boot; before this it was left in its reset state
// (high-Z) and the firmware had no way to de-energise the driver at all.
void init(gpio_num_t step_gpio, gpio_num_t dir_gpio, gpio_num_t enable_gpio);

// Energise (true) or de-energise (false) the driver's output stage.
//
// NOTE: de-energising drops holding current entirely - on a press whose ram
// can back-drive, that means the ram is no longer held. Motion code does NOT
// call this automatically (not on STALLED, not when idle); it is exposed so a
// deliberate de-energise is possible. Changing that to an automatic policy is
// a hardware decision - bench-verify back-drive behaviour first.
void setEnabled(bool enabled);
bool isEnabled();

void setSpeedInHz(uint32_t hz);
void setAcceleration(uint32_t steps_per_s2);

// Non-blocking: starts the move in a background task, returns immediately.
void moveTo(int32_t absolute_position);
void move(int32_t relative_steps);

bool isRunning();
int32_t getCurrentPosition();
void setCurrentPosition(int32_t position);

// Requests an immediate stop. Takes effect within one cruise-phase chunk
// (see kCruiseChunkMs) - not instantaneous, but bounded and small.
void forceStop();

}  // namespace stepper
