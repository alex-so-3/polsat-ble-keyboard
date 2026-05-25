#include "ble_kbd.h"
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "esp_hidd.h"
#include "esp_hid_gap.h"
#include "hid_keymap.h"
#include "status_led.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"

static const char *TAG = "ble_kbd";

// HID Report IDs used in the descriptor below and when sending input reports.
#define KBD_REPORT_ID    1
#define MOUSE_REPORT_ID  2
#define CC_REPORT_ID     3
// Boot-style keyboard report: modifier byte + reserved byte + 6 key slots.
#define KBD_REPORT_LEN   8
// Mouse report: buttons + relative X + relative Y + wheel + AC Pan (horizontal).
#define MOUSE_REPORT_LEN 5
// Consumer Control report: one 16-bit usage code (0 = released).
#define CC_REPORT_LEN    2

#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

// Pointer feel (all tunable).
//
// The trackball reports counts-since-last-frame at irregular intervals (the
// encoder is bursty: even at a steady hand speed dt jitters 24..95 ms), so the
// per-frame velocity (delta/dt) is noisy. Reacting per-frame is choppy; spreading
// each frame over the next interval is smooth but laggy. Instead we track a
// SMOOTHED velocity (EMA) and emit it continuously at 60 Hz: low latency (moves
// at the current speed right away) and it fills the gaps between bursts. When
// frames stop arriving the velocity coasts briefly, then decays to a stop, so
// overshoot scales with speed (negligible when slow, a little glide on fast
// flicks). Acceleration uses real speed = hypot(x,y)/dt (counts/ms), not the
// bursty delta magnitude.
#define MOUSE_GAIN      5.0f    // base px per count (small/mid-movement sensitivity)
#define MOUSE_ACCEL     0.35f   // acceleration vs speed (counts/ms); 0 = linear
// Asymmetric velocity smoothing: fast when speeding up (responsive start), slow
// when steady/decelerating (smooth, less overshoot). Raise ATTACK if it still
// feels laggy at the start of a move; lower RELEASE if steady motion is jittery.
#define MOUSE_EMA_ATTACK  0.75f
#define MOUSE_EMA_RELEASE 0.40f
#define MOUSE_COAST_MS  40.0f   // hold velocity this long after the last frame...
#define MOUSE_DECAY     0.70f   // ...then decay per tick toward a stop (lower = stops sooner)
#define MOUSE_TICK_US   16000   // ~60 Hz emit cadence
#define MOUSE_DT_MIN_MS 6.0f    // clamp frame interval used for the math
#define MOUSE_DT_MAX_MS 120.0f

// Scroll mode: holding BOTH mouse buttons turns the trackball into a scroll
// wheel. Movement snaps to one axis (vertical wheel OR horizontal AC Pan) so it
// never scrolls diagonally.
#define SCROLL_AXIS_V    1
#define SCROLL_AXIS_H    2
#define SCROLL_DIV       3.0f    // trackball counts per emitted scroll step (higher = slower)
#define SCROLL_RELOCK_US 200000  // re-pick the locked axis after this idle gap
// The first frame alone is too noisy to choose an axis, so accumulate this many
// counts of movement first; and bias toward vertical (the common direction) by
// only locking horizontal when it dominates by this factor.
#define SCROLL_LOCK_COUNTS 4.0f
#define SCROLL_H_BIAS      1.4f

// Standard 8-byte USB/BLE keyboard report descriptor (Report ID 1) with an LED
// output report. modifier(1) + reserved(1) + 6 keycodes, plus 5 LED bits.
static const uint8_t keyboard_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, KBD_REPORT_ID, //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0 = Left Ctrl)
    0x29, 0xE7,        //   Usage Maximum (0xE7 = Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)            ; modifier bits
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs)         ; reserved byte
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data,Array,Abs)          ; 6 key slots
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x91, 0x02,        //   Output (Data,Var,Abs)           ; LED bits
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Const,Array,Abs)        ; LED padding
    0xC0,              // End Collection (keyboard)

    // ----- Mouse (Report ID 2): 3 buttons + relative X/Y/wheel + AC Pan -----
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, MOUSE_REPORT_ID, //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x03,        //     Usage Maximum (3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)          ; 3 buttons
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Const,Var,Abs)         ; padding
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data,Var,Rel)          ; X, Y, wheel
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     Usage (AC Pan)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data,Var,Rel)          ; AC Pan (horizontal wheel)
    0xC0,              //   End Collection (physical)
    0xC0,              // End Collection (mouse)

    // ----- Consumer Control (Report ID 3): one 16-bit usage (AC Back/Forward ...) -----
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, CC_REPORT_ID,//   Report ID (3)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (0x03FF)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x03FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Array,Abs)   ; one consumer usage at a time
    0xC0,              // End Collection (consumer)
};

static esp_hid_raw_report_map_t ble_report_maps[] = {
    {.data = keyboard_report_map, .len = sizeof(keyboard_report_map)},
};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id         = 0x16C0,
    .product_id        = 0x05DF,
    .version           = 0x0100,
    // Kept short on purpose: the GAP helper packs flags + TX power + appearance
    // + HID UUID + this name into one 31-byte legacy adv packet, leaving ~15
    // bytes for the name. A longer name fails with NimBLE rc=4 (EMSGSIZE).
    .device_name       = "Polsat IR Kbd",
    .manufacturer_name = "Sejin",
    .serial_number     = "000000",
    .report_maps       = ble_report_maps,
    .report_maps_len   = 1,
};

static esp_hidd_dev_t *s_hid_dev;
static volatile bool   s_ready;   // link up & encrypted -> input reports deliverable

// Current HID report state.
static uint8_t  s_modifiers;
static uint8_t  s_keys[6];
static uint8_t  s_mouse_buttons;
static uint16_t s_consumer;     // currently-held Consumer Control usage (0 = none)

// ---- Fn layer ----
// The Fn key (Sejin function 0x60) is a layer modifier: held, it never emits a
// key of its own but remaps a few others (Fn+F1..F4 switch BLE device, Fn over
// the 789/uio/jkl/m cluster is a numeric keypad with * / - + and ', and Fn does
// media: volume (left/right), track skip (up/down), play/pause (enter)).
#define FN_FUNCTION   0x60
static bool s_fn_held;

// Per Sejin function code, what the press emitted, so the matching release undoes
// exactly that even if Fn was let go in between (which would change the lookup):
//   0              = nothing down for that code,
//   1..0xFE        = that HID keycode is in the report (remove it on release),
//   KC_FN_CONSUMED = an Fn action handled it with no key in the report; the
//                    presence of this marker also stops the keyboard's repeated
//                    "still held" frames from re-firing one-shot actions.
#define KC_FN_CONSUMED 0xFF
static uint8_t s_emitted_kc[256];

// Pointer velocity state (see mouse_tick / ble_kbd_handle_mouse).
static float              s_vx, s_vy;          // smoothed velocity, px per ms
static float              s_carry_x, s_carry_y;// sub-pixel emit remainder
static int64_t            s_mouse_last_us;     // timestamp of previous movement frame
static esp_timer_handle_t s_mouse_timer;

// Scroll state (active while Fn is held during trackball movement).
static int                s_scroll_axis;       // 0 none / SCROLL_AXIS_V / SCROLL_AXIS_H
static float              s_scroll_accum;      // sub-step remainder for the locked axis
static float              s_scroll_sx, s_scroll_sy; // movement gathered before the axis locks
static int64_t            s_scroll_last_us;    // timestamp of previous scroll frame

// ---- 6-key-slot bookkeeping (ported from polsat-ir-receiver/hid_bridge.c) ----

static bool key_in_slots(uint8_t kc) {
    for (int i = 0; i < 6; i++) {
        if (s_keys[i] == kc) return true;
    }
    return false;
}

static bool add_key(uint8_t kc) {
    if (kc == 0 || key_in_slots(kc)) return false;
    for (int i = 0; i < 6; i++) {
        if (s_keys[i] == 0) {
            s_keys[i] = kc;
            return true;
        }
    }
    return false;
}

static bool remove_key(uint8_t kc) {
    if (kc == 0) return false;
    for (int i = 0; i < 6; i++) {
        if (s_keys[i] == kc) {
            s_keys[i] = 0;
            return true;
        }
    }
    return false;
}

static void clear_keys(void) {
    memset(s_keys, 0, sizeof(s_keys));
}

static void send_report(void) {
    if (!s_ready || s_hid_dev == NULL) return;
    uint8_t buf[KBD_REPORT_LEN] = {0};
    buf[0] = s_modifiers;        // buf[1] stays 0 (reserved)
    memcpy(&buf[2], s_keys, 6);
    esp_err_t err = esp_hidd_dev_input_set(s_hid_dev, 0, KBD_REPORT_ID, buf, KBD_REPORT_LEN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "input_set failed: %s", esp_err_to_name(err));
    }
}

static void send_consumer(uint16_t usage) {
    if (!s_ready || s_hid_dev == NULL) return;
    uint8_t buf[CC_REPORT_LEN] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
    esp_hidd_dev_input_set(s_hid_dev, 0, CC_REPORT_ID, buf, CC_REPORT_LEN);
}

// Fn-layer keycodes that behave like normal keys (go into the report on press,
// removed on release): the laptop numeric keypad over the 789/uio/jkl/m cluster
//   7 8 9 / u i o / j k l / m  ->  7 8 9 / 4 5 6 / 1 2 3 / 0
// plus the operator/quote column next to it.
static uint8_t fn_keycode(uint8_t function) {
    switch (function) {
    case 0x66: return HID_KEY_KEYPAD_7;         // 7
    case 0x68: return HID_KEY_KEYPAD_8;         // 8
    case 0x69: return HID_KEY_KEYPAD_9;         // 9
    case 0x26: return HID_KEY_KEYPAD_4;         // u
    case 0x28: return HID_KEY_KEYPAD_5;         // i
    case 0x29: return HID_KEY_KEYPAD_6;         // o
    case 0x16: return HID_KEY_KEYPAD_1;         // j
    case 0x18: return HID_KEY_KEYPAD_2;         // k
    case 0x19: return HID_KEY_KEYPAD_3;         // l
    case 0x36: return HID_KEY_KEYPAD_0;         // m
    case 0x6B: return HID_KEY_KEYPAD_MULTIPLY;  // 0 -> *
    case 0x2B: return HID_KEY_KEYPAD_SUBTRACT;  // P -> -
    case 0x1B: return HID_KEY_KEYPAD_ADD;       // ; -> +
    case 0x3B: return HID_KEY_KEYPAD_DIVIDE;    // / -> keypad /
    case 0x4B: return HID_KEY_EQUAL;            // = -> = (dedicated keypad =/, usages proved unreliable)
    case 0x39: return HID_KEY_COMMA;            // . -> ,
    default:   return 0;
    }
}

// Fn+left/right -> volume down/up (repeats while held), else 0.
static uint16_t fn_consumer(uint8_t function) {
    switch (function) {
    case 0x0D: return HID_CC_VOLUME_DOWN;  // left
    case 0x04: return HID_CC_VOLUME_UP;    // right
    default:   return 0;
    }
}

// Fn+up/down/enter -> media (one tap per press), else 0.
static uint16_t fn_consumer_once(uint8_t function) {
    switch (function) {
    case 0x5C: return HID_CC_SCAN_NEXT_TRACK;      // up
    case 0x6C: return HID_CC_SCAN_PREVIOUS_TRACK;  // down
    case 0x0B: return HID_CC_PLAY_PAUSE;           // enter
    default:   return 0;
    }
}

// Fn+F1..F4 -> BLE device slot 0..3, else -1.
static int fn_device_slot(uint8_t function) {
    switch (function) {
    case 0x53: return 0;  // F1
    case 0x52: return 1;  // F2
    case 0x42: return 2;  // F3
    case 0x54: return 3;  // F4
    default:   return -1;
    }
}

// Send a one-shot modifier+key chord (e.g. Cmd+[) without disturbing whatever is
// currently held: snapshot the report, send the chord, then restore.
static void tap_combo(uint8_t mod, uint8_t keycode) {
    uint8_t save_mod = s_modifiers;
    uint8_t save_keys[6];
    memcpy(save_keys, s_keys, sizeof save_keys);

    s_modifiers = mod;
    memset(s_keys, 0, sizeof s_keys);
    s_keys[0] = keycode;
    send_report();                                  // press the chord

    s_modifiers = save_mod;
    memcpy(s_keys, save_keys, sizeof save_keys);
    send_report();                                  // release it / restore state
}

// Drop the current host and (re)advertise as the given device slot, blinking
// until a host connects. The new slot is persisted; on reboot we come up on it.
static void switch_device(uint8_t slot) {
    ESP_LOGI(TAG, "Fn: switch to device slot %u", (unsigned)(slot + 1));
    status_led_set_pairing(true);   // blink until the slot's host connects
    s_ready = false;
    s_modifiers = 0;
    s_mouse_buttons = 0;
    s_consumer = 0;
    s_vx = s_vy = 0;
    s_carry_x = s_carry_y = 0;
    s_mouse_last_us = 0;
    s_scroll_axis = 0; s_scroll_accum = 0; s_scroll_last_us = 0; s_scroll_sx = s_scroll_sy = 0;
    clear_keys();                   // s_emitted_kc is physical-key state, kept on purpose
    esp_hid_ble_gap_switch_device(slot);
}

void ble_kbd_handle_key(uint8_t function, bool toggle) {
    if (function == 0xFF) {  // all-keys-up sentinel
        s_modifiers = 0;
        s_fn_held = false;
        clear_keys();
        memset(s_emitted_kc, 0, sizeof s_emitted_kc);
        send_report();
        if (s_consumer) { s_consumer = 0; send_consumer(0); }
        return;
    }

    if (function == FN_FUNCTION) {   // layer key: emits nothing, just tracks state
        s_fn_held = !toggle;         // press (toggle=0) holds, release (1) lets go
        if (!s_fn_held) { s_scroll_axis = 0; s_scroll_accum = 0; s_scroll_sx = s_scroll_sy = 0; }  // end scroll gesture
        return;
    }

    // Release / final frame: undo exactly what the press did for this function
    // code, regardless of the current Fn state.
    if (toggle) {
        uint8_t kc = s_emitted_kc[function];
        if (kc == KC_FN_CONSUMED) {        // press was an Fn action -> nothing to undo
            s_emitted_kc[function] = 0;
            return;
        }
        if (kc != 0) {                     // a real keycode is down for this function
            s_emitted_kc[function] = 0;
            if (remove_key(kc)) send_report();
            return;
        }
        // No press recorded: fall back to the normal mapping (handles modifiers,
        // consumer keys, and the "release-only packet -> synthesize a tap" case).
        const key_map_t *m = keymap_lookup(function);
        if (!m) return;
        if (m->consumer) {
            if (s_consumer) { s_consumer = 0; send_consumer(0); }
            return;
        }
        if (m->modifier) {
            uint8_t next = (uint8_t)(s_modifiers & (uint8_t)~m->modifier);
            if (next != s_modifiers) { s_modifiers = next; send_report(); }
            return;
        }
        if (m->keycode && add_key(m->keycode)) {
            send_report();
            remove_key(m->keycode);
            send_report();
        }
        return;
    }

    // Press / hold. Fn layer takes priority over the base mapping.
    if (s_fn_held) {
        // One-shot actions: the KC_FN_CONSUMED marker makes them fire once per
        // physical press despite the keyboard's repeated "still held" frames.
        int slot = fn_device_slot(function);
        if (slot >= 0) {
            if (s_emitted_kc[function] != KC_FN_CONSUMED) {
                s_emitted_kc[function] = KC_FN_CONSUMED;
                switch_device((uint8_t)slot);
            }
            return;
        }
        // Media track skip: one tap per press (don't skip repeatedly while held).
        uint16_t cc_once = fn_consumer_once(function);
        if (cc_once) {
            if (s_emitted_kc[function] != KC_FN_CONSUMED) {
                s_emitted_kc[function] = KC_FN_CONSUMED;
                send_consumer(cc_once);
                send_consumer(0);
            }
            return;
        }
        // Volume: tap the consumer usage each frame, so holding repeats the step.
        uint16_t cc = fn_consumer(function);
        if (cc) {
            s_emitted_kc[function] = KC_FN_CONSUMED;  // release must not fall through
            send_consumer(cc);
            send_consumer(0);
            return;
        }
        // Keypad digits / operators / quote: ordinary keys in the report.
        uint8_t kp = fn_keycode(function);
        if (kp) {
            if (add_key(kp)) {
                s_emitted_kc[function] = kp;
                send_report();
            }
            return;
        }
        // Not an Fn-mapped key: fall through to the base mapping.
    }

    const key_map_t *m = keymap_lookup(function);
    if (!m) return;

    if (m->consumer) {
        if (m->consumer != s_consumer) { s_consumer = m->consumer; send_consumer(m->consumer); }
        return;
    }
    // Key with a modifier (e.g. Back = Cmd+[): one chord tap per press.
    if (m->keycode && m->modifier) {
        if (s_emitted_kc[function] != KC_FN_CONSUMED) {
            s_emitted_kc[function] = KC_FN_CONSUMED;
            tap_combo(m->modifier, m->keycode);
        }
        return;
    }
    if (m->modifier) {
        uint8_t next = (uint8_t)(s_modifiers | m->modifier);
        if (next != s_modifiers) { s_modifiers = next; send_report(); }
        return;
    }
    if (m->keycode && add_key(m->keycode)) {
        s_emitted_kc[function] = m->keycode;
        send_report();
    }
}

static void send_mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel, int8_t pan) {
    if (!s_ready || s_hid_dev == NULL) return;
    uint8_t buf[MOUSE_REPORT_LEN] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel, (uint8_t)pan};
    esp_err_t err = esp_hidd_dev_input_set(s_hid_dev, 0, MOUSE_REPORT_ID, buf, MOUSE_REPORT_LEN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mouse input_set failed: %s", esp_err_to_name(err));
    }
}

// 60 Hz emitter: move at the smoothed velocity. When the trackball goes quiet
// (no frame for MOUSE_COAST_MS) the velocity decays so motion glides to a stop
// instead of either freezing abruptly or sailing on forever.
static void mouse_tick(void *arg) {
    if (!s_ready) return;
    if (s_vx == 0.0f && s_vy == 0.0f) return;

    float age_ms = (float)(esp_timer_get_time() - s_mouse_last_us) / 1000.0f;
    if (age_ms > MOUSE_COAST_MS) {
        s_vx *= MOUSE_DECAY;
        s_vy *= MOUSE_DECAY;
        if (fabsf(s_vx) < 0.003f) s_vx = 0.0f;
        if (fabsf(s_vy) < 0.003f) s_vy = 0.0f;
    }

    const float tick_ms = (float)MOUSE_TICK_US / 1000.0f;
    s_carry_x += s_vx * tick_ms;
    s_carry_y += s_vy * tick_ms;
    int ex = (int)truncf(s_carry_x); s_carry_x -= (float)ex;
    int ey = (int)truncf(s_carry_y); s_carry_y -= (float)ey;
    if (ex > 127) ex = 127; else if (ex < -127) ex = -127;
    if (ey > 127) ey = 127; else if (ey < -127) ey = -127;
    if (ex == 0 && ey == 0) return;
    send_mouse(s_mouse_buttons, (int8_t)ex, (int8_t)ey, 0, 0);
}

void ble_kbd_handle_mouse(const sejin_frame_t *s) {
    if (!s_ready) return;                   // ignore while disconnected

    if (s->is_button_event) {
        if (s->button_down) {
            if (s->obc == 1) s_mouse_buttons |= MOUSE_BTN_LEFT;
            else if (s->obc == 2) s_mouse_buttons |= MOUSE_BTN_RIGHT;
            else if (s->obc == 3) s_mouse_buttons |= MOUSE_BTN_MIDDLE;
        } else {
            s_mouse_buttons = 0;            // any button-up frame releases all
        }
        send_mouse(s_mouse_buttons, 0, 0, 0, 0);  // buttons go out immediately
        return;
    }

    // ---- Fn held: scroll instead of moving the pointer. Snap to one axis so it
    // never scrolls diagonally; emit wheel (vertical) or AC Pan (horizontal). ----
    if (s_fn_held) {
        s_vx = s_vy = 0.0f;               // Fn held -> scroll, never glide the pointer
        s_carry_x = s_carry_y = 0.0f;

        int64_t snow = esp_timer_get_time();
        if (s_scroll_last_us && snow - s_scroll_last_us > SCROLL_RELOCK_US) {
            s_scroll_axis  = 0;            // paused long enough -> re-pick the axis
            s_scroll_accum = 0.0f;
            s_scroll_sx = s_scroll_sy = 0.0f;
        }
        s_scroll_last_us = snow;

        // Choose the axis from movement gathered over the first few counts (a
        // single frame is too noisy and often mis-picks horizontal), biased
        // toward vertical. Emit nothing until the axis is locked.
        if (s_scroll_axis == 0) {
            s_scroll_sx += (float)s->x;
            s_scroll_sy += (float)s->y;
            float asx = fabsf(s_scroll_sx), asy = fabsf(s_scroll_sy);
            if (asx + asy < SCROLL_LOCK_COUNTS) return;   // wait for a clearer direction
            s_scroll_axis  = (asx > asy * SCROLL_H_BIAS) ? SCROLL_AXIS_H : SCROLL_AXIS_V;
            s_scroll_accum = (s_scroll_axis == SCROLL_AXIS_V) ? s_scroll_sy : s_scroll_sx;
        } else {
            s_scroll_accum += (s_scroll_axis == SCROLL_AXIS_V) ? (float)s->y : (float)s->x;
        }

        // Up-roll scrolls up, right-roll scrolls right. Flip a sign if it feels
        // inverted (e.g. macOS "natural scrolling").
        int steps = (int)(s_scroll_accum / SCROLL_DIV);
        if (steps != 0) {
            s_scroll_accum -= (float)steps * SCROLL_DIV;
            if (steps > 127) steps = 127; else if (steps < -127) steps = -127;
            if (s_scroll_axis == SCROLL_AXIS_V) send_mouse(s_mouse_buttons, 0, 0, (int8_t)steps, 0);
            else                                send_mouse(s_mouse_buttons, 0, 0, 0, (int8_t)steps);
        }
        return;
    }

    // Time since the previous movement frame.
    int64_t now = esp_timer_get_time();
    float dt_ms = s_mouse_last_us ? (float)(now - s_mouse_last_us) / 1000.0f
                                  : (float)MOUSE_DT_MIN_MS;
    s_mouse_last_us = now;
    if (dt_ms < MOUSE_DT_MIN_MS) dt_ms = MOUSE_DT_MIN_MS;
    if (dt_ms > MOUSE_DT_MAX_MS) dt_ms = MOUSE_DT_MAX_MS;

    // Instantaneous velocity (px/ms) with speed-based acceleration, low-passed
    // into the smoothed velocity to reject the bursty per-frame jitter.
    float speed   = sqrtf((float)s->x * s->x + (float)s->y * s->y) / dt_ms; // counts/ms
    float factor  = MOUSE_GAIN * (1.0f + MOUSE_ACCEL * speed);
    float inst_vx = (float)s->x * factor / dt_ms;
    float inst_vy = -(float)s->y * factor / dt_ms;

    // Faster filter when accelerating (responsive), slower when steady/slowing.
    float inst_sp = sqrtf(inst_vx * inst_vx + inst_vy * inst_vy);
    float cur_sp  = sqrtf(s_vx * s_vx + s_vy * s_vy);
    float a = (inst_sp > cur_sp) ? MOUSE_EMA_ATTACK : MOUSE_EMA_RELEASE;
    s_vx += a * (inst_vx - s_vx);
    s_vy += a * (inst_vy - s_vy);
}

bool ble_kbd_connected(void) {
    return s_ready;
}

// Called from esp_hid_gap.c's NimBLE GAP handler once the link is encrypted.
void ble_hid_task_start_up(void) {
    s_ready = true;
    status_led_set_pairing(false);   // a host paired/reconnected -> stop blinking
    ESP_LOGI(TAG, "link encrypted, ready to send key reports");
}

// Forget the current host and advertise fresh so a new one can pair. The blink
// is started here for immediate feedback; ble_hid_task_start_up() ends it once a
// host completes pairing.
void ble_kbd_enter_pairing(void) {
    ESP_LOGI(TAG, "entering pairing mode (clearing bonds, re-advertising)");
    status_led_set_pairing(true);
    s_ready = false;
    s_modifiers = 0;
    s_mouse_buttons = 0;
    s_consumer = 0;
    s_fn_held = false;
    s_vx = s_vy = 0;
    s_carry_x = s_carry_y = 0;
    s_mouse_last_us = 0;
    s_scroll_axis = 0; s_scroll_accum = 0; s_scroll_last_us = 0; s_scroll_sx = s_scroll_sy = 0;
    clear_keys();
    memset(s_emitted_kc, 0, sizeof s_emitted_kc);
    esp_hid_ble_gap_pairing_reset();
}

// Referenced by the GAP helper; nothing to tear down here.
void ble_hid_task_shut_down(void) {
    s_ready = false;
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base,
                                    int32_t id, void *event_data) {
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "HIDD START -> advertising");
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "HIDD CONNECT");
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(TAG, "HIDD DISCONNECT -> re-advertising");
        s_ready = false;
        s_modifiers = 0;
        s_mouse_buttons = 0;
        s_consumer = 0;
        // s_fn_held and s_emitted_kc track physical key state, not the link, so
        // they are intentionally NOT reset here: a device switch disconnects while
        // Fn+Fx is still held, and clearing the KC_FN_CONSUMED marker would let the
        // keyboard's repeat frames re-fire the switch. They self-correct on the
        // next Fn frame / key release; stale keycodes just fail remove_key quietly.
        s_vx = s_vy = 0;
        s_carry_x = s_carry_y = 0;
        s_mouse_last_us = 0;
        s_scroll_axis = 0; s_scroll_accum = 0; s_scroll_last_us = 0; s_scroll_sx = s_scroll_sy = 0;
        clear_keys();
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_OUTPUT_EVENT:   // host LED state (Caps/Num lock) - ignored
    case ESP_HIDD_PROTOCOL_MODE_EVENT:
    case ESP_HIDD_CONTROL_EVENT:
    case ESP_HIDD_FEATURE_EVENT:
    case ESP_HIDD_STOP_EVENT:
    default:
        break;
    }
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();              // returns only on nimble_port_stop()
    nimble_port_freertos_deinit();
}

// Provided by NimBLE's store/config package (no public header, forward-declared
// the same way the esp-idf hid_device example does).
void ble_store_config_init(void);

void ble_kbd_init(void) {
    // Backstop: keep NimBLE's ESP-log tag quiet (it logs per-notify at INFO).
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_hid_gap_init(HID_DEV_MODE));
    ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD,
                                             ble_hid_config.device_name));
    ESP_ERROR_CHECK(esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE,
                                      ble_hidd_event_callback, &s_hid_dev));

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Periodic timer that drains accumulated pointer movement into smooth reports.
    const esp_timer_create_args_t mouse_timer_args = {
        .callback = mouse_tick,
        .name     = "mouse_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&mouse_timer_args, &s_mouse_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_mouse_timer, MOUSE_TICK_US));

    ESP_ERROR_CHECK(esp_nimble_enable(ble_host_task));
    ESP_LOGI(TAG, "BLE keyboard up, advertising as '%s'", ble_hid_config.device_name);
}
