#pragma once

#include <cstdint>
#include "driver/gpio.h"

// Native RMT/PCNT-based STEP/DIR pulse generator, replacing FastAccelStepper
// (which turned out to require Arduino-as-an-ESP-IDF-component - see ADR
// 0001 and docs/PLAN.md Phase 4). Exposes the small subset of
// FastAccelStepper's API src/motion.cpp actually used.
//
// *** UNVERIFIED ON HARDWARE - build-verified only. No motor/TMC5160 rig was
// connected while this was written. Before trusting this on the real press,
// bench-verify: a move actually reaches its target, isRunning()/
// getCurrentPosition() track reality (via the PCNT hardware step counter),
// and forceStop() halts pulse output within its documented latency bound
// (one cruise-phase chunk, see stepper.cpp's kCruiseChunkMs). ***
namespace stepper {

void init(gpio_num_t step_gpio, gpio_num_t dir_gpio);

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
