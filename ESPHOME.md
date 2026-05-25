# polsat-esphome (experimental)

An **ESPHome** packaging of the Polsat/Sejin IR keyboard firmware. It keeps the
BLE HID keyboard+mouse behaviour of the main [`polsat-ble-keyboard`](../polsat-ble-keyboard)
project **and** lets **key combinations fire ESPHome automations** — e.g.
`mqtt.publish` or a Home Assistant service.

> Status: a separate, experimental variant. The ESP-IDF project is unchanged and
> remains the source of truth for the decoder. See **Caveats** — running WiFi and
> BLE together on a single-core ESP32-C3 is the main risk.

## How it works

The IR decode + keymap + NimBLE HID stack are **reused verbatim** from the main
project (the `*.c` files copied into `components/polsat_kbd/`). A thin ESPHome
C++ component (`polsat_kbd.{h,cpp}`) drives them:

- `setup()` brings up the LED, BOOT button, NimBLE/esp_hid HID device and IR
  (RMT) — i.e. the old `app_main` init.
- `loop()` non-blockingly drains decoded IR frames and dispatches them.
- decoded keys normally go to BLE HID; **configured combos** are intercepted and
  fire an ESPHome `Trigger` instead (so YAML actions run).

ESPHome provides WiFi + the automation engine; **this component owns the BT
controller** (so do **not** add ESPHome's `esp32_ble`/`ble_server`/tracker — one
controller, one host). The Python codegen (`__init__.py`) pulls the built-in
esp-idf components (`bt`, `esp_hid`, `nvs_flash`, RMT, …) and sets the NimBLE
sdkconfig (HID service, max bonds, GAP name/appearance — the Apple-TV fix).

## Combo → action model

A combo is **`action_key` held + a key pressed** (mirrors the firmware's Fn
layer). The `action_key` itself never types; a combo key is suppressed from HID
while it routes to its trigger. All keys are identified by their **Sejin function
code**, which you read straight from the serial log line `... fn=0x..`.

```yaml
polsat_kbd:
  action_key: 0x21        # RAlt — the "ESPHome modifier"
  combos:
    - function: 0x19      # L  ->  RAlt+L
      then:
        - mqtt.publish: { topic: polsat/kbd, payload: "lights" }
    - function: 0x53      # F1 ->  RAlt+F1
      then:
        - homeassistant.service: { service: light.toggle, data: { entity_id: light.living_room } }
```

See [`polsat.yaml`](./polsat.yaml) for a full example.

## Setup

```sh
pip install esphome
# create secrets.yaml next to polsat.yaml with: wifi_ssid, wifi_password,
# mqtt_broker, mqtt_username, mqtt_password
esphome compile polsat.yaml      # first build downloads ESP-IDF (slow)
esphome run polsat.yaml          # flash + logs
```

Pairing, the BOOT pairing-reset, the status LED, Fn layer (numpad/media/devices),
and trackball scroll all behave exactly as in the main project's README.

## Caveats / risks

1. **WiFi + BLE on single-core C3** — RAM pressure and RF coexistence. The BLE-HID
   stability fixed in the main project (supervision timeouts, Apple-TV conn-params)
   **must be re-verified under WiFi load**; coexistence airtime can reintroduce
   `disconnect; reason=520`. NimBLE (lighter than Bluedroid) helps.
2. **Owns the BT controller** — never enable ESPHome BLE components alongside this.
3. **esp-idf component pulling** — relies on `esp32.include_builtin_idf_component`
   / `add_idf_sdkconfig_option`; these are ESPHome-version-sensitive. If a build
   can't find `esp_hid`/NimBLE headers, that's the spot to fix.
4. **Event loop / NVS** — ESPHome already inits the default event loop and NVS;
   `ble_kbd_init()` re-init is expected to be a no-op, but watch the first boot log.
5. **`device_name`** sets the GAP-service name; the *advertised* name is still
   hardcoded as `"Polsat IR Kbd"` in `ble_kbd.c`.
6. **Duplicated decoder** — `sejin.c` / `hid_keymap.c` exist here and in the main
   project; keep them in sync.

## Lower-effort alternative

If the NimBLE-in-ESPHome integration proves too fragile, the community component
[markusg1234/ESPHome-espidf_ble_keyboard](https://github.com/markusg1234/ESPHome-espidf_ble_keyboard)
provides a Bluedroid BLE keyboard+mouse with multi-host; you would then only need
the IR-decoder half + the combo triggers.
