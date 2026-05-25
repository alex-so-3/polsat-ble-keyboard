#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

namespace esphome {
namespace polsat_kbd {

// ESPHome wrapper around the (verbatim-reused) ESP-IDF firmware: it pumps IR
// frames, decodes Sejin, and forwards key/mouse events to the NimBLE HID stack —
// except for configured "action combos", which fire an ESPHome Trigger (so YAML
// can run mqtt.publish / homeassistant.* etc.) instead of typing.
//
// A combo is "action_key held + <key> pressed". action_key defaults to the **Fn**
// key (0x60) and is forwarded to the firmware, so the firmware's own Fn layer
// (multi-device switch, media keys, numpad) keeps working on the SAME layer. A
// configured combo key is intercepted — it fires its trigger and is suppressed
// from the firmware, so it overrides whatever Fn+<key> would otherwise do. Pick
// combo keys the Fn layer doesn't already use. Function codes are the `fn=0x..`
// values printed in the serial log.
class PolsatKbd : public Component {
 public:
  void set_ir_pin(int pin) { this->ir_pin_ = pin; }
  void set_led_pin(int pin) { this->led_pin_ = pin; }
  void set_led_active_low(bool v) { this->led_active_low_ = v; }
  void set_boot_pin(int pin) { this->boot_pin_ = pin; }
  void set_action_key(int function) { this->action_key_ = function; }
  void add_combo(uint8_t function, Trigger<> *trigger) {
    this->combos_.emplace_back(function, trigger);
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  // Bring BLE up after WiFi so coexistence is initialised in a sane order.
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  // Route one decoded Sejin-1 key event: action_key / combo (-> trigger) / HID.
  void handle_key_(uint8_t function, bool toggle);

  int ir_pin_{4};
  int led_pin_{-1};
  bool led_active_low_{true};
  int boot_pin_{-1};
  int action_key_{0x60};  // Sejin function code of the action modifier (default Fn); -1 = none

  bool action_held_{false};
  uint8_t fired_[256] = {0};  // per-function latch so a held combo fires once
  std::vector<std::pair<uint8_t, Trigger<> *>> combos_;
};

}  // namespace polsat_kbd
}  // namespace esphome
