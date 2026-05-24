#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sejin.h"

// Bring up the NimBLE host and advertise as a BLE HID keyboard + mouse.
void ble_kbd_init(void);

// True once a host is connected and the link is encrypted, i.e. reports can be
// delivered.
bool ble_kbd_connected(void);

// Feed a decoded Sejin-1 key event: `function` is the Fy function code and
// `toggle` is the protocol's toggle bit (false = press/hold, true = release /
// final frame). Updates the 8-byte HID keyboard report state and sends it over
// BLE when connected and the state changed. `function == 0xFF` releases all keys.
void ble_kbd_handle_key(uint8_t function, bool toggle);

// Feed a decoded Sejin-2 (trackball) frame: relative movement or a button
// event. Sends a BLE mouse report when connected.
void ble_kbd_handle_mouse(const sejin_frame_t *s);

// Enter pairing mode: clear stored BLE bonds, drop any active link, and
// advertise fresh so a new host can pair. Bound to a long-press of BOOT.
void ble_kbd_enter_pairing(void);
