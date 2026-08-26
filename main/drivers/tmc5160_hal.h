#pragma once

#include "driver/spi_master.h"

// Add the TMC5160 as a device on an already-initialized SPI bus (shared with
// the display - see main/display_touch.cpp). Call once before any
// tmc5160_readRegister/tmc5160_writeRegister call.
void tmc5160_hal_init(spi_host_device_t host);
