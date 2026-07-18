#include "stepper.h"
#include "stepper_motor_encoder.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include "driver/rmt_tx.h"
#include "driver/pulse_cnt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

namespace stepper {

static const char *TAG = "stepper";

// RMT resolution: 1 tick = 1us, plenty of headroom below the ~kHz step rates
// this press runs at.
static constexpr uint32_t kResolutionHz = 1'000'000;
static constexpr uint32_t kMinFreqHz = 100; // curve encoder floor; avoids div-by-zero at v=0
static constexpr uint32_t kSamplePointsMax = 500;
// Cruise-phase chunk size: forceStop() latency is bounded by this, since the
// cruise loop only checks the stop flag between chunks (see stepper.h's
// warning - this bound is a design choice, not yet bench-verified).
static constexpr uint32_t kCruiseChunkMs = 20;

// *** PCNT range assumption: the press's mechanical travel is assumed to fit
// in +-30000 steps (16 microsteps => ~1875 full motor steps of travel).
// setCurrentPosition() re-zeros at every calibration, bounding drift. If a
// future board needs more travel than this, add PCNT overflow accumulation
// (flags.accum_count + watch points) - not implemented here. ***
static constexpr int kPcntLimit = 30000;

static rmt_channel_handle_t s_rmt_chan = nullptr;
static pcnt_unit_handle_t s_pcnt_unit = nullptr;
static gpio_num_t s_dir_gpio = GPIO_NUM_NC;

static std::atomic<uint32_t> s_speedHz{1000};
static std::atomic<uint32_t> s_accel{4000}; // steps/s^2
static std::atomic<bool> s_running{false};
static std::atomic<bool> s_stopRequested{false};
static std::atomic<int32_t> s_targetPosition{0};
static std::atomic<int32_t> s_zeroOffset{0}; // getCurrentPosition() = pcnt_count + s_zeroOffset

static TaskHandle_t s_moveTask = nullptr;
static SemaphoreHandle_t s_moveRequest = nullptr; // binary sema: a move is pending

static int32_t pcnt_position() {
    int count = 0;
    pcnt_unit_get_count(s_pcnt_unit, &count);
    return count + s_zeroOffset.load();
}

// Transmits `count` pulses at a constant `freq_hz`, in kCruiseChunkMs chunks
// so forceStop() can interrupt it promptly. Blocking (call from the move task).
static void transmit_uniform(rmt_encoder_handle_t uniform_encoder, uint32_t freq_hz,
                              uint32_t count) {
    if (count == 0 || s_stopRequested.load()) return;
    uint32_t chunk_pulses = (freq_hz * kCruiseChunkMs) / 1000;
    if (chunk_pulses < 1) chunk_pulses = 1;

    rmt_transmit_config_t tx_cfg = {};
    uint32_t remaining = count;
    while (remaining > 0 && !s_stopRequested.load()) {
        uint32_t this_chunk = remaining < chunk_pulses ? remaining : chunk_pulses;
        tx_cfg.loop_count = (int)this_chunk - 1; // loop_count=0 means "once"
        rmt_transmit(s_rmt_chan, uniform_encoder, &freq_hz, sizeof(freq_hz), &tx_cfg);
        rmt_tx_wait_all_done(s_rmt_chan, -1);
        remaining -= this_chunk;
    }
}

// Transmits one accel (start_hz -> end_hz) or decel (end_hz -> start_hz)
// curve of `points` pulses. Blocking.
static void transmit_curve(uint32_t start_hz, uint32_t end_hz, uint32_t points) {
    if (points == 0 || start_hz == end_hz || s_stopRequested.load()) return;
    stepper_motor_curve_encoder_config_t cfg = {};
    cfg.resolution = kResolutionHz;
    cfg.sample_points = points;
    cfg.start_freq_hz = start_hz;
    cfg.end_freq_hz = end_hz;
    rmt_encoder_handle_t enc = nullptr;
    if (rmt_new_stepper_motor_curve_encoder(&cfg, &enc) != ESP_OK) return;

    rmt_transmit_config_t tx_cfg = {};
    rmt_transmit(s_rmt_chan, enc, &points, sizeof(points), &tx_cfg);
    rmt_tx_wait_all_done(s_rmt_chan, -1);
    rmt_del_encoder(enc);
}

static void move_task(void *) {
    stepper_motor_uniform_encoder_config_t uniform_cfg = {kResolutionHz};
    rmt_encoder_handle_t uniform_encoder = nullptr;
    rmt_new_stepper_motor_uniform_encoder(&uniform_cfg, &uniform_encoder);

    for (;;) {
        xSemaphoreTake(s_moveRequest, portMAX_DELAY);
        s_stopRequested.store(false);

        int32_t target = s_targetPosition.load();
        int32_t start_pos = pcnt_position();
        int32_t delta = target - start_pos;
        if (delta == 0) {
            s_running.store(false);
            continue;
        }

        gpio_set_level(s_dir_gpio, delta > 0 ? 1 : 0);
        uint32_t total_steps = (uint32_t)labs(delta);

        uint32_t target_hz = s_speedHz.load();
        if (target_hz < kMinFreqHz) target_hz = kMinFreqHz;
        uint32_t accel = s_accel.load();
        if (accel < 1) accel = 1;

        // Linear-ramp approximation (the curve itself is smoothstep-shaped,
        // see stepper_motor_encoder.c) - close enough for the accel values
        // this press uses. sample_points below is both the pulse count of
        // the ramp AND (via kResolutionHz/accel) its rough duration.
        float ramp_time_s = (float)(target_hz - kMinFreqHz) / (float)accel;
        float avg_hz = (target_hz + kMinFreqHz) / 2.0f;
        uint32_t ramp_points = (uint32_t)lroundf(avg_hz * ramp_time_s);
        if (ramp_points < 2) ramp_points = 2;
        if (ramp_points > kSamplePointsMax) ramp_points = kSamplePointsMax;

        uint32_t accel_points = ramp_points, decel_points = ramp_points;
        uint32_t cruise_points = 0;
        if (accel_points + decel_points >= total_steps) {
            // Move too short for a full trapezoid - triangular profile.
            accel_points = decel_points = total_steps / 2;
            if (accel_points < 1) accel_points = 1;
            if (decel_points < 1) decel_points = 1;
        } else {
            cruise_points = total_steps - accel_points - decel_points;
        }

        transmit_curve(kMinFreqHz, target_hz, accel_points);
        transmit_uniform(uniform_encoder, target_hz, cruise_points);
        transmit_curve(target_hz, kMinFreqHz, decel_points);

        s_running.store(false);
    }
}

void init(gpio_num_t step_gpio, gpio_num_t dir_gpio) {
    s_dir_gpio = dir_gpio;
    gpio_config_t dir_cfg = {};
    dir_cfg.pin_bit_mask = 1ULL << dir_gpio;
    dir_cfg.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&dir_cfg));
    gpio_set_level(dir_gpio, 0);

    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = step_gpio;
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = kResolutionHz;
    tx_cfg.mem_block_symbols = 64;
    tx_cfg.trans_queue_depth = 4;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &s_rmt_chan));
    ESP_ERROR_CHECK(rmt_enable(s_rmt_chan));

    pcnt_unit_config_t unit_cfg = {};
    unit_cfg.high_limit = kPcntLimit;
    unit_cfg.low_limit = -kPcntLimit;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt_unit));

    pcnt_chan_config_t chan_cfg = {};
    chan_cfg.edge_gpio_num = step_gpio;
    chan_cfg.level_gpio_num = dir_gpio;
    pcnt_channel_handle_t chan = nullptr;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_cfg, &chan));
    // Count on the rising edge of STEP; DIR high = count up, DIR low = count down.
    ESP_ERROR_CHECK(
        pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                      PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(
        pcnt_channel_set_level_action(chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                       PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));

    s_moveRequest = xSemaphoreCreateBinary();
    xTaskCreate(move_task, "stepper_move", 4096, nullptr, 10, &s_moveTask);

    ESP_LOGW(TAG, "native RMT/PCNT stepper - UNVERIFIED on hardware, see stepper.h");
}

void setSpeedInHz(uint32_t hz) { s_speedHz.store(hz); }
void setAcceleration(uint32_t steps_per_s2) { s_accel.store(steps_per_s2); }

void moveTo(int32_t absolute_position) {
    s_targetPosition.store(absolute_position);
    s_stopRequested.store(false);
    s_running.store(true);
    xSemaphoreGive(s_moveRequest);
}

void move(int32_t relative_steps) { moveTo(getCurrentPosition() + relative_steps); }

bool isRunning() { return s_running.load(); }

int32_t getCurrentPosition() { return pcnt_position(); }

void setCurrentPosition(int32_t position) {
    pcnt_unit_clear_count(s_pcnt_unit);
    s_zeroOffset.store(position);
}

void forceStop() {
    // Deliberately NOT calling rmt_disable()/rmt_enable() here: this can run
    // concurrently with move_task possibly being blocked in
    // rmt_tx_wait_all_done() on the same channel, and there's no confirmed-safe
    // way to abort that from another task without risking a driver-level race.
    // The flag check at each cruise chunk boundary (kCruiseChunkMs) is the
    // real stop mechanism - bounded, single-task, no concurrent driver calls.
    // Jam detection only fires during the cruise phase in the ported
    // algorithm (accel/decel are SG-blanked - see motion.cpp), so this is the
    // path that matters in practice. See stepper.h's hardware-verification note.
    s_stopRequested.store(true);
}

} // namespace stepper
