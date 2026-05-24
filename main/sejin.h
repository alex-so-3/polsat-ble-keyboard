#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ir.h"

typedef struct {
    bool    is_sejin1;        // true = Sejin-1-38, false = Sejin-2-38
    uint8_t device;           // Sejin-1: Dx[6:0], Sejin-2: 64 - Dx[7:2]
    uint8_t seed;             // E nibble

    // Sejin-1 fields.
    uint8_t subdevice;        // Fx[6:0]
    uint8_t function;         // Fy
    bool    toggle;           // Fx[7]: 0=held, 1=last frame

    // Sejin-2 fields.
    uint8_t obc;              // Dx[1:0], button number for button-down
    int8_t  x;                // Fx as signed 8-bit
    int8_t  y;                // Fy as signed 8-bit
    bool    is_button_event;  // true when x==0 && y==0
    bool    button_down;      // valid when is_button_event
    bool    rmobc_supported;  // false when both x and y are non-zero
    uint16_t rmobc;           // RemoteMaster helper code
} sejin_frame_t;

// Attempts Sejin-1-38 and Sejin-2-38 parsing in one pass.
// Returns true on success and fills *out, else returns false.
bool decode_sejin_38(const ir_frame_t *frame, sejin_frame_t *out);
