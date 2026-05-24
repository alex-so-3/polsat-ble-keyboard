#pragma once
#include <stdint.h>

// Standard USB HID keyboard usage IDs (HID Usage Tables, keyboard page 0x07).
// These same values are used by USB and by BLE HID reports, so the keymap is
// reusable once the BLE transport is added.

#define HID_KEY_NONE          0x00
#define HID_KEY_A             0x04
#define HID_KEY_B             0x05
#define HID_KEY_C             0x06
#define HID_KEY_D             0x07
#define HID_KEY_E             0x08
#define HID_KEY_F             0x09
#define HID_KEY_G             0x0A
#define HID_KEY_H             0x0B
#define HID_KEY_I             0x0C
#define HID_KEY_J             0x0D
#define HID_KEY_K             0x0E
#define HID_KEY_L             0x0F
#define HID_KEY_M             0x10
#define HID_KEY_N             0x11
#define HID_KEY_O             0x12
#define HID_KEY_P             0x13
#define HID_KEY_Q             0x14
#define HID_KEY_R             0x15
#define HID_KEY_S             0x16
#define HID_KEY_T             0x17
#define HID_KEY_U             0x18
#define HID_KEY_V             0x19
#define HID_KEY_W             0x1A
#define HID_KEY_X             0x1B
#define HID_KEY_Y             0x1C
#define HID_KEY_Z             0x1D
#define HID_KEY_1             0x1E
#define HID_KEY_2             0x1F
#define HID_KEY_3             0x20
#define HID_KEY_4             0x21
#define HID_KEY_5             0x22
#define HID_KEY_6             0x23
#define HID_KEY_7             0x24
#define HID_KEY_8             0x25
#define HID_KEY_9             0x26
#define HID_KEY_0             0x27
#define HID_KEY_ENTER         0x28
#define HID_KEY_ESCAPE        0x29
#define HID_KEY_BACKSPACE     0x2A
#define HID_KEY_TAB           0x2B
#define HID_KEY_SPACE         0x2C
#define HID_KEY_MINUS         0x2D
#define HID_KEY_EQUAL         0x2E
#define HID_KEY_BRACKET_LEFT  0x2F
#define HID_KEY_BRACKET_RIGHT 0x30
#define HID_KEY_BACKSLASH     0x31
#define HID_KEY_SEMICOLON     0x33
#define HID_KEY_APOSTROPHE    0x34
#define HID_KEY_GRAVE         0x35
#define HID_KEY_COMMA         0x36
#define HID_KEY_PERIOD        0x37
#define HID_KEY_SLASH         0x38
#define HID_KEY_CAPS_LOCK     0x39
#define HID_KEY_F1            0x3A
#define HID_KEY_F2            0x3B
#define HID_KEY_F3            0x3C
#define HID_KEY_F4            0x3D
#define HID_KEY_F5            0x3E
#define HID_KEY_F6            0x3F
#define HID_KEY_F7            0x40
#define HID_KEY_F8            0x41
#define HID_KEY_F9            0x42
#define HID_KEY_F10           0x43
#define HID_KEY_F11           0x44
#define HID_KEY_F12           0x45
#define HID_KEY_PRINT_SCREEN  0x46
#define HID_KEY_HOME          0x4A
#define HID_KEY_PAGE_UP       0x4B
#define HID_KEY_DELETE        0x4C
#define HID_KEY_END           0x4D
#define HID_KEY_PAGE_DOWN     0x4E
#define HID_KEY_ARROW_RIGHT   0x4F
#define HID_KEY_ARROW_LEFT    0x50
#define HID_KEY_ARROW_DOWN    0x51
#define HID_KEY_ARROW_UP      0x52

// Numeric keypad. The digits produce numbers only while the host's Num Lock is
// on; with Num Lock off they act as Home/arrows/PageUp etc. The operators
// (/ * - +) always produce their symbol regardless of Num Lock.
#define HID_KEY_KEYPAD_1        0x59
#define HID_KEY_KEYPAD_2        0x5A
#define HID_KEY_KEYPAD_3        0x5B
#define HID_KEY_KEYPAD_4        0x5C
#define HID_KEY_KEYPAD_5        0x5D
#define HID_KEY_KEYPAD_6        0x5E
#define HID_KEY_KEYPAD_7        0x5F
#define HID_KEY_KEYPAD_8        0x60
#define HID_KEY_KEYPAD_9        0x61
#define HID_KEY_KEYPAD_0        0x62
#define HID_KEY_KEYPAD_DIVIDE   0x54
#define HID_KEY_KEYPAD_MULTIPLY 0x55
#define HID_KEY_KEYPAD_SUBTRACT 0x56
#define HID_KEY_KEYPAD_ADD      0x57
#define HID_KEY_KEYPAD_EQUAL    0x67
#define HID_KEY_KEYPAD_COMMA    0x85
#define HID_KEY_NUM_LOCK        0x53

// The extra key ISO keyboards have that ANSI ones lack (next to Left Shift). On
// ISO layouts that place § there it produces the section/paragraph sign.
#define HID_KEY_NONUS_BACKSLASH 0x64

// HID keyboard modifier bitmasks (byte 0 of a boot keyboard report).
#define HID_MOD_LCTRL         0x01
#define HID_MOD_LSHIFT        0x02
#define HID_MOD_LALT          0x04
#define HID_MOD_LGUI          0x08
#define HID_MOD_RCTRL         0x10
#define HID_MOD_RSHIFT        0x20
#define HID_MOD_RALT          0x40
#define HID_MOD_RGUI          0x80

// Consumer Control usages (HID usage page 0x0C), sent via the consumer report.
#define HID_CC_AC_BACK      0x0224
#define HID_CC_AC_FORWARD   0x0225
#define HID_CC_VOLUME_UP    0x00E9
#define HID_CC_VOLUME_DOWN  0x00EA
#define HID_CC_MUTE         0x00E2
#define HID_CC_SCAN_NEXT_TRACK     0x00B5
#define HID_CC_SCAN_PREVIOUS_TRACK 0x00B6
#define HID_CC_PLAY_PAUSE          0x00CD

typedef struct {
    uint8_t     function;  // Sejin-1 function code (Fy)
    uint8_t     keycode;   // USB HID usage id, 0 for a pure modifier / consumer key
    uint8_t     modifier;  // HID_MOD_* bit, 0 for a normal key
    const char *name;      // human-readable label, for logging
    uint16_t    consumer;  // Consumer Control usage (page 0x0C); 0 if not a CC key
} key_map_t;

// Returns the mapping for a Sejin-1 function code, or NULL if unmapped.
const key_map_t *keymap_lookup(uint8_t function);
