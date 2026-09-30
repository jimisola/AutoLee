#pragma once

#include <cstdint>
#include "driver/gpio.h"

// Native RMT/PCNT-based STEP/DIR pulse generator, replacing FastAccelStepper
// (which turned out to require Arduino-as-an-ESP-IDF-component - see ADR
// 0001 and docs/PLAN.md Phase 4). Exposes the small subset of
// FastAccelStepper's API the original motion logic actually used, with the
// behaviour the Arduino firmware got from it: DIR low counts up
// (setDirectionPin(DIR_PIN, false)), a constant-acceleration ramp from rest
// (lib/autolee_logic/step_ramp.h), moveTo() mid-move retargets by braking
// first, and the driver is energised only around moves.
//
// *** UNVERIFIED ON A PRESS. The ESP-IDF port's first run on a real press
// failed with the motor; this layer is what differed from the Arduino
// firmware (#104). Bench-verify direction, reaching target, isRunning()/
// getCurrentPosition() against reality, and stop latency before trusting it. ***
namespace stepper {

// `enable_gpio` drives the TMC5160's DRV_ENN input (active low - see
// docs/wiring.md). The driver starts de-energised; each move energises it and
// it drops STEPPER_DISABLE_DELAY_MS after the last one (FastAccelStepper's
// setAutoEnable() + setDelayToDisable() in the Arduino firmware).
void init(gpio_num_t step_gpio, gpio_num_t dir_gpio, gpio_num_t enable_gpio);

// Energise (true) or de-energise (false) the driver's output stage now. The
// next move energises it again regardless.
void setEnabled(bool enabled);
bool isEnabled();

void setSpeedInHz(uint32_t hz);
void setAcceleration(uint32_t steps_per_s2);

// Non-blocking: starts the move in a background task, returns immediately.
// Called during a move, it retargets it - braking to rest first if the new
// target is behind the direction of travel.
void moveTo(int32_t absolute_position);
void move(int32_t relative_steps);

bool isRunning();
int32_t getCurrentPosition();
void setCurrentPosition(int32_t position);

// Stops without a ramp and cancels any retarget requested before it. Steps
// already queued (at most ~4 ms, see stepper.cpp's kChunkMaxUs) still go out.
void forceStop();

}  // namespace stepper
