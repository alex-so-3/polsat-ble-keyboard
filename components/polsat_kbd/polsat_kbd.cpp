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

  // Run the IR capture in its own task (not ESPHome's shared loop): it blocks on
  // each frame and re-arms the RMT immediately, so fast trackball frame bursts are
  // not missed. Combos detected here are queued and fired from loop().
  this->combo_queue_ = xQueueCreate(8, sizeof(uint8_t));
  xTaskCreate(PolsatKbd::ir_task_trampoline_, "polsat_ir", 4096, this, 5,
              &this->ir_task_handle_);
}

void PolsatKbd::ir_task_() {
  ir_frame_t frame;
  sejin_frame_t s;
  for (;;) {
    // Blocks until a frame is ready, then re-arms the RMT immediately — so dense
    // trackball frame bursts are captured without the gaps a shared loop() leaves.
    if (!ir_rmt_get_frame(&frame, portMAX_DELAY)) {
      continue;
    }
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

void PolsatKbd::loop() {
  if (s_pairing_req) {
    s_pairing_req = false;
    ble_kbd_enter_pairing();
  }
  // Fire any combos the IR task queued (ESPHome triggers must run in loop()).
  uint8_t idx;
  while (this->combo_queue_ != nullptr &&
         xQueueReceive(this->combo_queue_, &idx, 0) == pdTRUE) {
    if (idx < this->combos_.size()) {
      ESP_LOGD(TAG, "combo[%u] -> ESPHome action", (unsigned) idx);
      this->combos_[idx].trigger->trigger();
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

  // A key currently consumed as a combo: swallow every further frame (repeats and
  // the release) so the firmware never types or acts on it.
  if (this->fired_[function]) {
    if (toggle) {
      this->fired_[function] = 0;
    }
    return;
  }

  // The action modifier (default Fn): track it AND forward it to the firmware, so
  // the firmware's own Fn layer (multi-device / media / numpad) keeps working.
  if (this->action_key_ >= 0 && function == (uint8_t) this->action_key_) {
    this->action_held_ = !toggle;
    ble_kbd_handle_key(function, toggle);
    return;
  }

  // Action layer held + a configured combo key pressed -> fire the ESPHome
  // trigger (once per press) and consume the key, overriding whatever Fn+<key>
  // would otherwise do in the firmware. A combo whose slot_mask excludes the
  // current BLE slot is skipped, so Fn+<key> falls through to the firmware
  // default on that slot (e.g. Fn+Left = Volume Down on non-allowed slots).
  if (this->action_held_ && !toggle) {
    uint8_t active_bit = (uint8_t) (1u << ble_kbd_current_slot());
    for (size_t i = 0; i < this->combos_.size(); i++) {
      const auto &c = this->combos_[i];
      if (c.function == function && (c.slot_mask & active_bit)) {
        this->fired_[function] = 1;
        uint8_t idx = (uint8_t) i;
        // Queue it; loop() fires the ESPHome trigger from the main-loop thread.
        xQueueSend(this->combo_queue_, &idx, 0);
        return;
      }
    }
  }

  // Everything else, including Fn + non-combo keys, goes to the firmware.
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
