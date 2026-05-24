#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "ir.h"
#include "ir_rmt.h"
#include "sejin.h"
#include "hid_keymap.h"
#include "ble_kbd.h"
#include "status_led.h"
#include "boot_btn.h"

// IR receiver output pin.
#define IR_GPIO 4

// Status LED. On an ESP32-C3 SuperMini this is the on-board LED on GPIO8, which
// is active-low (driving the pin LOW lights it).
#define LED_GPIO        8
#define LED_ACTIVE_LOW  true

// BOOT button (active-low). Hold it ~1.5 s to enter BLE pairing mode.
#define BOOT_BTN_GPIO   9

// Set to 1 to also dump the raw captured pulse list for every frame. Useful for
// first bring-up / tuning the timing thresholds in ir_rmt.c.
#define IR_DUMP_RAW 0

// Set to 1 to log the raw trackball data stream on one timeline: every decoded
// movement frame (dt / x / y / seed) AND every decode failure (a frame the
// keyboard sent but we could not decode). This tells us whether the gaps in the
// data are real (keyboard silent) or dropped/corrupted frames.
// NOTE: logs over UART per frame, which itself makes the pointer choppy while
// enabled -- judge the *data*, not the feel. Set back to 0 for normal use.
#define MOUSE_DEBUG 0

// Set to 1 to log decoded keypresses (DOWN/UP + key name) at INFO level. Key
// events are low-rate, so this does not disturb timing like mouse logging does.
#define KEY_DEBUG 1

#if KEY_DEBUG
#define KEY_LOG(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define KEY_LOG(...) ESP_LOGD(TAG, __VA_ARGS__)
#endif

static const char *TAG = "polsat";

#if IR_DUMP_RAW
static void dump_raw(const ir_frame_t *f) {
    char line[IR_MAX_PULSES * 7 + 1];
    int n = 0;
    for (int i = 0; i < f->count && n < (int)sizeof(line) - 8; i++) {
        n += snprintf(line + n, sizeof(line) - n, "%ld ", (long)f->durations[i]);
    }
    ESP_LOGI(TAG, "RAW count=%d: %s", f->count, line);
}
#endif

static void handle_sejin1(const sejin_frame_t *s) {
    // 0xFF is the keyboard's "all keys released" sentinel.
    if (s->function == 0xFF) {
        KEY_LOG("ALL KEYS UP");
        ble_kbd_handle_key(0xFF, s->toggle);
        return;
    }

    const key_map_t *m = keymap_lookup(s->function);
    // toggle == 0 means held/pressed, toggle == 1 is the final (release) frame.
    const char *state = s->toggle ? "UP  " : "DOWN";

    if (!m) {
        ESP_LOGW(TAG, "%s function=0x%02X (unmapped)  dev=0x%02X sub=0x%02X",
                 state, s->function, s->device, s->subdevice);
        return;
    }

    if (m->modifier) {
        KEY_LOG("%s  fn=0x%02X  %-12s [modifier 0x%02X]", state, s->function, m->name, m->modifier);
    } else {
        KEY_LOG("%s  fn=0x%02X  %-12s [keycode  0x%02X]", state, s->function, m->name, m->keycode);
    }

    // Forward to the BLE keyboard.
    ble_kbd_handle_key(s->function, s->toggle);
}

static void handle_sejin2(const sejin_frame_t *s) {
    if (s->is_button_event) {
        if (s->button_down) {
            ESP_LOGD(TAG, "MOUSE button %u DOWN", s->obc);
        } else {
            ESP_LOGD(TAG, "MOUSE button UP");
        }
    } else {
        ESP_LOGD(TAG, "MOUSE move dx=%d dy=%d", s->x, s->y);
    }

    // Forward to the BLE mouse.
    ble_kbd_handle_mouse(s);
}

#if MOUSE_DEBUG
// Capture frame events into RAM with zero I/O during a stroke (so UART logging
// can't perturb the timing), then dump them during the idle pause afterwards.
#define DBG_CAP 700
typedef struct {
    uint32_t t_us;   // esp_timer_get_time() truncated to 32 bits
    int16_t  x, y;
    uint8_t  seed;
    uint8_t  flags;  // bit0 ok, bit1 sejin1(key), bit2 button-event, bit3 button-down
} dbg_rec_t;
static dbg_rec_t s_dbg[DBG_CAP];
static int s_dbg_n;

static void dbg_record(bool ok, const sejin_frame_t *s, int count) {
    if (s_dbg_n >= DBG_CAP) return;
    dbg_rec_t *r = &s_dbg[s_dbg_n++];
    r->t_us = (uint32_t)esp_timer_get_time();
    r->x = 0; r->y = 0; r->seed = 0; r->flags = 0;
    if (!ok) {
        r->x = (int16_t)count;        // failures: stash the pulse count in x
        return;
    }
    r->flags = 1;
    r->seed = s->seed;
    if (s->is_sejin1) {
        r->flags |= 2;
    } else if (s->is_button_event) {
        r->flags |= 4 | (s->button_down ? 8 : 0);
    } else {
        r->x = s->x; r->y = s->y;
    }
}

static void dbg_dump(void) {
    if (s_dbg_n == 0) return;
    ESP_LOGI("mdbg", "---- captured %d frames ----", s_dbg_n);
    uint32_t prev = s_dbg[0].t_us;
    for (int i = 0; i < s_dbg_n; i++) {
        dbg_rec_t *r = &s_dbg[i];
        int dt = (int)((int32_t)(r->t_us - prev)) / 1000;
        prev = r->t_us;
        if (!(r->flags & 1)) {
            ESP_LOGI("mdbg", "dt=%4d  DECODE-FAIL pulses=%d", dt, r->x);
        } else if (r->flags & 2) {
            ESP_LOGI("mdbg", "dt=%4d  KEY  seed=%2u", dt, r->seed);
        } else if (r->flags & 4) {
            ESP_LOGI("mdbg", "dt=%4d  BTN  down=%d seed=%2u", dt, (r->flags & 8) ? 1 : 0, r->seed);
        } else {
            ESP_LOGI("mdbg", "dt=%4d  x=%4d y=%4d seed=%2u", dt, r->x, r->y, r->seed);
        }
    }
    s_dbg_n = 0;
}
#endif

// Set from the BOOT-button callback (esp_timer task); serviced in the main loop
// so the actual BLE work runs outside the timer context.
static volatile bool s_pairing_req;

// BOOT held ~1.5 s: blink right away for feedback, defer the BLE reset.
static void on_boot_long_press(void) {
    status_led_set_pairing(true);
    s_pairing_req = true;
}

void app_main(void) {
    ESP_LOGI(TAG, "Polsat / Sejin SWK-8695WS IR receiver -> BLE keyboard");
    status_led_init(LED_GPIO, LED_ACTIVE_LOW);   // 1 s boot indication
    boot_btn_init(BOOT_BTN_GPIO, on_boot_long_press);
    ble_kbd_init();
    ir_rmt_init(IR_GPIO);

    ir_frame_t  frame;
    sejin_frame_t s;

    while (1) {
        if (s_pairing_req) {
            s_pairing_req = false;
            ble_kbd_enter_pairing();
        }
        if (!ir_rmt_get_frame(&frame, 200)) {
#if MOUSE_DEBUG
            dbg_dump();   // idle: flush the captured stroke to the console
#endif
            continue;     // idle timeout, just wait again
        }
#if IR_DUMP_RAW
        dump_raw(&frame);
#endif
        bool ok = decode_sejin_38(&frame, &s);

#if MOUSE_DEBUG
        dbg_record(ok, &s, frame.count);
#endif

        if (!ok) {
            continue;  // noise or partial frame, ignore
        }

        status_led_activity();  // valid IR frame decoded -> brief LED flash

        if (s.is_sejin1) {
            handle_sejin1(&s);
        } else {
            handle_sejin2(&s);
        }
    }
}
