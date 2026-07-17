// ============================================================================
//  autolee_logic/stall_monitor.h
//  Sliding-counter runtime stall detector — pure FSM, host-testable.
//  Mirrors the high/low counter logic in handleMotion() (AutoLee/motion.h):
//   - each reading above the trip bumps highCount (capped at needed+4)
//   - a jam fires once highCount reaches `needed`
//   - readings at/below trip bump lowCount; every 3 lows decay highCount by 1
// ============================================================================
#pragma once
#include <cstdint>

namespace autolee {

class StallCounter {
 public:
  explicit StallCounter(uint8_t needed) : needed_(needed) {}

  void reset() { highCount_ = 0; lowCount_ = 0; }

  // Feed one filtered SG reading. Returns true when a jam is detected.
  bool update(uint16_t sg, uint16_t trip) {
    if (sg > trip) {
      if (highCount_ < needed_ + 4) highCount_++;
      lowCount_ = 0;
      return highCount_ >= needed_;
    }
    lowCount_++;
    if (lowCount_ >= 3) {
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
  uint8_t highCount_ = 0;
  uint8_t lowCount_ = 0;
};

}  // namespace autolee
