# polsat-ble-keyboard (ESP32-C3)

The **Sejin SWK-8695WS** is an early-2000s infrared wireless keyboard with a
built-in trackball, made by Se Jin Electron (Korea). It was white-labeled under
many brands and bundled with all sorts of hardware — Escient media servers,
EarthWalk classroom systems, even Compaq PCs — but in Poland it is best
remembered as the keyboard **Polsat** shipped with its e-mail-capable satellite
receivers, back in the brief era when a set-top box doubled as a living-room
internet terminal. The original IR receivers are long gone; this project builds a
new one.

This is ESP-IDF firmware that turns that keyboard into a **Bluetooth LE HID
keyboard and mouse**. It captures the IR signal over the RMT peripheral, decodes
the Sejin-1-38 / Sejin-2-38 frames, and re-transmits the key presses and
trackball (pointer, buttons, scroll) to a paired host over BLE.

The IR decoding (`sejin.c`), the function→key map (`hid_keymap.c`), and the
6-key-slot press/release logic (`ble_kbd.c`) are ported from the RP2040
[`polsat-ir-receiver`](https://github.com/alufers/polsat-ir-receiver) project, so the capture layer
(`ir_rmt.c`) deliberately produces the exact same signed-microsecond pulse format
the decoder expects.

The BLE side uses the **NimBLE** host (smaller than Bluedroid for a BLE-only
device) together with the `esp_hid` device API. `esp_hid_gap.c` is the GAP/pairing
helper taken from the esp-idf `esp_hid_device` example.

Decoded events are still printed to the serial console, so `idf.py monitor`
doubles as a live key logger.

> The trackball is a full BLE mouse: pointer movement, left/right/middle buttons,
> and **Fn + roll to scroll** (see [Trackball scroll](#trackball-scroll)).

## Hardware

- ESP32-C3 board. Defaults assume an **ESP32-C3 SuperMini**: on-board LED on
  GPIO8 (active-low), BOOT button on GPIO9.
- A 38 kHz IR receiver module (e.g. TSOP2238), active-low output.
  - VCC -> 3V3
  - GND -> GND
  - OUT -> **GPIO4 (IO4)**

All pins are configurable in `main/main.c`: `IR_GPIO` (IR receiver),
`LED_GPIO` / `LED_ACTIVE_LOW` (status LED), `BOOT_BTN_GPIO` (pairing button).

## Build / flash / monitor

```sh
. ~/clones/esp-idf/export.sh        # source the ESP-IDF environment
idf.py set-target esp32c3           # first time only
idf.py build
idf.py -p <PORT> flash monitor
```

## Pairing

After flashing, the device advertises as **"Polsat IR Kbd"**. On your host
(phone, computer, TV, …) open the Bluetooth settings, pick that device, and
confirm pairing.
Pairing is "Just Works" (no passkey to type) because the board has no
display/keypad; the link is still encrypted and bonded. Bonding keys are stored
in NVS, so it reconnects automatically afterwards.

> If you re-flash after changing the BLE/security settings, **forget the device
> on the host first** — a stale bond from a previous build will block re-pairing.

### Re-pairing with the BOOT button

To pair a different host (or recover from a stale bond) **hold the BOOT button for
~1.5 s**. The device then clears its stored bonds, drops the current host, and
advertises fresh so a new host can pair. You still need to *forget* it on the old
host. The pin is `BOOT_BTN_GPIO` (GPIO9) in `main/main.c`.

### Status LED

A single on-board LED (`LED_GPIO`, GPIO8, active-low — the ESP32-C3 SuperMini
default) shows state:

- **on for ~1 s at power-up** — firmware started,
- **blinking** — pairing mode (waiting for a host); stops once a host pairs,
- **brief flash** — a valid IR frame was just decoded,
- **off** — idle.

Change `LED_GPIO` / `LED_ACTIVE_LOW` in `main/main.c` for a different board.

> Switching device (Fn+F1..F4, below) also blinks the LED until the slot's host
> connects — an empty slot simply waits for a new host to pair.

## Fn layer

The **Fn** key (Sejin function `0x60`) is a layer modifier: held on its own it
sends nothing, but it remaps a few keys.

### Numeric keypad

Hold **Fn** and use the laptop-style embedded keypad, plus the operator keys
next to it:

```
 7 8 9      ->  7 8 9        0  ->  *      ; ->  +      / ->  /
 u i o      ->  4 5 6        P  ->  -      =  ->  =      . ->  ,
 j k l      ->  1 2 3
   m        ->    0
```

The digits send true HID *keypad* usages, so they only appear while the host's
**Num Lock is on** (there is a dedicated **Num Lock** key on the base layer). The
operators `* / - +` are keypad usages; `=` and `,` are the regular keys (the
dedicated keypad `=`/`,` usages proved unreliable on the host).

### Media keys

Hold **Fn** and use the arrow keys:

| Fn + | Action |
| ---- | ------ |
| ← / → | Volume down / up (held repeats the step) |
| ↑ / ↓ | Next / previous track (one step per press) |
| Enter | Play / pause |

### Multiple devices

Hold **Fn + F1/F2/F3/F4** to switch between up to **4 paired hosts**. Each slot
advertises under its own BLE identity, so every host keeps an independent bond;
switching disconnects the current host and (re)connects/pairs the chosen slot
(the LED blinks until it connects). The active slot is saved in NVS and restored
on reboot. Holding BOOT (pairing reset) clears the bonds of *all* slots.

> Because each slot now uses a static-random address, a host paired by an older
> (public-address) build must be re-paired after flashing this version.

## Host-specific keys

A few base keys target a **macOS** host (the main test target):

- **Back / Forward** send **Cmd+[ / Cmd+]** (browser navigation) — macOS ignores
  the usual Consumer *AC Back/Forward* usages.
- The **§** key uses the ISO *Non-US `\`* usage, so it only produces `§` with an
  ISO keyboard layout selected on the host.

These live in `hid_keymap.c`; change them there for a different host OS/layout.

## Trackball scroll

**Hold Fn** and roll the trackball to scroll instead of moving the pointer.
Movement snaps to a single axis — vertical (wheel) or horizontal (AC Pan),
whichever you move first — so it never scrolls diagonally; pause briefly to
re-pick the axis. Release Fn to return to pointer mode.

> This added a horizontal-scroll field to the mouse HID descriptor, so a host
> paired by an earlier build must be re-paired (or forget/re-add the device).

## Logs & troubleshooting

Press keys on the IR keyboard; they are typed into the host, and the same events
are logged on the serial console, e.g.:

```
I (1234) polsat: DOWN  fn=0x12  A            [keycode  0x04]
I (1300) polsat: UP    fn=0x12  A            [keycode  0x04]
I (1450) polsat: DOWN  fn=0x07  LShift       [modifier 0x02]
I (1700) ble_kbd: link encrypted, ready to send key reports
```

(Trackball movement is logged only at `DEBUG` level, so it does not appear at the
default `INFO` log level.)

If keys are decoded (you see the `DOWN`/`UP` lines) but nothing reaches the host,
check that `ble_kbd: link encrypted, ready to send key reports` appeared — that is
the signal that reports are being delivered.

If nothing is decoded at all, set `IR_DUMP_RAW` to `1` in `main/main.c` to print
the raw captured pulse list and verify the receiver wiring/polarity.

## How the capture maps to the decoder

`ir_rmt.c` runs the RMT RX channel at 1 MHz (1 tick = 1 µs) and converts each
`rmt_symbol_word_t` into the decoder's `int32_t durations[]` list:

- LOW level (carrier present, the receiver pulls its output low) -> **mark**,
  stored positive.
- HIGH level (idle) -> **space**, stored negative.

A receive ends when the line stays idle longer than `IR_FRAME_GAP_US` (2200 µs).
