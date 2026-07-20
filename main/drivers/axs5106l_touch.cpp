#include "axs5106l_touch.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "axs5106l";

#define AXS5106L_ADDR 0x63
#define AXS5106L_ID_REG 0x08
#define AXS5106L_TOUCH_DATA_REG 0x01

static i2c_master_dev_handle_t s_dev = nullptr;
static uint16_t s_width = 0, s_height = 0, s_rotation = 0;
static volatile bool s_int_flag = false;
static axs5106l_touch_data_t s_data = {};

static void IRAM_ATTR touch_isr(void *) {
  s_int_flag = true;
}

static bool i2c_read_reg(uint8_t reg_addr, uint8_t *data, size_t length) {
  // The AXS5106L does NOT support I2C repeated-start: the register-pointer
  // write must be terminated with a STOP before the read's START. Using
  // i2c_master_transmit_receive() (which issues a repeated-start) makes the
  // chip NACK every register read - the address-only probe still ACKs, which
  // is why the controller looked present but its ID and touch registers all
  // read back 0/garbage. Two separate transactions reproduce the original
  // Arduino driver's write(reg)+endTransmission()  /  requestFrom() sequence.
  esp_err_t err = i2c_master_transmit(s_dev, &reg_addr, 1, -1);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "I2C write reg 0x%02x failed: %s", reg_addr, esp_err_to_name(err));
    return false;
  }
  err = i2c_master_receive(s_dev, data, length, -1);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "I2C read reg 0x%02x failed: %s", reg_addr, esp_err_to_name(err));
    return false;
  }
  return true;
}

void axs5106l_touch_init(i2c_master_bus_handle_t i2c_bus, gpio_num_t rst_gpio, gpio_num_t int_gpio,
                         uint16_t rotation, uint16_t width, uint16_t height) {
  s_width = width;
  s_height = height;
  s_rotation = rotation;

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = AXS5106L_ADDR;
  dev_cfg.scl_speed_hz = 100000;  // conservative for first bring-up; bump once stable
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_dev));

  gpio_config_t rst_cfg = {};
  rst_cfg.pin_bit_mask = 1ULL << rst_gpio;
  rst_cfg.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&rst_cfg));
  gpio_set_level(rst_gpio, 0);
  vTaskDelay(pdMS_TO_TICKS(200));
  gpio_set_level(rst_gpio, 1);
  vTaskDelay(pdMS_TO_TICKS(500));  // AXS5106L boot time after reset release

  gpio_config_t int_cfg = {};
  int_cfg.pin_bit_mask = 1ULL << int_gpio;
  int_cfg.mode = GPIO_MODE_INPUT;
  int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  int_cfg.intr_type = GPIO_INTR_NEGEDGE;
  ESP_ERROR_CHECK(gpio_config(&int_cfg));
  gpio_install_isr_service(
      0);  // ok if already installed elsewhere: returns ESP_ERR_INVALID_STATE, ignored
  gpio_isr_handler_add(int_gpio, touch_isr, nullptr);

  esp_err_t probe_err = i2c_master_probe(i2c_bus, AXS5106L_ADDR, 100);
  ESP_LOGI(TAG, "I2C probe @0x%02x: %s", AXS5106L_ADDR, esp_err_to_name(probe_err));

  uint8_t id[3] = {0};
  if (i2c_read_reg(AXS5106L_ID_REG, id, sizeof(id)) && id[0] != 0) {
    ESP_LOGI(TAG, "AXS5106L id: %02x %02x %02x", id[0], id[1], id[2]);
  } else {
    ESP_LOGW(TAG, "AXS5106L id read returned 0 - check wiring/reset");
  }
}

void axs5106l_touch_read(void) {
  s_data.touch_num = 0;
  if (!s_int_flag) return;
  s_int_flag = false;

  uint8_t data[14] = {0};
  if (!i2c_read_reg(AXS5106L_TOUCH_DATA_REG, data, sizeof(data))) return;

  s_data.touch_num = data[1];
  if (s_data.touch_num == 0 || s_data.touch_num > AXS5106L_MAX_TOUCH_POINTS) {
    s_data.touch_num = 0;
    return;
  }
  for (uint8_t i = 0; i < s_data.touch_num; i++) {
    s_data.coords[i].x = ((uint16_t)(data[2 + i * 6] & 0x0f)) << 8;
    s_data.coords[i].x |= data[3 + i * 6];
    s_data.coords[i].y = ((uint16_t)(data[4 + i * 6] & 0x0f)) << 8;
    s_data.coords[i].y |= data[5 + i * 6];
  }
}

bool axs5106l_touch_get_coordinates(axs5106l_touch_data_t *out) {
  if (out == nullptr || s_data.touch_num == 0) return false;

  for (int i = 0; i < s_data.touch_num; i++) {
    switch (s_rotation) {
      case 1:
        out->coords[i].y = s_data.coords[i].x;
        out->coords[i].x = s_data.coords[i].y;
        break;
      case 2:
        out->coords[i].x = s_data.coords[i].x;
        out->coords[i].y = s_height - 1 - s_data.coords[i].y;
        break;
      case 3:
        out->coords[i].y = s_height - 1 - s_data.coords[i].x;
        out->coords[i].x = s_width - 1 - s_data.coords[i].y;
        break;
      default:
        out->coords[i].x = s_width - 1 - s_data.coords[i].x;
        out->coords[i].y = s_data.coords[i].y;
        break;
    }
  }
  out->touch_num = s_data.touch_num;
  return true;
}
