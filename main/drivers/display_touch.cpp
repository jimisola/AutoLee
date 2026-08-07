#include "display_touch.h"

// Explicit since ESP-IDF 6.0: vTaskDelay()/pdMS_TO_TICKS() below used to arrive
// transitively through one of the esp_lcd/driver headers. 6.0 tightened those
// includes, so name the dependency rather than relying on it being re-exported.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_io_spi.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "axs5106l_touch.h"

#include "config.h"  // SCR_W, SCR_H, ROTATION, GFX_BL, Touch_* pins

static void touchpad_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  axs5106l_touch_data_t t;
  axs5106l_touch_read();
  if (axs5106l_touch_get_coordinates(&t)) {
    data->point.x = t.coords[0].x;
    data->point.y = t.coords[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static const char *TAG = "display_touch";

// Shared SPI bus (ST7789 + TMC5160, see CLAUDE.md "Shared SPI bus"): SCK=1, MOSI=2.
#define LCD_SCK_GPIO 1
#define LCD_MOSI_GPIO 2
#define LCD_MISO_GPIO 3  // unused by the display (write-only); read back by the TMC5160 in Phase 4
// GPIO_NUM_* rather than bare ints: ESP-IDF 6.0 typed esp_lcd's cs/dc/reset
// config members as gpio_num_t, and C++ will not implicitly narrow an int into
// that enum. The bus pins above stay plain ints - spi_bus_config_t's members
// are still int, and gpio_num_t converts to int implicitly.
#define LCD_DC_GPIO GPIO_NUM_15
#define LCD_CS_GPIO GPIO_NUM_14
#define LCD_RST_GPIO GPIO_NUM_22

#define LCD_SPI_HOST SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

// The panel is actually a JD9853 (ST7789-command-compatible but needing this
// exact vendor init), not a plain ST7789 - esp_lcd_new_panel_st7789()'s
// built-in default init won't produce a correct picture. This is a
// byte-for-byte port of src/ui_touch.cpp's `init_operations` (Arduino_GFX
// batchOperation format) - same panel, same bring-up, just replayed via
// esp_lcd_panel_io_tx_param() instead of Arduino_GFX's SPI bus. Do not
// "clean up" the magic numbers below; they are the vendor's register values.
// clang-format off
static const uint8_t jd9853_b7[] = {0x00, 0x47, 0x00, 0x6F};
static const uint8_t jd9853_bb[] = {0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0};
static const uint8_t jd9853_c3[] = {0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77};
static const uint8_t jd9853_c4[] = {0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16,
                                     0x79, 0x0B, 0x0A, 0x16, 0x82};
static const uint8_t jd9853_c8[] = {0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26,
                                     0x25, 0x17, 0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29,
                                     0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D,
                                     0x04, 0x00};
static const uint8_t jd9853_d0[] = {0x04, 0x06, 0x6B, 0x0F, 0x00};
static const uint8_t jd9853_b7_2[] = {0x03, 0x13, 0xEF, 0x35, 0x35};
static const uint8_t jd9853_c1[] = {0x14, 0x15, 0xC0};
static const uint8_t jd9853_e5_a[] = {0x00, 0x02, 0x00};
static const uint8_t jd9853_e5_b[] = {0x01, 0x02, 0x00};
static const uint8_t jd9853_2a[] = {0x00, 0x22, 0x00, 0xCD}; // matches set_gap(34, 0)
static const uint8_t jd9853_2b[] = {0x00, 0x00, 0x01, 0x3F};

static void jd9853_send_init_sequence(esp_lcd_panel_io_handle_t io) {
    esp_lcd_panel_io_tx_param(io, 0x11, nullptr, 0); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t d16[2];
    d16[0] = 0x98; d16[1] = 0x53; esp_lcd_panel_io_tx_param(io, 0xDF, d16, 2);
    esp_lcd_panel_io_tx_param(io, 0xB2, (const uint8_t[]){0x23}, 1);
    esp_lcd_panel_io_tx_param(io, 0xB7, jd9853_b7, sizeof(jd9853_b7));
    esp_lcd_panel_io_tx_param(io, 0xBB, jd9853_bb, sizeof(jd9853_bb));
    d16[0] = 0x44; d16[1] = 0xA4; esp_lcd_panel_io_tx_param(io, 0xC0, d16, 2);
    esp_lcd_panel_io_tx_param(io, 0xC1, (const uint8_t[]){0x16}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC3, jd9853_c3, sizeof(jd9853_c3));
    esp_lcd_panel_io_tx_param(io, 0xC4, jd9853_c4, sizeof(jd9853_c4));
    esp_lcd_panel_io_tx_param(io, 0xC8, jd9853_c8, sizeof(jd9853_c8));
    esp_lcd_panel_io_tx_param(io, 0xD0, jd9853_d0, sizeof(jd9853_d0));
    d16[0] = 0x00; d16[1] = 0x30; esp_lcd_panel_io_tx_param(io, 0xD7, d16, 2);
    esp_lcd_panel_io_tx_param(io, 0xE6, (const uint8_t[]){0x14}, 1);
    esp_lcd_panel_io_tx_param(io, 0xDE, (const uint8_t[]){0x01}, 1);
    esp_lcd_panel_io_tx_param(io, 0xB7, jd9853_b7_2, sizeof(jd9853_b7_2));
    esp_lcd_panel_io_tx_param(io, 0xC1, jd9853_c1, sizeof(jd9853_c1));
    d16[0] = 0x06; d16[1] = 0x3A; esp_lcd_panel_io_tx_param(io, 0xC2, d16, 2);
    d16[0] = 0x72; d16[1] = 0x12; esp_lcd_panel_io_tx_param(io, 0xC4, d16, 2);
    esp_lcd_panel_io_tx_param(io, 0xBE, (const uint8_t[]){0x00}, 1);
    esp_lcd_panel_io_tx_param(io, 0xDE, (const uint8_t[]){0x02}, 1);
    esp_lcd_panel_io_tx_param(io, 0xE5, jd9853_e5_a, sizeof(jd9853_e5_a));
    esp_lcd_panel_io_tx_param(io, 0xE5, jd9853_e5_b, sizeof(jd9853_e5_b));
    esp_lcd_panel_io_tx_param(io, 0xDE, (const uint8_t[]){0x00}, 1);
    esp_lcd_panel_io_tx_param(io, 0x35, (const uint8_t[]){0x00}, 1); // tearing effect line ON
    esp_lcd_panel_io_tx_param(io, 0x3A, (const uint8_t[]){0x05}, 1); // COLMOD: 16bpp
    esp_lcd_panel_io_tx_param(io, 0x2A, jd9853_2a, sizeof(jd9853_2a));
    esp_lcd_panel_io_tx_param(io, 0x2B, jd9853_2b, sizeof(jd9853_2b));
    esp_lcd_panel_io_tx_param(io, 0xDE, (const uint8_t[]){0x02}, 1);
    esp_lcd_panel_io_tx_param(io, 0xE5, jd9853_e5_a, sizeof(jd9853_e5_a));
    esp_lcd_panel_io_tx_param(io, 0xDE, (const uint8_t[]){0x00}, 1);
    esp_lcd_panel_io_tx_param(io, 0x36, (const uint8_t[]){0x00}, 1); // MADCTL: rotation 0

    esp_lcd_panel_io_tx_param(io, 0x21, nullptr, 0); // INVON (this panel needs inversion on)
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_io_tx_param(io, 0x29, nullptr, 0); // DISPON
}
// clang-format on

// Mirrors the original Arduino firmware:
//   Arduino_ST7789(bus, /*rst=*/22, /*rotation=*/0, /*ips=*/false,
//                  /*w=*/172, /*h=*/320, /*colOffset=*/34, /*rowOffset=*/0, 34, 0)
static esp_lcd_panel_handle_t init_display_panel(esp_lcd_panel_io_handle_t *out_io) {
  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = LCD_SCK_GPIO;
  buscfg.mosi_io_num = LCD_MOSI_GPIO;
  buscfg.miso_io_num = LCD_MISO_GPIO;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = SCR_W * 40 * sizeof(uint16_t);
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = LCD_CS_GPIO;
  io_config.dc_gpio_num = LCD_DC_GPIO;
  io_config.spi_mode = 0;
  io_config.pclk_hz = LCD_PIXEL_CLOCK_HZ;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;

  esp_lcd_panel_io_handle_t io_handle = nullptr;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = LCD_RST_GPIO;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;  // MADCTL=0x00 in the vendor init below
  panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;  // pairs with lv_conf.h's LV_COLOR_16_SWAP=1
  panel_config.bits_per_pixel = 16;

  esp_lcd_panel_handle_t panel_handle = nullptr;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  // Deliberately not calling esp_lcd_panel_init(): this panel is a JD9853
  // (ST7789-command-compatible but needs its own vendor init), and
  // jd9853_send_init_sequence() below - a byte-for-byte port of
  // src/ui_touch.cpp's `init_operations` - already does SLPOUT, all vendor
  // registers, COLMOD, MADCTL (rotation 0), INVON, and DISPON. Calling the
  // generic ST7789 init first would be redundant at best.
  jd9853_send_init_sequence(io_handle);
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 34, 0));

  *out_io = io_handle;
  return panel_handle;
}

static i2c_master_bus_handle_t init_touch_i2c_bus(void) {
  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = (gpio_num_t)Touch_I2C_SDA;
  bus_config.scl_io_num = (gpio_num_t)Touch_I2C_SCL;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  i2c_master_bus_handle_t bus_handle = nullptr;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
  return bus_handle;
}

lv_display_t *display_touch_init(void) {
  gpio_config_t bl_cfg = {};
  bl_cfg.pin_bit_mask = 1ULL << GFX_BL;
  // INPUT_OUTPUT rather than plain OUTPUT: GPIO_MODE_OUTPUT leaves the input
  // buffer disabled, so gpio_get_level() on this pin reads 0 whatever is
  // actually being driven - which makes it useless for telling "the backlight
  // is off" from "the backlight is on and something else is wrong". Same drive
  // strength and behaviour otherwise; it only makes the pad readable.
  bl_cfg.mode = GPIO_MODE_INPUT_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&bl_cfg));
  gpio_set_level((gpio_num_t)GFX_BL, 0);  // keep off until content is drawn

  esp_lcd_panel_io_handle_t io_handle = nullptr;
  esp_lcd_panel_handle_t panel_handle = init_display_panel(&io_handle);

  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

  lvgl_port_display_cfg_t disp_cfg = {};
  disp_cfg.io_handle = io_handle;
  disp_cfg.panel_handle = panel_handle;
  disp_cfg.buffer_size = SCR_W * 40;
  disp_cfg.double_buffer = true;
  disp_cfg.hres = SCR_W;
  disp_cfg.vres = SCR_H;
  disp_cfg.monochrome = false;
  disp_cfg.rotation.swap_xy = false;
  disp_cfg.rotation.mirror_x = false;
  disp_cfg.rotation.mirror_y = false;
  disp_cfg.flags.buff_dma = true;

  lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
  if (!disp) {
    ESP_LOGE(TAG, "lvgl_port_add_disp failed");
    return nullptr;
  }

  i2c_master_bus_handle_t i2c_bus = init_touch_i2c_bus();
  axs5106l_touch_init(i2c_bus, (gpio_num_t)Touch_RST, (gpio_num_t)Touch_INT, ROTATION, SCR_W,
                      SCR_H);

  static lv_indev_drv_t indev_drv;  // static: LVGL keeps a pointer to this past return
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read_cb;
  indev_drv.disp = disp;
  lv_indev_drv_register(&indev_drv);
  ESP_LOGI(TAG, "AXS5106L touch ready");

  gpio_set_level((gpio_num_t)GFX_BL, 1);
  return disp;
}
