#include "sejin.h"
#include "esp_log.h"

// Ported verbatim from polsat-ir-receiver/sejin.c. The only change is routing
// the diagnostic message through ESP-IDF logging at DEBUG level instead of the
// RP2040 serial log.

static const char *TAG = "sejin";

#define UNIT_US  310
#define NEAR(v, target, tol) ((v) >= (target) - (tol) && (v) <= (target) + (tol))

static int abs_us(int v) { return v < 0 ? -v : v; }

static bool parse_sejin_common(const ir_frame_t *frame,
                               uint8_t *dx,
                               uint8_t *fx,
                               uint8_t *fy,
                               uint8_t *e,
                               uint8_t *c) {
    // Need header pair + enough transitions to form 17 dibits.
    if (frame->count < 29) {
        return false;
    }

    // Header: mark ~= 930us, space ~= 930us.
    if (!NEAR(frame->durations[0], 930, 300)) {
        return false;
    }
    if (!NEAR(frame->durations[1], -930, 300)) {
        return false;
    }

    uint8_t d[17] = {0};
    int nBit = 0;
    int bcnt = 0;


    for (int i = 1; i + 1 < frame->count && nBit < 34; i += 2) {
        int space = abs_us(frame->durations[i]);
        int next_mark = abs_us(frame->durations[i + 1]);

        bcnt += (int)((float)(space + next_mark) / (float)UNIT_US + 0.5f);


        while (bcnt > 2 * nBit && nBit < 34) {
            int dbit = bcnt - 2 * nBit - 1;
            if (dbit > 3) dbit = 3;
            d[nBit / 2] = (uint8_t)dbit;
            nBit += 2;
        }
    }

    if (nBit != 34) {
        return false;
    }

    if (d[0] != 3) {
        return false;
    }

    *dx = (uint8_t)((d[1] << 6) | (d[2] << 4) | (d[3] << 2) | d[4]);
    *fx = (uint8_t)((d[5] << 6) | (d[6] << 4) | (d[7] << 2) | d[8]);
    *fy = (uint8_t)((d[9] << 6) | (d[10] << 4) | (d[11] << 2) | d[12]);
    *e  = (uint8_t)((d[13] << 2) | d[14]);
    *c  = (uint8_t)((d[15] << 2) | d[16]);


    uint8_t chk = (uint8_t)(((*dx >> 4) + (*dx & 0xF) +
                             (*fx >> 4) + (*fx & 0xF) +
                             (*fy >> 4) + (*fy & 0xF) + *e) & 0xF);
    if (*c != chk) {
        return false;
    }

    return true;
}

bool decode_sejin_38(const ir_frame_t *frame, sejin_frame_t *out) {
    uint8_t dx, fx, fy, e, c;
    if (!parse_sejin_common(frame, &dx, &fx, &fy, &e, &c)) {
        ESP_LOGD(TAG, "not a valid Sejin-1-38 or Sejin-2-38 frame");
        return false;
    }

    out->seed = e;

    if ((dx & 0x80) == 0) {
        // Sejin-1.
        out->is_sejin1 = true;
        out->device = dx & 0x7F;
        out->subdevice = fx & 0x7F;
        out->function = fy;
        out->toggle = (fx >> 7) & 1;

        out->obc = 0;
        out->x = 0;
        out->y = 0;
        out->is_button_event = false;
        out->button_down = false;
        out->rmobc_supported = false;
        out->rmobc = 0;
        return true;
    }

    // Sejin-2.
    int x = (fx & 0x80) ? ((int)fx - 256) : (int)fx;
    int y = (fy & 0x80) ? ((int)fy - 256) : (int)fy;
    int fn1 = fx ? (int)fx : (int)fy;
    int fn2_raw = dx & 0x03;
    int fn2_rm = fn2_raw;
    if (fx != 0) fn2_rm += 0x08;
    if (fn1 != 0) fn2_rm += 0x10;

    out->is_sejin1 = false;
    out->device = (uint8_t)(64 - ((dx >> 2) & 0x3F));
    out->obc = (uint8_t)fn2_raw;
    out->x = (int8_t)x;
    out->y = (int8_t)y;
    out->is_button_event = (x == 0 && y == 0);
    out->button_down = out->is_button_event && (out->obc != 0);
    out->rmobc_supported = (x == 0 || y == 0);
    out->rmobc = (uint16_t)(fn1 + (fn2_rm << 8));


    out->subdevice = 0;
    out->function = 0;
    out->toggle = false;

    return true;
}
