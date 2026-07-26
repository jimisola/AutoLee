// Host stub for freertos/FreeRTOS.h - see host_test/fakes/README-ish note in
// host_test/fakes/fake_hw.h. Only the handful of primitives main/motion/ uses.
#pragma once

// The real freertos/FreeRTOS.h pulls in stddef.h/stdlib.h, which is where
// main/config.h gets size_t and motion.cpp gets labs() from - mirror that so
// the firmware sources compile unchanged on the host.
#include <cstddef>
#include <cstdint>
#include <cstdlib>

typedef uint32_t TickType_t;

// Opaque handle, same as the real freertos/task.h - only ever compared/passed
// through on the host (main/globals.h's g_pump_task_handle), never dereferenced.
typedef void *TaskHandle_t;

// Same tick rate the firmware runs at (CONFIG_FREERTOS_HZ=100), so the
// pdMS_TO_TICKS rounding behaviour motion.cpp's delay() compensates for is
// reproduced faithfully on the host.
#define configTICK_RATE_HZ 100
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * configTICK_RATE_HZ) / 1000))

// portMUX_TYPE / portENTER_CRITICAL: the host build is single-threaded, so the
// spinlock only needs to track nesting depth. fake_hw tracks that depth and
// flags any stepper/TMC/webLog call made inside a critical section, which is
// exactly what motion_state.h forbids.
typedef struct {
  int depth;
} portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED {0}

namespace fake {
void enter_critical();
void exit_critical();
}  // namespace fake

#define portENTER_CRITICAL(mux) \
  do {                          \
    (void)(mux);                \
    fake::enter_critical();     \
  } while (0)
#define portEXIT_CRITICAL(mux) \
  do {                         \
    (void)(mux);               \
    fake::exit_critical();     \
  } while (0)
