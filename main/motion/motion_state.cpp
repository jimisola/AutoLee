#include "motion_state.h"

// Single definition site for the cross-task motion state and its spinlock.
// Field defaults (speed profiles, default current, work zone) live in the
// struct declaration in motion_state.h.
MotionState g_motion;

namespace motion_state {

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

}  // namespace motion_state
