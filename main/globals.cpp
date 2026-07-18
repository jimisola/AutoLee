#include "config.h"
#include "globals.h"

// config.h's cross-module mutable state (single definition site).
SpeedProfile profiles[NUM_PROFILES] = {
    {"Slow", 15000, 350, 350},
    {"Normal", 35000, 15, 15},
    {"Fast", 45000, 1, 1},
};
uint8_t activeProfile = 1; // default to Normal
int32_t upOffsetSteps = 0;
int32_t downOffsetSteps = 0;
uint16_t RUN_CURRENT_MA = 3500;
int32_t SG_WORK_ZONE_STEPS = 5500;

// globals.h's motion-state (single definition site).
volatile RunState runState = IDLE;
long currentTarget = 0;
uint32_t stopEntryMs = 0;

long rawUp = 0, rawDown = 0;
long endpointUp = 0, endpointDown = 0;
bool endpointsCalibrated = false;
long counter = 0;

uint32_t lastDirectionChangeMs = 0;
uint8_t runSGHighCount = 0;
uint8_t runSGLowCount = 0;

bool batchActive = false;
int32_t batchCount = 0;
int32_t batchTarget = 0;
