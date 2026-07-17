// ============================================================================
//  autolee_logic/endpoint_math.h
//  Pure endpoint / offset math — no hardware, host-testable.
//  Mirrors clamp_i32() and recomputeEffectiveEndpoints() in AutoLee/motion.h.
// ============================================================================
#pragma once
#include <cstdint>

namespace autolee {

inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

struct Endpoints {
  int32_t upOffset;     // possibly clamped / adjusted
  int32_t downOffset;   // possibly clamped / adjusted (to honor the guard)
  long    endpointUp;   // effective UP position
  long    endpointDown; // effective DOWN position
};

// Faithful port of recomputeEffectiveEndpoints():
//  - if not calibrated, endpoints are 0 (offsets returned unchanged)
//  - offsets are clamped to [offMin, offMax]
//  - DOWN is forced at least `guard` steps below UP; if so, downOffset is
//    back-computed so it stays consistent with rawDown.
inline Endpoints computeEffectiveEndpoints(bool calibrated,
                                           long rawUp, long rawDown,
                                           int32_t upOffset, int32_t downOffset,
                                           int32_t offMin, int32_t offMax,
                                           int32_t guard) {
  if (!calibrated) return { upOffset, downOffset, 0, 0 };

  upOffset   = clamp_i32(upOffset, offMin, offMax);
  downOffset = clamp_i32(downOffset, offMin, offMax);

  long upEff = rawUp + upOffset;
  long dnEff = rawDown + downOffset;
  if (dnEff <= (upEff + guard)) {
    dnEff = upEff + guard;
    downOffset = (int32_t)(dnEff - rawDown);
  }
  return { upOffset, downOffset, upEff, dnEff };
}

}  // namespace autolee
