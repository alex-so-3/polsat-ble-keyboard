#include "polsat_kbd.h"

#include <cstring>

#include "esphome/core/log.h"

// The reused firmware is plain C; give it C linkage so it links against the
// .c objects compiled in this component.
extern "C" {
#include "ir.h"
#include "ir_rmt.h"
#include "sejin.h"
#include "ble_kbd.h"
#include "status_led.h"
#include "boot_btn.h"
}

namespace esphome {
namespace polsat_kbd {

static const char *const TAG = "polsat_kbd";

// BOOT button callback runs in the esp_timer task; keep it tiny — just flag the
// request and blink, then service the (NimBLE-heavy) reset from loop().
static volatile bool s_pairing_req = false;
static void on_boot_long_press() {
  status_led_set_pairing(true);
  s_pairing_req = true;
}

void PolsatKbd::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Polsat IR keyboard (BLE HID)...");
  if (this->led_pin_ >= 0) {
    status_led_init(this->led_pin_, this->led_active_low_);
  }
  if (this->boot_pin_ >= 0) {
    boot_btn_init(this->boot_pin_, on_boot_long_press);
  }
  ble_kbd_init();          // NVS + NimBLE + esp_hid + mouse timer (own FreeRTOS task)
  ir_rmt_init(this->ir_pin_);
}

void PolsatKbd::loop() {
  if (s_pairing_req) {
    s_pairing_req = false;
    ble_kbd_enter_pairing();
  }

  // Non-blocking drain: pull whatever IR frames are queued, then return so we
  // don't stall ESPHome's loop. NimBLE + the esp_timer callbacks run elsewhere.
  ir_frame_t frame;
  sejin_frame_t s;
  while (ir_rmt_get_frame(&frame, 0)) {
    if (!decode_sejin_38(&frame, &s)) {
      continue;
    }
    status_led_activity();
    if (s.is_sejin1) {
      this->handle_key_(s.function, s.toggle);
    } else {
      ble_kbd_handle_mouse(&s);
    }
  }
}

void PolsatKbd::handle_key_(uint8_t function, bool toggle) {
  if (function == 0xFF) {  // all-keys-up sentinel
    this->action_held_ = false;
    memset(this->fired_, 0, sizeof this->fired_);
    ble_kbd_handle_key(0xFF, toggle);
    return;
  }

  // The action modifier: tracks state, never types.
  if (this->action_key_ >= 0 && function == (uint8_t) this->action_key_) {
    this->action_held_ = !toggle;
    if (!this->action_held_) {
      memset(this->fired_, 0, sizeof this->fired_);
    }
    return;
  }

  // While the action modifier is held, a configured key fires its trigger
  // (once per press) instead of typing.
  if (this->action_held_) {
    for (auto &combo : this->combos_) {
      if (combo.first == function) {
        if (!toggle) {
          if (!this->fired_[function]) {
            this->fired_[function] = 1;
            ESP_LOGD(TAG, "combo fn=0x%02X -> ESPHome action", function);
            combo.second->trigger();
          }
        } else {
          this->fired_[function] = 0;
        }
        return;  // suppressed from HID
      }
    }
    // action held but this key has no combo -> fall through and type normally
  }

  ble_kbd_handle_key(function, toggle);
}

void PolsatKbd::dump_config() {
  ESP_LOGCONFIG(TAG, "Polsat IR keyboard:");
  ESP_LOGCONFIG(TAG, "  IR pin: GPIO%d", this->ir_pin_);
  ESP_LOGCONFIG(TAG, "  LED pin: %d (active_low=%s)", this->led_pin_,
                YESNO(this->led_active_low_));
  ESP_LOGCONFIG(TAG, "  BOOT pin: %d", this->boot_pin_);
  if (this->action_key_ >= 0) {
    ESP_LOGCONFIG(TAG, "  action_key: 0x%02X, combos: %d", this->action_key_,
                  (int) this->combos_.size());
  } else {
    ESP_LOGCONFIG(TAG, "  action combos: disabled (no action_key)");
  }
}

}  // namespace polsat_kbd
}  // namespace esphome
