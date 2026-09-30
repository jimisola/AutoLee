#include "tmc5160_ctrl.h"
#include "tmc5160_hal.h"

#include "driver/gpio.h"

extern "C" {
#include "TMC5160.h"
}

#include "config.h"  // TMC_DIAG_PIN
#include "tmc_current.h"

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
  // TOFF=5, TBL=2 (36 tCLK comparator blank time) as upstream v1.28, which
  // reverted v1.10.0's TOFF=4/TBL=1. INTPOL: MicroPlyer interpolates 16
  // microsteps to 256 internally - smoother motion at no torque cost.
  //
  // *** Chopper timing moves SG_RESULT, so a change here invalidates measured
  // StallGuard trips. UNVERIFIED on this port's hardware; confirm on the bench
  // rig before trusting it on the press (docs/PLAN.md Phase 4). ***
  //
  // Note: upstream had to write TBL as a raw register field because
  // TMCStepper's blank_time() silently no-ops on the value 1 (it expects
  // 16/24/36/54 clock counts). We use TMC-API's field writes throughout, so we
  // are not exposed to that particular trap - but the value below is the raw
  // TBL field encoding, not a clock count.
  // GCONF as TMCStepper's begin() leaves it (0): multistep_filt off. StealthChop
  // only, so inert here, but it keeps the register the one the press ran on.
  tmc5160_fieldWrite(kIcID, TMC5160_MULTISTEP_FILT_FIELD, 0);
  // CHOPCONF bit 17 is vsense on the TMC2130/5130 and reserved on the TMC5160;
  // earlier builds of this port set it, and it survives until a power cycle.
  tmc5160_fieldWrite(kIcID, TMC5160_VSENSE_FIELD, 0);
  tmc5160_fieldWrite(kIcID, TMC5160_TOFF_FIELD, 5);
  tmc5160_fieldWrite(kIcID, TMC5160_TBL_FIELD, 2);
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

// TMCStepper's TMC5160 algorithm (see tmc_current.h): GLOBAL_SCALER plus CS,
// hold at half the run current. 3500 mA -> GLOBAL_SCALER 130, IRUN 20, IHOLD 10.
void rms_current(uint16_t mA) {
  const autolee::TmcCurrent c = autolee::tmc5160Current(mA, s_rSenseOhm);
  tmc5160_fieldWrite(kIcID, TMC5160_GLOBAL_SCALER_FIELD, c.globalScaler);
  tmc5160_fieldWrite(kIcID, TMC5160_IRUN_FIELD, c.irun);
  tmc5160_fieldWrite(kIcID, TMC5160_IHOLD_FIELD, c.ihold);
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
