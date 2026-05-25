# polsat-ble-keyboard — ESPHome variant (`esphome` branch)

This branch packages the Polsat/Sejin SWK-8695WS IR keyboard as an **ESPHome
external component** (`components/polsat_kbd`). It turns the IR keyboard into a
**Bluetooth LE HID keyboard + mouse** and adds **key combinations that fire
ESPHome automations** (e.g. `mqtt.publish`, Home Assistant services).

> The standalone ESP-IDF firmware — and the full reference for the keyboard
> features (Fn layer, numpad, media keys, multi-device switching, trackball
> scroll, the Polsat history) — lives on the
> [**`main`**](https://github.com/alex-so-3/polsat-ble-keyboard/tree/main) branch.
> This branch is only the ESPHome packaging; it reuses a snapshot of that code.

## Use it from Home Assistant / ESPHome

Pull the component from this branch in your ESPHome device YAML:

```yaml
external_components:
  - source: github://alex-so-3/polsat-ble-keyboard@esphome
    components: [polsat_kbd]
    refresh: 0s   # always re-fetch on build; omit/raise to cache (default 1 day)
```

See [`esphome-example.yaml`](./esphome-example.yaml) for a complete device config
(WiFi + API + OTA + MQTT + the `polsat_kbd` block with combos).

> Use the **`esp-idf`** framework, and do **not** add ESPHome's `esp32_ble` /
> `ble_server` / `esp32_ble_tracker` — this component owns the Bluetooth
> controller (one controller, one host). WiFi and BLE run together on the
> single-core ESP32-C3.

## Configuration

```yaml
polsat_kbd:
  ir_pin: 4            # IR receiver (TSOP) output GPIO
  led_pin: 8           # status LED GPIO (-1 disables)
  led_active_low: true # LED lit when the pin is LOW (SuperMini default)
  boot_pin: 9          # hold ~1.5 s = pairing reset (-1 disables)
  device_name: "Polsat IR Kbd"
  action_key: 0x60     # Fn (default) — shared with the firmware Fn layer
  combos:
    - function: 0x12   # Fn + A -> run `then` (and don't type A)
      then:
        - mqtt.publish: { topic: polsat/kbd, payload: "lights" }
```

| Option | Default | Meaning |
| --- | --- | --- |
| `ir_pin` | `4` | IR receiver (TSOP) output GPIO |
| `led_pin` | `8` | status LED GPIO (`-1` to disable) |
| `led_active_low` | `true` | LED is lit when the pin is LOW |
| `boot_pin` | `9` | pairing-reset button GPIO (`-1` to disable) |
| `device_name` | `"Polsat IR Kbd"` | BLE name |
| `action_key` | `0x60` (Fn) | Sejin function code of the action modifier |
| `combos` | — | list of `function` (Sejin code) + a `then:` automation |

### Combo → ESPHome action (on the Fn layer)

`action_key` defaults to **Fn (0x60)** and is **shared with the firmware's Fn
layer** — Fn keeps doing multi-device switching (Fn+F1..F4), media keys
(Fn+arrows / Enter) and the numpad, and the configured combos add ESPHome actions
on the **same layer**. A combo is **Fn held + a key pressed**: that key fires its
`then:` automation and is suppressed from typing, **overriding** whatever Fn+<key>
would otherwise do — so choose combo keys the Fn layer doesn't already use (e.g.
plain letters). Identify keys by the **`fn=0x..` code in the serial log**.

Pairing, the BOOT pairing-reset, the status LED, the full Fn layer and trackball
scroll behave exactly as documented on the `main` branch.

## Caveats

- **WiFi + BLE coexistence** on the single-core C3 is the main runtime risk; watch
  the logs for `disconnect; reason=520`.
- The component bundles a **snapshot** of the decoder / NimBLE HID code from
  `main`; keep them in sync if `main` changes.
- Flashing replaces any prior firmware and **clears BLE bonds** — hosts must be
  re-paired afterwards.

## Build standalone (without Home Assistant)

```sh
pip install esphome
# create secrets.yaml (wifi_ssid, wifi_password, mqtt_broker, mqtt_username, mqtt_password)
esphome compile esphome-example.yaml
esphome run esphome-example.yaml
```
