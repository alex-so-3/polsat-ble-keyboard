#pragma once
#include <stdint.h>

// A captured IR frame: a list of signed pulse durations in microseconds.
// Positive = mark (IR carrier present, receiver output LOW for an active-low
// receiver such as a TSOP). Negative = space (no carrier, output HIGH).
// This matches the format produced by the RP2040 polsat-ir-receiver so the
// Sejin decoder can be shared verbatim.

#define IR_MAX_PULSES 200

typedef struct {
    int32_t durations[IR_MAX_PULSES];
    int count;
} ir_frame_t;
