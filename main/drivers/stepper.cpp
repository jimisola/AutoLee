#include "stepper.h"
#include "config.h"
#include "step_ramp.h"

#include <atomic>
#include <cmath>
#include "driver/rmt_tx.h"
#include "driver/pulse_cnt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

namespace stepper {

static const char *TAG = "stepper";

// 1 tick = 1 us. A symbol's two halves are 15-bit, so the longest step is
// ~65 ms (~15 Hz) - below the first step of any ramp the press uses.
static constexpr uint32_t kResolutionHz = 1'000'000;
static constexpr uint32_t kMinStepTicks = 2;
static constexpr uint32_t kMaxStepTicks = 65534;
// Steps per RMT transmission, and its longest duration. Two are in flight at a
// time, and stop/retarget act between transmissions, so both take effect
// within ~2 x kChunkMaxUs (~4 ms; FastAccelStepper's forceStop() was
// near-immediate, and every step after a jam pushes further into it).
static constexpr size_t kChunkMaxSteps = 128;
static constexpr uint32_t kChunkMaxUs = 2'000;
// The Arduino firmware's FastAccelStepper setDirectionPin(DIR_PIN, false):
// DIR low is the counting-up direction.
static constexpr int kDirLevelUp = 0;

// PCNT hardware counter window. The unit is configured with accumulation
// enabled (flags.accum_count) plus watch points on both limits, so each time
// the hardware counter hits +-kPcntLimit the driver folds that span into a
// software accumulator and pcnt_unit_get_count() keeps returning the true
// running total. The effective position range is therefore int32_t, not
// +-kPcntLimit.
static constexpr int kPcntLimit = STEP_COUNT_WRAP;

static rmt_channel_handle_t s_rmt_chan = nullptr;
static rmt_encoder_handle_t s_copy_encoder = nullptr;
static pcnt_unit_handle_t s_pcnt_unit = nullptr;
static gpio_num_t s_dir_gpio = GPIO_NUM_NC;
static gpio_num_t s_enable_gpio = GPIO_NUM_NC;
// DRV_ENN is active low: level 0 = driver energised.
static constexpr int kEnableAsserted = 0;
static constexpr int kEnableDeasserted = 1;
static std::atomic<bool> s_enabled{false};

static std::atomic<uint32_t> s_speedHz{1000};
static std::atomic<uint32_t> s_accel{4000};  // steps/s^2
static std::atomic<bool> s_running{false};
static std::atomic<bool> s_stopRequested{false};
static std::atomic<int32_t> s_targetPosition{0};
static std::atomic<int32_t> s_zeroOffset{0};  // getCurrentPosition() = pcnt_count + s_zeroOffset

static TaskHandle_t s_moveTask = nullptr;
static SemaphoreHandle_t s_moveRequest = nullptr;  // binary: a move is pending
static SemaphoreHandle_t s_chunkDone = nullptr;    // counting: a transmission finished

static rmt_symbol_word_t s_chunk[2][kChunkMaxSteps];

static int32_t pcnt_position() {
  int count = 0;
  pcnt_unit_get_count(s_pcnt_unit, &count);
  return count + s_zeroOffset.load();
}

static bool IRAM_ATTR on_chunk_done(rmt_channel_handle_t, const rmt_tx_done_event_data_t *,
                                    void *) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_chunkDone, &woken);
  return woken == pdTRUE;
}

// Fills `out` with the next steps of `ramp`, up to a chunk's worth. Stops at a
// point of rest, since the direction can only change there and DIR has to be
// set between transmissions. Returns the step count; `dir` gets their
// direction. `carry` holds the sub-microsecond remainder, so rounding to whole
// ticks does not shift the average step rate.
static size_t fill_chunk(rmt_symbol_word_t *out, autolee::StepRamp &ramp, float &carry,
                         int8_t &dir) {
  const int32_t target = s_targetPosition.load();
  const uint32_t hz = s_speedHz.load();
  const uint32_t accel = s_accel.load();
  const float vmax = hz < 1 ? 1.0f : (float)hz;
  const float a = accel < 1 ? 1.0f : (float)accel;

  size_t n = 0;
  uint32_t us = 0;
  dir = 0;
  while (n < kChunkMaxSteps && us < kChunkMaxUs) {
    if (n > 0 && ramp.v <= 0.0f) break;
    const float seconds = autolee::nextStep(ramp, target, vmax, a);
    if (seconds <= 0.0f) break;
    if (n == 0) dir = ramp.dir;

    const float exact = seconds * (float)kResolutionHz + carry;
    long ticks = lroundf(exact);
    if (ticks < (long)kMinStepTicks) ticks = kMinStepTicks;
    if (ticks > (long)kMaxStepTicks) ticks = kMaxStepTicks;
    carry = exact - (float)ticks;

    const uint32_t high = (uint32_t)ticks / 2;
    out[n].level0 = 1;  // rising edge first: PCNT and the TMC5160 step on it
    out[n].duration0 = high;
    out[n].level1 = 0;
    out[n].duration1 = (uint32_t)ticks - high;
    n++;
    us += (uint32_t)ticks;
  }
  return n;
}

// Runs the ramp to the current target, reading the target, speed and
// acceleration again for every chunk - so a moveTo() mid-move retargets the
// same ramp (braking first if the new target is behind) rather than starting
// a new one. Returns at rest on target, or after a forceStop().
static void run_move() {
  autolee::StepRamp ramp;
  ramp.pos = pcnt_position();
  float carry = 0.0f;
  int8_t dirLevelSet = 0;  // direction DIR currently encodes; 0 = not set this move
  int inFlight = 0;
  int slot = 0;
  const rmt_transmit_config_t tx_cfg = {};  // line idles low after each transmission

  while (!s_stopRequested.load()) {
    if (inFlight == 2) {  // wait for the older transmission to free its buffer
      xSemaphoreTake(s_chunkDone, portMAX_DELAY);
      inFlight--;
      if (s_stopRequested.load()) break;  // a stop that came in meanwhile
    }
    int8_t dir = 0;
    const size_t n = fill_chunk(s_chunk[slot], ramp, carry, dir);
    if (n == 0) break;  // at rest on target

    if (dir != dirLevelSet) {
      // Only at a point of rest: let the steps in flight finish before DIR moves.
      while (inFlight > 0) {
        xSemaphoreTake(s_chunkDone, portMAX_DELAY);
        inFlight--;
      }
      gpio_set_level(s_dir_gpio, dir > 0 ? kDirLevelUp : !kDirLevelUp);
      dirLevelSet = dir;
    }
    rmt_transmit(s_rmt_chan, s_copy_encoder, s_chunk[slot], n * sizeof(rmt_symbol_word_t), &tx_cfg);
    inFlight++;
    slot ^= 1;
  }
  while (inFlight > 0) {
    xSemaphoreTake(s_chunkDone, portMAX_DELAY);
    inFlight--;
  }
}

static void move_task(void *) {
  TickType_t idleSince = xTaskGetTickCount();
  for (;;) {
    // Idle: wait for a move - or, while energised, only until the hold delay
    // runs out (the Arduino firmware's setDelayToDisable()).
    TickType_t wait = portMAX_DELAY;
    if (s_enabled.load()) {
      const TickType_t hold = pdMS_TO_TICKS(STEPPER_DISABLE_DELAY_MS);
      const TickType_t idle = xTaskGetTickCount() - idleSince;
      wait = idle >= hold ? 0 : hold - idle;
    }
    if (xSemaphoreTake(s_moveRequest, wait) != pdTRUE) {
      setEnabled(false);
      continue;
    }
    // Reset here, in the single task that owns it, rather than in moveTo(): a
    // producer clearing it could otherwise wipe a stop another caller had just
    // requested (review finding #7).
    s_stopRequested.store(false);
    s_running.store(true);
    if (!s_enabled.load()) setEnabled(true);  // setAutoEnable(true)

    run_move();

    // A moveTo() that arrived mid-move has been folded into the ramp already,
    // but left a request pending; loop straight back rather than read as idle
    // for an instant - handleMotion() would take that as "move complete".
    if (uxSemaphoreGetCount(s_moveRequest) == 0) s_running.store(false);
    idleSince = xTaskGetTickCount();
  }
}

void init(gpio_num_t step_gpio, gpio_num_t dir_gpio, gpio_num_t enable_gpio) {
  s_dir_gpio = dir_gpio;
  gpio_config_t dir_cfg = {};
  dir_cfg.pin_bit_mask = 1ULL << dir_gpio;
  dir_cfg.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&dir_cfg));
  gpio_set_level(dir_gpio, kDirLevelUp);

  // DRV_ENN is driven, and starts de-energised: moves energise it and it drops
  // STEPPER_DISABLE_DELAY_MS after the last one, as FastAccelStepper's
  // setAutoEnable() + setDelayToDisable() did in the Arduino firmware.
  s_enable_gpio = enable_gpio;
  gpio_config_t en_cfg = {};
  en_cfg.pin_bit_mask = 1ULL << enable_gpio;
  en_cfg.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&en_cfg));
  setEnabled(false);

  rmt_tx_channel_config_t tx_cfg = {};
  tx_cfg.gpio_num = step_gpio;
  tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_cfg.resolution_hz = kResolutionHz;
  tx_cfg.mem_block_symbols = 64;
  tx_cfg.trans_queue_depth = 4;
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_rmt_chan));
  const rmt_tx_event_callbacks_t cbs = {.on_trans_done = on_chunk_done};
  ESP_ERROR_CHECK(rmt_tx_register_event_callbacks(s_rmt_chan, &cbs, nullptr));
  ESP_ERROR_CHECK(rmt_enable(s_rmt_chan));
  const rmt_copy_encoder_config_t copy_cfg = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_cfg, &s_copy_encoder));

  pcnt_unit_config_t unit_cfg = {};
  unit_cfg.high_limit = kPcntLimit;
  unit_cfg.low_limit = -kPcntLimit;
  // Accumulate on overflow instead of silently wrapping. Requires watch points
  // on both limits (added below) - the driver's watch-point ISR is what folds
  // the wrapped span into the accumulator.
  unit_cfg.flags.accum_count = 1;
  ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt_unit));
  ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt_unit, kPcntLimit));
  ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_pcnt_unit, -kPcntLimit));

  pcnt_chan_config_t chan_cfg = {};
  chan_cfg.edge_gpio_num = step_gpio;
  chan_cfg.level_gpio_num = dir_gpio;
  pcnt_channel_handle_t chan = nullptr;
  ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_cfg, &chan));
  // Count on the rising edge of STEP, up while DIR is at kDirLevelUp.
  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                               PCNT_CHANNEL_EDGE_ACTION_HOLD));
  static_assert(kDirLevelUp == 0, "the level actions below assume DIR low counts up");
  ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan, PCNT_CHANNEL_LEVEL_ACTION_INVERSE,
                                                PCNT_CHANNEL_LEVEL_ACTION_KEEP));
  ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));
  ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));

  s_moveRequest = xSemaphoreCreateBinary();
  s_chunkDone = xSemaphoreCreateCounting(2, 0);
  xTaskCreate(move_task, "stepper_move", 4096, nullptr, 10, &s_moveTask);

  ESP_LOGW(TAG, "native RMT/PCNT stepper - UNVERIFIED on hardware, see stepper.h");
}

void setEnabled(bool enabled) {
  if (s_enable_gpio == GPIO_NUM_NC) return;
  gpio_set_level(s_enable_gpio, enabled ? kEnableAsserted : kEnableDeasserted);
  s_enabled.store(enabled);
  ESP_LOGD(TAG, "driver %s", enabled ? "enabled" : "disabled");
}

bool isEnabled() {
  return s_enabled.load();
}

void setSpeedInHz(uint32_t hz) {
  s_speedHz.store(hz);
}
void setAcceleration(uint32_t steps_per_s2) {
  s_accel.store(steps_per_s2);
}

// Start a move to an absolute position, or retarget the one in flight: the
// running ramp picks the new target up at its next chunk, and brakes to rest
// first if it lies behind the direction of travel - FastAccelStepper's
// behaviour for a moveTo() during a move, which requestGracefulStop() relies
// on.
//
// Deliberately does NOT clear s_stopRequested - move_task does that when it
// actually begins the new move (review finding #7).
void moveTo(int32_t absolute_position) {
  s_targetPosition.store(absolute_position);
  s_running.store(true);
  xSemaphoreGive(s_moveRequest);
}

void move(int32_t relative_steps) {
  moveTo(getCurrentPosition() + relative_steps);
}

bool isRunning() {
  return s_running.load();
}

int32_t getCurrentPosition() {
  return pcnt_position();
}

void setCurrentPosition(int32_t position) {
  pcnt_unit_clear_count(s_pcnt_unit);
  s_zeroOffset.store(position);
}

void forceStop() {
  // No rmt_disable() from here: move_task may be inside an RMT call on the
  // same channel. It stops queueing instead, so the steps already in flight
  // (at most two chunks, ~2 x kChunkMaxUs) still go out - bounded,
  // single-task, and PCNT counts them either way.
  s_stopRequested.store(true);
  // Cancel a retarget requested before this stop, as FastAccelStepper's
  // forceStop() does. A moveTo() issued after it gives the request again and
  // starts a fresh move once this one has halted.
  xSemaphoreTake(s_moveRequest, 0);
}

}  // namespace stepper
