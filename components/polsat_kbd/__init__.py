"""ESPHome external component: Polsat/Sejin IR keyboard -> BLE HID + combo actions.

Wraps the ESP-IDF firmware (NimBLE HID over esp_hid) as an ESPHome component and
lets key combinations fire ESPHome automations (mqtt.publish, homeassistant.*).
Requires the esp-idf framework and that ESPHome's own `esp32_ble` is NOT used —
this component owns the Bluetooth controller (one controller, one host).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@polsat-ble-keyboard"]

polsat_kbd_ns = cg.esphome_ns.namespace("polsat_kbd")
PolsatKbd = polsat_kbd_ns.class_("PolsatKbd", cg.Component)

CONF_IR_PIN = "ir_pin"
CONF_LED_PIN = "led_pin"
CONF_LED_ACTIVE_LOW = "led_active_low"
CONF_BOOT_PIN = "boot_pin"
CONF_DEVICE_NAME = "device_name"
CONF_ACTION_KEY = "action_key"
CONF_COMBOS = "combos"
CONF_FUNCTION = "function"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PolsatKbd),
        cv.Optional(CONF_IR_PIN, default=4): cv.int_range(min=0, max=48),
        # -1 disables the LED / BOOT button.
        cv.Optional(CONF_LED_PIN, default=8): cv.int_range(min=-1, max=48),
        cv.Optional(CONF_LED_ACTIVE_LOW, default=True): cv.boolean,
        cv.Optional(CONF_BOOT_PIN, default=9): cv.int_range(min=-1, max=48),
        cv.Optional(CONF_DEVICE_NAME, default="Polsat IR Kbd"): cv.All(
            cv.string, cv.Length(max=29)
        ),
        # The Sejin function code of the "action modifier" (held -> combos fire).
        # Defaults to Fn (0x60), shared with the firmware's Fn layer; the `fn=0x..`
        # value seen in the serial log.
        cv.Optional(CONF_ACTION_KEY, default=0x60): cv.hex_uint8_t,
        # Each combo: a `function` code + a `then:` automation. Fires while the
        # action_key is held and that key is pressed.
        cv.Optional(CONF_COMBOS): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                    automation.Trigger.template()
                ),
                cv.Required(CONF_FUNCTION): cv.hex_uint8_t,
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _configure_idf_build(device_name: str) -> None:
    """Pull the built-in ESP-IDF components our C sources include and switch the
    Bluetooth stack to NimBLE-only HID with our tuned settings."""
    from esphome.components import esp32

    for comp in (
        "bt",
        "nvs_flash",
        "esp_hid",
        "esp_timer",
        "esp_event",
        "esp_hw_support",
        "esp_driver_rmt",
        "esp_driver_gpio",
    ):
        try:
            esp32.include_builtin_idf_component(comp)
        except (ImportError, AttributeError):
            # Older ESPHome: these get pulled transitively once BT is enabled.
            pass

    opts = {
        "CONFIG_BT_ENABLED": True,
        "CONFIG_BT_NIMBLE_ENABLED": True,
        "CONFIG_BT_BLUEDROID_ENABLED": False,
        "CONFIG_BT_NIMBLE_HID_SERVICE": True,
        # esp_hidd registers our 3 input reports (keyboard/mouse/consumer); the
        # default NimBLE HID-service limits are too low and esp_hidd_dev_init then
        # fails with BLE_HS_EINVAL. Match the working standalone sdkconfig.
        "CONFIG_BT_NIMBLE_SVC_HID_MAX_INSTANCES": 2,
        "CONFIG_BT_NIMBLE_SVC_HID_MAX_RPTS": 3,
        "CONFIG_BT_NIMBLE_NVS_PERSIST": True,
        "CONFIG_BT_NIMBLE_MAX_BONDS": 8,
        "CONFIG_BT_NIMBLE_MAX_CCCDS": 16,
        # GAP service characteristics Apple reads after connecting.
        "CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME": device_name,
        "CONFIG_BT_NIMBLE_SVC_GAP_APPEARANCE": 961,  # 0x03C1 = Keyboard
        # The BLE stack does not fit the default factory app partition.
        "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE": True,
    }
    for key, value in opts.items():
        esp32.add_idf_sdkconfig_option(key, value)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_ir_pin(config[CONF_IR_PIN]))
    cg.add(var.set_led_pin(config[CONF_LED_PIN]))
    cg.add(var.set_led_active_low(config[CONF_LED_ACTIVE_LOW]))
    cg.add(var.set_boot_pin(config[CONF_BOOT_PIN]))
    cg.add(var.set_action_key(config[CONF_ACTION_KEY]))

    for conf in config.get(CONF_COMBOS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        await automation.build_automation(trigger, [], conf)
        cg.add(var.add_combo(conf[CONF_FUNCTION], trigger))

    _configure_idf_build(config[CONF_DEVICE_NAME])
