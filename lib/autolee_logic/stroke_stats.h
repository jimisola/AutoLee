// Per-stroke StallGuard statistics: what a run's SG readings actually did,
// which is the number a trip is tuned against (a jam is SG rising above it).
#pragma once

#include <cstdint>

namespace autolee {

// SG_RESULT 0/1 is the bottom of the range. At speed on a low supply voltage
// it is the genuine clean-running value, not noise, so it is counted rather
// than dropped - but kept out of min/max, which would otherwise sit at 1.
constexpr uint16_t kSgFloor = 1;

class StrokeStats {
 public:
  void reset() {
    min_ = 0xFFFF;
    max_ = 0;
    floor_ = 0;
  }

  // Returns true when this reading is a new maximum for the stroke.
  bool add(uint16_t sg) {
    if (sg <= kSgFloor) {
      if (floor_ < 0xFFFF) floor_++;
      return false;
    }
    if (sg < min_) min_ = sg;
    if (sg > max_) {
      max_ = sg;
      return true;
    }
    return false;
  }

  // Nothing above the floor was read: min/max are meaningless.
  bool onlyFloor() const { return max_ == 0; }
  bool empty() const { return max_ == 0 && floor_ == 0; }
  uint16_t min() const { return min_; }
  uint16_t max() const { return max_; }
  uint16_t floor() const { return floor_; }

 private:
  uint16_t min_ = 0xFFFF;
  uint16_t max_ = 0;
  uint16_t floor_ = 0;
};

}  // namespace autolee
