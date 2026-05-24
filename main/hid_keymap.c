#include "hid_keymap.h"
#include <stddef.h>

// Function-code -> HID key mapping, ported from polsat-ir-receiver/hid_bridge.c.
// The "name" field is added purely so decoded keypresses read nicely in the log.
static const key_map_t key_map[] = {
    {0x02, HID_KEY_B,            0,                "B"},
    {0x03, HID_KEY_TAB,          0,                "Tab"},
    {0x04, HID_KEY_ARROW_RIGHT,  0,                "Right"},
    {0x05, HID_KEY_SPACE,        0,                "Space"},
    {0x06, HID_KEY_APOSTROPHE,   0,                "'"},
    {0x07, 0,                    HID_MOD_LSHIFT,   "LShift"},
    {0x08, HID_KEY_MINUS,        0,                "-"},
    {0x09, HID_KEY_BRACKET_LEFT, 0,                "["},
    {0x0B, HID_KEY_ENTER,        0,                "Enter"},
    {0x0C, HID_KEY_BRACKET_LEFT, HID_MOD_LGUI,     "Back"},   // macOS browser back = Cmd+[
    {0x0D, HID_KEY_ARROW_LEFT,   0,                "Left"},
    {0x12, HID_KEY_A,            0,                "A"},
    {0x13, HID_KEY_S,            0,                "S"},
    {0x14, HID_KEY_D,            0,                "D"},
    {0x15, HID_KEY_F,            0,                "F"},
    {0x16, HID_KEY_J,            0,                "J"},
    {0x18, HID_KEY_K,            0,                "K"},
    {0x19, HID_KEY_L,            0,                "L"},
    {0x1B, HID_KEY_SEMICOLON,    0,                ";"},
    {0x1C, HID_KEY_PAGE_DOWN,    0,                "PageDown"},
    {0x1D, HID_KEY_END,          0,                "End"},
    {0x1E, 0,                    HID_MOD_LGUI,     "LGui"},
    {0x21, 0,                    HID_MOD_RALT,     "RAlt"},
    {0x22, HID_KEY_Q,            0,                "Q"},
    {0x23, HID_KEY_W,            0,                "W"},
    {0x24, HID_KEY_E,            0,                "E"},
    {0x25, HID_KEY_R,            0,                "R"},
    {0x26, HID_KEY_U,            0,                "U"},
    {0x28, HID_KEY_I,            0,                "I"},
    {0x29, HID_KEY_O,            0,                "O"},
    {0x2B, HID_KEY_P,            0,                "P"},
    {0x2C, HID_KEY_BRACKET_RIGHT,0,                "]"},
    {0x2D, HID_KEY_NUM_LOCK,     0,                "NumLock"},
    {0x32, HID_KEY_Z,            0,                "Z"},
    {0x33, HID_KEY_X,            0,                "X"},
    {0x34, HID_KEY_C,            0,                "C"},
    {0x35, HID_KEY_V,            0,                "V"},
    {0x36, HID_KEY_M,            0,                "M"},
    {0x37, 0,                    HID_MOD_RSHIFT,   "RShift"},
    {0x38, HID_KEY_COMMA,        0,                ","},
    {0x39, HID_KEY_PERIOD,       0,                "."},
    {0x3B, HID_KEY_SLASH,        0,                "/"},
    {0x3C, HID_KEY_NONUS_BACKSLASH, 0,             "§"},  // ISO Non-US key; § on ISO layouts that place it there
    {0x3D, HID_KEY_N,            0,                "N"},
    {0x41, HID_KEY_BRACKET_RIGHT,HID_MOD_LGUI,     "Forward"},// macOS browser forward = Cmd+]
    {0x42, HID_KEY_F3,           0,                "F3"},
    {0x43, HID_KEY_ESCAPE,       0,                "Esc"},
    {0x44, HID_KEY_F5,           0,                "F5"},
    {0x45, HID_KEY_5,            0,                "5"},
    {0x46, HID_KEY_6,            0,                "6"},
    {0x48, HID_KEY_F6,           0,                "F6"},
    {0x49, HID_KEY_F8,           0,                "F8"},
    {0x4B, HID_KEY_EQUAL,        0,                "="},
    {0x4C, HID_KEY_BACKSPACE,    0,                "Backspace"},
    {0x4D, HID_KEY_HOME,         0,                "Home"},
    {0x4E, HID_KEY_BACKSLASH,    0,                "\\"},
    {0x52, HID_KEY_F2,           0,                "F2"},
    {0x53, HID_KEY_F1,           0,                "F1"},
    {0x54, HID_KEY_F4,           0,                "F4"},
    {0x55, HID_KEY_G,            0,                "G"},
    {0x56, HID_KEY_H,            0,                "H"},
    {0x58, HID_KEY_F7,           0,                "F7"},
    {0x59, HID_KEY_F9,           0,                "F9"},
    {0x5C, HID_KEY_ARROW_UP,     0,                "Up"},
    {0x5D, HID_KEY_PAGE_UP,      0,                "PageUp"},
    {0x5F, 0,                    HID_MOD_LCTRL,    "LCtrl"},
    {0x60, 0,                    0,                "Fn"},  // layer key; handled in ble_kbd.c
    {0x62, HID_KEY_1,            0,                "1"},
    {0x63, HID_KEY_2,            0,                "2"},
    {0x64, HID_KEY_3,            0,                "3"},
    {0x65, HID_KEY_4,            0,                "4"},
    {0x66, HID_KEY_7,            0,                "7"},
    {0x68, HID_KEY_8,            0,                "8"},
    {0x69, HID_KEY_9,            0,                "9"},
    {0x6B, HID_KEY_0,            0,                "0"},
    {0x6C, HID_KEY_ARROW_DOWN,   0,                "Down"},
    {0x6D, HID_KEY_DELETE,       0,                "Delete"},
    {0x71, 0,                    HID_MOD_LALT,     "LAlt"},
    {0x72, HID_KEY_GRAVE,        0,                "`"},
    {0x73, HID_KEY_CAPS_LOCK,    0,                "CapsLock"},
    {0x75, HID_KEY_T,            0,                "T"},
    {0x76, HID_KEY_Y,            0,                "Y"},
    {0x78, HID_KEY_F11,          0,                "F11"},
    {0x79, HID_KEY_F10,          0,                "F10"},
    {0x7B, HID_KEY_F12,          0,                "F12"},
    {0x7C, HID_KEY_PRINT_SCREEN, 0,                "PrintScreen"},
};

const key_map_t *keymap_lookup(uint8_t function) {
    for (size_t i = 0; i < sizeof(key_map) / sizeof(key_map[0]); i++) {
        if (key_map[i].function == function) {
            return &key_map[i];
        }
    }
    return NULL;
}
