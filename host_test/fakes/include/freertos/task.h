// Host stub for freertos/task.h - vTaskDelay only. Implemented in fake_hw.cpp,
// where it advances the fake clock (and therefore the simulated stepper).
#pragma once

#include "freertos/FreeRTOS.h"

void vTaskDelay(TickType_t ticks);
