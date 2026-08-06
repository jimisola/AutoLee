// Host stub for esp_task_wdt.h. The host build has no watchdog; the fake just
// counts feeds so a test can assert the blocking loops keep feeding it.
#pragma once

typedef int esp_err_t;

esp_err_t esp_task_wdt_reset(void);
