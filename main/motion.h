#pragma once

#include <cstdint>

// Motion state machine + StallGuard logic, ported from src/motion.cpp.
// *** UNVERIFIED ON HARDWARE - see main/stepper.h. This is the safety-critical
// jam-detection/stop/backoff code; bench-verify before trusting on the press. ***
void motion_init();

void handleMotion(); // pump, call every loop iteration
void startRunBetweenEndpoints();
void requestGracefulStop();
bool calibrateEndpointsSensorless();
void safeCreepHome();
bool return_home_up_safe();
void recomputeEffectiveEndpoints();
uint16_t read_sg();
