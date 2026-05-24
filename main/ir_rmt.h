#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ir.h"

// Initialise RMT RX capture of the IR receiver on the given GPIO and arm the
// first receive. The receiver is assumed to be active-low (idle HIGH), e.g. a
// TSOP-style 36/38 kHz demodulator.
void ir_rmt_init(int gpio_num);

// Block up to timeout_ms for the next captured frame. On success fills *out
// (in the signed-microsecond format expected by decode_sejin_38) and returns
// true. Returns false on timeout.
bool ir_rmt_get_frame(ir_frame_t *out, uint32_t timeout_ms);
