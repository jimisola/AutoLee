// ============================================================================
//  autolee_logic/stall_monitor.h
//  Sliding-counter runtime stall detector — pure FSM, host-testable.
//  Mirrors the high/low counter logic in handleMotion() (src/motion.cpp):
//   - each reading above the trip bumps highCount (capped at needed+margin)
//   - a jam fires once highCount reaches `needed`
//   - readings at/below trip bump lowCount; every `decay` lows drop highCount by 1
//
// The margin/decay defaults match the original firmware. The firmware passes
// its own named constants (RUN_SG_HIGH_SATURATION_MARGIN / RUN_SG_LOW_DECAY_COUNT
// from main/config.h); they're parameters rather than includes so this header
// stays free of any ESP-IDF/Arduino dependency and testable on the host.
// ============================================================================
#pragma once
#include <cstdint>

namespace autolee {

class StallCounter {
 public:
  explicit StallCounter(uint8_t needed, uint8_t saturationMargin = 4, uint8_t lowDecayCount = 3)
      : needed_(needed), margin_(saturationMargin), decay_(lowDecayCount) {}

  void reset() {
    highCount_ = 0;
    lowCount_ = 0;
  }

  // Feed one filtered SG reading. Returns true when a jam is detected.
  bool update(uint16_t sg, uint16_t trip) {
    if (sg > trip) {
      if (highCount_ < needed_ + margin_) highCount_++;
      lowCount_ = 0;
      return highCount_ >= needed_;
    }
    lowCount_++;
    if (lowCount_ >= decay_) {
      lowCount_ = 0;
      if (highCount_ > 0) highCount_--;
    }
    return false;
  }

  uint8_t highCount() const { return highCount_; }
  uint8_t lowCount() const { return lowCount_; }
  uint8_t needed() const { return needed_; }

 private:
  uint8_t needed_;
  uint8_t margin_;
  uint8_t decay_;
  uint8_t highCount_ = 0;
  uint8_t lowCount_ = 0;
};

}  // namespace autolee
