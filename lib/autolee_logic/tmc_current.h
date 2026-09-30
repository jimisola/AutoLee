// TMC5160 run/hold current from a requested RMS current: the algorithm of
// TMCStepper's TMC2160Stepper::rms_current(), which is what TMC5160Stepper
// runs in the Arduino firmware this press was tuned on. Integer arithmetic is
// kept exactly as there, so the register values match bit for bit.
#pragma once

#include <cstdint>

namespace autolee {

struct TmcCurrent {
  uint8_t globalScaler;  // GLOBAL_SCALER; 0 means 256 (full scale)
  uint8_t irun;          // IHOLD_IRUN.IRUN (CS)
  uint8_t ihold;         // IHOLD_IRUN.IHOLD
};

// The TMC5160 drives external MOSFETs: full-scale sense voltage 0.325 V, no
// vsense range and no internal-resistance term - unlike the TMC2130/5130
// formula, which overstates the needed CS by ~1.9x at R_SENSE = 0.022 ohm.
inline TmcCurrent tmc5160Current(uint16_t mA, float rsenseOhm, float holdMultiplier = 0.5f) {
  constexpr uint32_t kVfs = 325;  // 0.325 V * 1000
  uint8_t cs = 31;
  uint32_t scaler = 0;  // = 256

  const uint16_t rsScaled = (uint16_t)(rsenseOhm * 0xFFFF);  // scale to 16 bits
  uint32_t numerator = 11585;                                // 32 * 256 * sqrt(2)
  numerator *= rsScaled;
  numerator >>= 8;
  numerator *= mA;

  do {
    uint32_t denominator = kVfs * 0xFFFF >> 8;
    denominator *= cs + 1;
    scaler = numerator / denominator;

    if (scaler > 255)
      scaler = 0;  // maximum
    else if (scaler < 128)
      cs--;  // try again with a smaller CS
  } while (0 < scaler && scaler < 128);

  if (cs > 31) cs = 31;
  return {(uint8_t)scaler, cs, (uint8_t)(cs * holdMultiplier)};
}

// RMS phase current, in mA, that a GLOBAL_SCALER/CS pair produces.
inline float tmc5160RmsMa(uint8_t globalScaler, uint8_t cs, float rsenseOhm) {
  const float scale = (globalScaler == 0 ? 256.0f : (float)globalScaler) / 256.0f;
  return scale * (cs + 1) / 32.0f * 0.325f / rsenseOhm / 1.41421356f * 1000.0f;
}

}  // namespace autolee
