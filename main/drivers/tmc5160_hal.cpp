// TMC-API hardware-abstraction callbacks for the TMC5160, over the shared SPI
// bus (see CLAUDE.md "Shared SPI bus"). Display CS is forced HIGH before every
// transfer to avoid bus contention - ported from the original Arduino
// firmware's read_sg_raw().
#include <cstring>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "TMC5160.h"
}

#include "config.h"  // TMC_CS

#define LCD_CS_GPIO 14  // must match main/display_touch.cpp

static spi_device_handle_t s_tmc_spi = nullptr;

void tmc5160_hal_init(spi_host_device_t host) {
  spi_device_interface_config_t dev_cfg = {};
  dev_cfg.clock_speed_hz = 4 * 1000 * 1000;  // TMC5160 SPI max ~4MHz for reliable reads
  dev_cfg.mode = 3;                          // TMC5160 requires SPI mode 3 (CPOL=1, CPHA=1)
  dev_cfg.spics_io_num = -1;  // CS handled manually (shared-bus contention with the display)
  dev_cfg.queue_size = 1;

  gpio_config_t cs_cfg = {};
  cs_cfg.pin_bit_mask = 1ULL << TMC_CS;
  cs_cfg.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&cs_cfg));
  gpio_set_level((gpio_num_t)TMC_CS, 1);

  ESP_ERROR_CHECK(spi_bus_add_device(host, &dev_cfg, &s_tmc_spi));
}

extern "C" void tmc5160_readWriteSPI(uint16_t icID, uint8_t *data, size_t dataLength) {
  (void)icID;
  if (s_tmc_spi == nullptr || data == nullptr || dataLength == 0) return;

  // TMC-API expects the response written back into `data` in-place; the SPI
  // driver doesn't guarantee tx_buffer == rx_buffer is safe, so use a
  // separate rx buffer and copy back.
  uint8_t rx[8];  // TMC5160 datagrams are at most 5 bytes; 8 is headroom
  if (dataLength > sizeof(rx)) return;
  spi_transaction_t t = {};
  t.length = dataLength * 8;
  t.tx_buffer = data;
  t.rx_buffer = rx;

  // Hold the bus for the whole CS-low..CS-high window. The display shares this
  // SPI bus, and its LVGL flush runs on a different task: without the lock, an
  // esp_lcd transfer can be interleaved between our CS toggle and our transfer,
  // corrupting a StallGuard read - i.e. silently feeding the jam detector
  // garbage. acquire_bus() also blocks the display's transfers for the
  // duration, which is what makes the manual CS handling below safe.
  spi_device_acquire_bus(s_tmc_spi, portMAX_DELAY);

  // Display CS forced high before every StallGuard/TMC SPI transfer to
  // prevent bus contention - preserve this when touching SPI/SG code.
  gpio_set_level((gpio_num_t)LCD_CS_GPIO, 1);
  esp_rom_delay_us(10);  // let the bus settle after display CS release

  gpio_set_level((gpio_num_t)TMC_CS, 0);
  spi_device_polling_transmit(s_tmc_spi, &t);
  gpio_set_level((gpio_num_t)TMC_CS, 1);

  spi_device_release_bus(s_tmc_spi);

  memcpy(data, rx, dataLength);
}

extern "C" bool tmc5160_readWriteUART(uint16_t, uint8_t *, size_t, size_t) {
  return false;  // this board uses SPI only
}

extern "C" TMC5160BusType tmc5160_getBusType(uint16_t) {
  return IC_BUS_SPI;
}

extern "C" uint8_t tmc5160_getNodeAddress(uint16_t) {
  return 0;
}  // UART-only, unused
