#include "tmc5160_ctrl.h"
#include "tmc5160_hal.h"

#include <cmath>
#include "driver/gpio.h"

extern "C" {
#include "TMC5160.h"
}

#include "config.h"  // TMC_DIAG_PIN

namespace tmc5160 {

static constexpr uint16_t kIcID = 0;
static float s_rSenseOhm = 0.022f;

void init(spi_host_device_t spi_host, float r_sense_ohm) {
  s_rSenseOhm = r_sense_ohm;
  tmc5160_hal_init(spi_host);

  gpio_config_t diag_cfg = {};
  diag_cfg.pin_bit_mask = 1ULL << TMC_DIAG_PIN;
  diag_cfg.mode = GPIO_MODE_INPUT;
  gpio_config(&diag_cfg);  // configured but not read - see CLAUDE.md; SG polled over SPI

  // Mirrors the original Arduino firmware's TMCStepper setup() sequence.
  // TOFF=4 (was 5): shorter slow-decay phase per chopper cycle, tuned upstream
  // for low-speed high-load torque. TBL=1 -> 24 tCLK comparator blank time.
  // INTPOL: MicroPlyer interpolates 16 microsteps to 256 internally - smoother
  // motion at no torque cost.
  //
  // *** All three come from Karl's upstream v1.10.0 and are UNVERIFIED on this
  // port's hardware. They change torque/smoothness characteristics; confirm on
  // the bench rig before trusting them on the press (docs/PLAN.md Phase 4). ***
  //
  // Note: upstream had to write TBL as a raw register field because
  // TMCStepper's blank_time() silently no-ops on the value 1 (it expects
  // 16/24/36/54 clock counts). We use TMC-API's field writes throughout, so we
  // are not exposed to that particular trap - but the value below is the raw
  // TBL field encoding, not a clock count.
  tmc5160_fieldWrite(kIcID, TMC5160_TOFF_FIELD, 4);
  tmc5160_fieldWrite(kIcID, TMC5160_TBL_FIELD, 1);
  tmc5160_fieldWrite(kIcID, TMC5160_INTPOL_FIELD, 1);
  tmc5160_fieldWrite(kIcID, TMC5160_MRES_FIELD, 4);         // 16 microsteps: log2(256/16)=4
  tmc5160_fieldWrite(kIcID, TMC5160_EN_PWM_MODE_FIELD, 0);  // SpreadCycle, not StealthChop
  tmc5160_fieldWrite(kIcID, TMC5160_TPWMTHRS_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_TCOOLTHRS_FIELD, 0xFFFFF);
  tmc5160_fieldWrite(kIcID, TMC5160_SEMIN_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_SEMAX_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_SEUP_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_SEDN_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_DIAG1_STALL_FIELD, 1);
  tmc5160_fieldWrite(kIcID, TMC5160_DIAG1_INDEX_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_DIAG1_ONSTATE_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_DIAG1_STEPS_SKIPPED_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_DIAG1_POSCOMP_PUSHPULL_FIELD, 1);  // "diag1_pushpull"
}

// Same formula TMCStepper uses for TMC5130/5160-family chips: CS =
// 32*sqrt2*I/1000*(Rsense+0.02)/Vfs - 1, picking the low-sensitivity full-scale voltage (0.180V,
// vsense=1) when the high-sensitivity range (0.325V) would clip the 5-bit CS field below its usable
// floor.
void rms_current(uint16_t mA) {
  constexpr float kSqrt2 = 1.41421356f;
  float cs = 32.0f * kSqrt2 * (mA / 1000.0f) * (s_rSenseOhm + 0.02f) / 0.325f - 1.0f;
  bool vsense = false;
  if (cs < 16.0f) {
    vsense = true;
    cs = 32.0f * kSqrt2 * (mA / 1000.0f) * (s_rSenseOhm + 0.02f) / 0.180f - 1.0f;
  }
  if (cs > 31.0f) cs = 31.0f;
  if (cs < 0.0f) cs = 0.0f;

  tmc5160_fieldWrite(kIcID, TMC5160_VSENSE_FIELD, vsense ? 1 : 0);
  tmc5160_fieldWrite(kIcID, TMC5160_IRUN_FIELD, (uint32_t)lroundf(cs));
  tmc5160_fieldWrite(kIcID, TMC5160_IHOLD_FIELD,
                     (uint32_t)lroundf(cs));  // hold = run (no idle current cut)
}

void en_pwm_mode(bool enabled) {
  tmc5160_fieldWrite(kIcID, TMC5160_EN_PWM_MODE_FIELD, enabled ? 1 : 0);
}
void TPWMTHRS(uint32_t threshold) {
  tmc5160_fieldWrite(kIcID, TMC5160_TPWMTHRS_FIELD, threshold);
}
void TCOOLTHRS(uint32_t threshold) {
  tmc5160_fieldWrite(kIcID, TMC5160_TCOOLTHRS_FIELD, threshold);
}
void semin(uint8_t value) {
  tmc5160_fieldWrite(kIcID, TMC5160_SEMIN_FIELD, value);
}
void semax(uint8_t value) {
  tmc5160_fieldWrite(kIcID, TMC5160_SEMAX_FIELD, value);
}
void seup(uint8_t value) {
  tmc5160_fieldWrite(kIcID, TMC5160_SEUP_FIELD, value);
}
void sedn(uint8_t value) {
  tmc5160_fieldWrite(kIcID, TMC5160_SEDN_FIELD, value);
}
void sgt(int8_t value) {
  tmc5160_fieldWrite(kIcID, TMC5160_SGT_FIELD, (uint32_t)(int32_t)value);
}

uint32_t DRV_STATUS() {
  return (uint32_t)tmc5160_readRegister(kIcID, TMC5160_DRV_STATUS);
}
uint16_t SG_RESULT() {
  return (uint16_t)tmc5160_fieldRead(kIcID, TMC5160_SG_RESULT_FIELD);
}

}  // namespace tmc5160
