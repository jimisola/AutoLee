// Host stub for driver/spi_master.h - only the host enum used in
// tmc5160::init()'s signature.
#pragma once

typedef enum {
  SPI1_HOST = 0,
  SPI2_HOST = 1,
  SPI3_HOST = 2,
} spi_host_device_t;
