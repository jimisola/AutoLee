# Sources are grouped by concern (drivers / motion / net / ui) rather than sitting
# flat. Every group is also on INCLUDE_DIRS, so `#include "motion.h"` keeps working
# regardless of which group the including file lives in - the grouping is for
# humans navigating the tree, not a module boundary the compiler enforces.
idf_component_register(
    SRCS "app_main.cpp" "globals.cpp"
         "drivers/display_touch.cpp" "drivers/axs5106l_touch.cpp"
         "drivers/tmc5160_hal.cpp" "drivers/tmc5160_ctrl.cpp"
         "drivers/stepper.cpp" "drivers/stepper_motor_encoder.c"
         "motion/motion.cpp"
         "net/wifi_mgr.cpp" "net/web_server.cpp"
         "ui/ui_touch.cpp"
         "../lib/tmc_api/TMC5160/TMC5160.c" "../lib/tmc_api/helpers/CRC.c" "../lib/tmc_api/helpers/Functions.c"
    INCLUDE_DIRS "." "drivers" "motion" "net" "ui"
                 "../lib/tmc_api/TMC5160" "../lib/tmc_api/helpers"
    REQUIRES autolee_logic nvs_flash esp_lcd driver esp_timer esp_driver_rmt esp_driver_pcnt
             esp_driver_gpio esp_driver_spi esp_driver_i2c esp_system esp_wifi esp_netif
             esp_event nvs_flash dns_server psychic_http esp_https_server esp_http_server
             app_update
)
