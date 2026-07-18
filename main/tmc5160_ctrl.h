#pragma once

#include <cstdint>
#include "driver/spi_master.h"

// Thin wrapper over TMC-API's raw register access, exposing the same
// operations src/motion.cpp used via TMCStepper's TMC5160Stepper class - see
// docs/PLAN.md Phase 4. Single-motor (icID always 0).
namespace tmc5160 {

// Bring up the SPI device + apply the same GCONF/CHOPCONF/COOLCONF/IHOLD_IRUN
// defaults src/main.cpp's setup() applied via TMCStepper (toff=5, 16
// microsteps, SpreadCycle not StealthChop, DIAG1=stall/push-pull).
void init(spi_host_device_t spi_host, float r_sense_ohm);

void rms_current(uint16_t mA);
void en_pwm_mode(bool enabled);
void TPWMTHRS(uint32_t threshold);
void TCOOLTHRS(uint32_t threshold);
void semin(uint8_t value);
void semax(uint8_t value);
void seup(uint8_t value);
void sedn(uint8_t value);
void sgt(int8_t value);

uint32_t DRV_STATUS();
// Bits 0-9 of DRV_STATUS: the raw, unfiltered StallGuard2 result.
uint16_t SG_RESULT();

}  // namespace tmc5160
