// Auto SG: measure each speed profile's clean StallGuard maximum with jam
// detection off, and set its trip just above it. Pure bookkeeping, fed one
// finished stroke at a time; motion.cpp drives the strokes.
#pragma once

#include <cstdint>

#include "stroke_stats.h"

namespace autolee {

// Trip = measured max + margin. The margin is one count on purpose: the gap
// between the highest clean reading and a jam is only a few counts and narrows
// with speed, so a larger margin can put the trip above the jam level itself.
// A trip at or below floorTripMax means the profile read only SG floor (0/1),
// and detection then relies on a stall spiking off it.
struct AutoSgConfig {
  uint16_t strokes;  // measured strokes per profile (after the discarded first)
  uint16_t margin;
  uint16_t tripMin;
  uint16_t tripMax;
};

inline uint16_t floorTripMax(const AutoSgConfig &c) {
  return (uint16_t)(1 + c.margin);
}

// A trip that relies on spikes off the SG floor: set, but limited.
inline bool tripAtFloor(uint16_t trip, uint16_t floorMax) {
  return trip > 0 && trip <= floorMax;
}

// Jam detection is set up when every profile has a trip; 0 means "not set".
template <typename TripAt>
inline bool allTripsSet(uint8_t n, TripAt tripAt) {
  for (uint8_t i = 0; i < n; i++)
    if (tripAt(i) == 0) return false;
  return true;
}

class AutoSg {
 public:
  static constexpr uint8_t kMaxProfiles = 3;
  enum class Step : uint8_t {
    Continue,     // keep stroking this profile
    NextProfile,  // this profile is measured; switch to profile()
    Done,         // every profile measured; tripFor() is valid
    Failed,       // a profile yielded no samples at all
  };

  explicit AutoSg(const AutoSgConfig &cfg) : cfg_(cfg) {}

  void begin(uint8_t profiles) {
    profiles_ = profiles > kMaxProfiles ? kMaxProfiles : profiles;
    profile_ = 0;
    active_ = true;
    resetProfile();
    for (uint8_t i = 0; i < kMaxProfiles; i++) {
      measured_[i] = 0;
      floorOnly_[i] = false;
    }
  }

  void abort() { active_ = false; }

  // One finished stroke. The first stroke of each profile starts wherever the
  // ram happened to be and is usually partial, so it is discarded.
  Step strokeDone(const StrokeStats &s) {
    if (!active_) return Step::Failed;
    seen_++;
    if (seen_ > 1) {
      if (s.max() > max_) max_ = s.max();
      floor_ += s.floor();
    }
    if (seen_ < cfg_.strokes + 1u) return Step::Continue;

    if (max_ == 0 && floor_ == 0) {
      active_ = false;
      return Step::Failed;
    }
    // Only floor readings is a valid clean measurement: max = 1.
    floorOnly_[profile_] = (max_ == 0);
    measured_[profile_] = floorOnly_[profile_] ? 1 : max_;

    profile_++;
    resetProfile();
    if (profile_ >= profiles_) {
      active_ = false;
      return Step::Done;
    }
    return Step::NextProfile;
  }

  bool active() const { return active_; }
  uint8_t profile() const { return profile_; }
  // Measured strokes so far for the current profile (the discarded one excluded).
  uint16_t stroke() const { return seen_ > 0 ? (uint16_t)(seen_ - 1) : 0; }
  uint16_t measured(uint8_t i) const { return measured_[i]; }
  bool floorOnly(uint8_t i) const { return floorOnly_[i]; }

  uint16_t tripFor(uint8_t i) const {
    uint32_t t = (uint32_t)measured_[i] + cfg_.margin;
    if (t < cfg_.tripMin) t = cfg_.tripMin;
    if (t > cfg_.tripMax) t = cfg_.tripMax;
    return (uint16_t)t;
  }

 private:
  void resetProfile() {
    seen_ = 0;
    max_ = 0;
    floor_ = 0;
  }

  AutoSgConfig cfg_;
  uint8_t profiles_ = 0;
  uint8_t profile_ = 0;
  bool active_ = false;
  uint16_t seen_ = 0;
  uint16_t max_ = 0;
  uint32_t floor_ = 0;
  uint16_t measured_[kMaxProfiles] = {0, 0, 0};
  bool floorOnly_[kMaxProfiles] = {false, false, false};
};

}  // namespace autolee
