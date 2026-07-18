idf_component_register(
    SRCS "app_main.cpp" "display_touch.cpp" "axs5106l_touch.cpp" "tmc5160_hal.cpp" "tmc5160_ctrl.cpp"
         "stepper.cpp" "stepper_motor_encoder.c" "motion.cpp" "globals.cpp"
         "../lib/tmc_api/TMC5160/TMC5160.c" "../lib/tmc_api/helpers/CRC.c" "../lib/tmc_api/helpers/Functions.c"
    INCLUDE_DIRS "." "../lib/tmc_api/TMC5160" "../lib/tmc_api/helpers"
    REQUIRES autolee_logic nvs_flash esp_lcd driver esp_timer esp_driver_rmt esp_driver_pcnt
             esp_driver_gpio esp_driver_spi esp_driver_i2c esp_system
)
