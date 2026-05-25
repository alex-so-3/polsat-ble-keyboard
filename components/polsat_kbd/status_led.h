#pragma once
#include <stdbool.h>

// Single-color status LED driver (e.g. the on-board LED of an ESP32-C3 SuperMini
// on GPIO8, active-low). A periodic timer renders one of these states, in
// priority order:
//   1. boot indication  - solid on for ~1 s right after start-up,
//   2. pairing mode      - blinks while waiting for a new host to pair,
//   3. IR activity       - a very short flash each time a valid IR frame arrives,
//   4. idle              - off.
//
// `active_low` is true when pulling the GPIO LOW lights the LED.
void status_led_init(int gpio_num, bool active_low);

// Enter (true) or leave (false) the pairing-mode blink.
void status_led_set_pairing(bool on);

// Briefly flash the LED to acknowledge a freshly decoded IR frame.
void status_led_activity(void);
