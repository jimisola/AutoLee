// Host stub for esp_timer.h - the monotonic microsecond clock motion.cpp's
// millis() helper is built on. Backed by the fake clock in fake_hw.cpp.
#pragma once

#include <cstdint>

int64_t esp_timer_get_time(void);
