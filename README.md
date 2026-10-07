<div align="center">

# ESPHome Xiaomi Light Bar nRF24

**ESPHome external component for the Xiaomi Mi Computer Monitor Light Bar (MJGJD01YL), with reliable state tracking**

[![ESPHome](https://img.shields.io/badge/ESPHome-2026.9.1-18BCF2?logo=esphome&logoColor=white)](https://esphome.io/components/external_components.html)
[![Boards](https://img.shields.io/badge/boards-ESP8266%20·%20ESP32-E7352C?logo=espressif&logoColor=white)](#requirements)
[![Radio](https://img.shields.io/badge/radio-nRF24L01-00A9CE?logo=nordicsemiconductor&logoColor=white)](#1-wiring)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-native%20API-41BDF5?logo=homeassistant&logoColor=white)](https://www.home-assistant.io/integrations/esphome/)
[![License](https://img.shields.io/badge/license-MIT-3DA639?logo=opensourceinitiative&logoColor=white)](LICENSE)

</div>

Control the **Xiaomi Mi Computer Monitor Light Bar MJGJD01YL** (the model with the 2.4 GHz wireless remote)
from Home Assistant with an ESP8266 or ESP32 and an nRF24L01 radio module, and keep using the original remote
alongside it.

> The **MJGJD02YL / Light Bar 1S** (Bluetooth, Mi Home app) uses a different protocol and is **not** supported.

## Why this component

The light bar is a one-way device: it receives commands, but it never reports what it is showing. Every
controller therefore has to *guess* the bar's state, and the guess drifts whenever a command is missed or the
original remote is used.

The common way around this is to set values absolutely: turn the brightness all the way down, then step up to
the target (the method documented by the protocol's reverse engineering). It works without knowing the current
value, but the bar visibly dips to its minimum on every change, and the remote is either ignored or not
reflected in Home Assistant.

This component is built around the state problem instead:

- **Relative steps.** Brightness and color temperature changes are sent as steps from the tracked value, the
  same way the remote's wheel does. The bar fades straight to the new value. The absolute method is used only
  when the current value is genuinely unknown, once.
- **The original remote is followed.** Presses, wheel turns, press-and-turn and hold update the light in Home
  Assistant, without sending anything back to the bar (no echo).
- **Recovery tools.** Calibration buttons, a configurable daily calibration and state restore after a reboot
  keep any remaining drift short-lived.

### Compared with lightbar2mqtt

This project descends from [ebinf/lightbar2mqtt](https://github.com/ebinf/lightbar2mqtt). Its state-tracking
logic was first developed as an unpublished rewrite of that firmware and then moved into an ESPHome component.

| | lightbar2mqtt 0.3 | This component |
|---|---|---|
| Integration | MQTT + discovery, settings in `config.h` | ESPHome native API, YAML configuration, OTA and logs from ESPHome |
| Brightness / temperature change | Absolute: dips to the minimum, then climbs | Relative steps; absolute only when the value is unknown |
| Rapid changes (sliders, scripts) | One radio burst per message | Merged within 150 ms, only the final value is sent |
| Original remote | Events published; light state not updated | Light state updated in Home Assistant, plus an `event` entity |
| Duplicate radio packets | Only back-to-back repeats are dropped reliably (the window check compares the counter against the serial) | Sequence window per serial; gaps are logged |
| Sending | Blocks the radio and the main loop for ~200 ms per command | Non-blocking; the radio listens between copies |
| After a controller reboot | Bar assumed on | Last state restored, nothing sent to the bar |
| Out-of-sync state | Toggle-state button | Calibration buttons and daily calibration |
| Boards | ESP32 (ESP8266 via a separate fork) | ESP8266 and ESP32 from the same component |
| nRF24 driver | [RF24](https://github.com/nRF24/RF24) library (GPL-2.0) | Own driver on ESPHome's `spi:` bus, no extra library |

## Features

- **Light entity**: on/off, brightness (the bar's 16 levels) and color temperature (2700–6500 K, 16 levels).
- **Pair button**: pairs the bar with any serial you choose.
- **Two ways to use the remote** ([details](#using-the-original-remote)):
  - *Same serial as the remote*: the remote controls the bar directly and the controller follows it.
  - *Own serial + relay*: the remote talks to the controller, which relays its commands, so Home Assistant
    always matches the bar.
- **Remote as an `event` entity**: `press`, `rotate_right`, `rotate_left`, `press_rotate_right`,
  `press_rotate_left`, `hold`, for automations. Works with any remote in range.
- **Calibration buttons**: *bar is on*, *bar is off* (correct the stored state without sending anything) and
  *resend brightness and temperature*.
- **Daily calibration**, configurable from Home Assistant (switch, time of day, quiet minutes) and stored on the
  device: once a day the bar is assumed off, postponed while the bar is in use.
- **Restore after reboot**: rebooting the controller never toggles the bar.
- **Several bars and remotes** per controller, each with its own serial.

## Requirements

- Xiaomi Mi Computer Monitor Light Bar **MJGJD01YL**.
- An **ESP8266** (e.g. NodeMCU, Wemos D1 mini) or **ESP32** board.
- An **nRF24L01 / nRF24L01+** module and a 10 µF capacitor.
- **ESPHome**: developed and tested with 2026.9.1. Releases older than 2026.7 lack APIs this component uses.
- **Home Assistant** with the ESPHome integration (the daily calibration also needs a `time:` source).

## Installation

Steps 1 to 5 are all you need to control the bar from Home Assistant.
[Using the original remote](#using-the-original-remote) is optional.

### 1. Wiring

Add the 10 µF capacitor between VCC and GND, close to the nRF24 module.

| nRF24 | ESP8266 | ESP32 |
|---|---|---|
| VCC | 3V3 | 3V3 |
| GND | GND | GND |
| SCK | D5 (GPIO14) | GPIO18 |
| MISO | D6 (GPIO12) | GPIO19 |
| MOSI | D7 (GPIO13) | GPIO23 |
| CSN | D1 (GPIO5) | GPIO5 |
| CE | D2 (GPIO4) | GPIO4 |

CSN and CE can be moved to other free GPIOs.

### 2. Configuration

Complete configurations for both boards, with every optional entity, are in [`example/`](example):
[`lightbar-esp8266.yaml`](example/lightbar-esp8266.yaml) (NodeMCU / Wemos D1 mini) and
[`lightbar-esp32.yaml`](example/lightbar-esp32.yaml) (ESP32 DevKit, ESP-IDF framework). The minimum is:

```yaml
external_components:
  - source: github://juancase/esphome-xiaomi-lightbar-nrf24
    components: [xiaomi_lightbar]

spi:
  clk_pin: GPIO14
  miso_pin: GPIO12
  mosi_pin: GPIO13

xiaomi_lightbar:
  cs_pin: GPIO5
  ce_pin: GPIO4

light:
  - platform: xiaomi_lightbar
    name: Light bar
    serial: 0x123456   # any 3-byte value; the bar is paired with it
    pair:
      name: Pair
```

Without a version, ESPHome uses the latest code on `main`. To stay on a release until you choose to update, add
its tag:

```yaml
  - source: github://juancase/esphome-xiaomi-lightbar-nrf24@v1.0.0
```

The files in `example/` load the component from this repository's `components/` folder, so they work as they
are from a clone. To use one from the ESPHome dashboard, copy it there and replace its `external_components:`
block with the one above.

The examples read their credentials from [`example/secrets.yaml`](example/secrets.yaml), which must stay in the
same folder as the YAML you flash:

| Key | Value |
|---|---|
| `wifi_ssid`, `wifi_password` | Your WiFi. |
| `api_key` | Encryption key for Home Assistant. Generate one with `python -c "import base64, secrets; print(base64.b64encode(secrets.token_bytes(32)).decode())"`. |
| `ota_password` | Any password, for updates over WiFi. |

### 3. Flash

Connect the board by USB and run, in the `example/` folder:

```
esphome run lightbar-esp8266.yaml
```

(or `lightbar-esp32.yaml`), then choose the serial port. Later updates can go over WiFi.

### 4. Add it to Home Assistant

Home Assistant usually finds the device by itself: wait for it to appear under *Settings → Devices & services*.
If it does not, choose *Add integration → ESPHome* and enter the IP address shown in the log (port 6053). Home
Assistant asks for the `api_key` from your secrets.

### 5. Pair

Unplug the bar, plug it in again and press **Pair** in Home Assistant within 10 seconds. The bar blinks when it
accepts the new serial.

**You can now control the bar from Home Assistant.** The original remote no longer controls the bar, because the
bar now obeys the controller's serial. To keep using the remote, read on.

## Using the original remote

Two serials are involved:

- **The controller's serial** (`serial:`, `0x123456` in the examples): you choose it.
- **The remote's serial** (`0xABCDEF` in the examples): you have to find it. Press a button on the remote and
  look for `Received ... from 0x......` in the log.

The bar obeys only one serial: the one it was paired with. Which one you choose gives two modes.

### A) The controller uses the remote's serial

```yaml
light:
  - platform: xiaomi_lightbar
    serial: 0xABCDEF   # your remote's serial
    # no "remotes:"
```

Flash and pair again (unplug the bar, plug it in, press **Pair** within 10 seconds), so the bar goes back to the
remote's serial. A new bar that was never paired with another serial does not need this.

- Home Assistant sends commands to the bar through the controller.
- The remote sends commands to the bar directly. The controller listens and updates Home Assistant.

**Pro:** the remote keeps working even when the controller is off or out of range (Home Assistant just won't
notice those changes).

**Con:** if the controller misses a packet from the remote (distance, interference), Home Assistant gets out of
step with the bar. Usually it's minor, one step of brightness. But if the missed packet was an on/off press,
Home Assistant shows the bar on when it is off, or the other way around, until you fix it with the
[calibration buttons](#light-platform-xiaomi_lightbar).

### B) The controller has its own serial and relays the remote (the examples)

```yaml
light:
  - platform: xiaomi_lightbar
    serial: 0x123456   # any value other than your remote's
    remotes:
      - 0xABCDEF       # your remote's serial
```

Pair the bar once (step 5).

- Home Assistant sends commands to the bar through the controller.
- The remote sends commands to the controller, which passes them on to the bar.
- The bar obeys only the controller, not the remote.

**Pro:** Home Assistant always matches the bar. Everything goes through the controller, so a remote press that
the controller misses never reaches the bar either: just press again.

**Con:** the remote only works while the controller is on and in range.

### Summary

| | A) Remote's serial | B) Own serial + `remotes:` |
|---|---|---|
| Remote works with the controller off or out of range | ✅ Yes | ❌ No |
| Home Assistant always matches the bar | ❌ No, a missed packet puts it out of step | ✅ Yes |
| Pairing | Only if the bar was paired with another serial | Once |

## Configuration reference

### Hub: `xiaomi_lightbar:`

| Key | Required | Description |
|---|---|---|
| `ce_pin` | yes | GPIO connected to the nRF24's CE. |
| `cs_pin` | yes | GPIO connected to the nRF24's CSN. |
| `spi_id` | no | The `spi:` bus to use, if there is more than one. |

### Light: `platform: xiaomi_lightbar`

All options of an ESPHome [light](https://esphome.io/components/light/), plus:

| Key | Required | Description |
|---|---|---|
| `serial` | yes | The 3-byte serial the bar obeys. |
| `remotes` | no | Remotes the bar is not paired with; their commands are relayed to the bar (mode B). |
| `pair` | no | Button *Pair*: pairs the bar with `serial` (see [step 5](#5-pair)). |
| `calibrate_on` | no | Button *Calibrate: bar is on*: tells the controller the bar is on. Sends nothing: use it when Home Assistant shows the bar off but it is on. |
| `calibrate_off` | no | Button *Calibrate: bar is off*: the same, for off. |
| `resend` | no | Button *Calibrate: resend brightness and temperature*: the bar dims to its minimum and goes up to the values shown in Home Assistant. |
| `auto_calibrate` | no | Daily calibration: once a day (by default at 04:00, when the monitor is usually off) the bar is assumed off, so a state that got out of step is corrected. It is postponed until nothing happened for the given minutes. Three entities, all changeable from Home Assistant: `enabled` (switch), `time` (default `04:00`) and `quiet_minutes` (default `60`). Needs a `time:` component. |

`restore_mode` defaults to `RESTORE_DEFAULT_OFF`. On the ESP8266, `esp8266: restore_from_flash: true` keeps the
state of the light and the calibration settings when the power is cut; without it they only survive a reboot.

### Remote: `event: platform: xiaomi_lightbar`

All options of an ESPHome [event](https://esphome.io/components/event/), plus `serial` (required): the remote's
serial. Every press or turn fires an event: `press`, `rotate_right`, `rotate_left`, `press_rotate_right`,
`press_rotate_left`, `hold`. Works with any remote, even one no bar listens to.

## How it works

```
Home Assistant ──native API──► ESPHome ──► light: xiaomi_lightbar ──► BarState ──► hub ──SPI──► nRF24L01 ))) bar
                                                ▲                                      │
                                                └──── remote commands (tracked / relayed) ◄─── ))) remote
```

- **`BarState`** keeps two sets of values: what Home Assistant requested and what the bar is believed to show.
  After a short coalescing window (150 ms) it sends only the difference, as relative steps.
- **No echo.** When the remote changes the bar, `BarState` is updated first and the new state is then published.
  The `write_state()` that ESPHome calls back finds nothing that differs, so nothing is sent.
- **After a reset** (pairing, or holding the remote button) the bar's levels are unknown. They are driven to
  full brightness and the warmest temperature with one relative command each, which works from any starting
  point and leaves both values known.
- **Radio.** Each command is sent 20 times, 10 ms apart, like the original remote. The copies are spread over
  `loop()` calls and the radio switches back to receive after each one. Steps that arrive while a command is
  waiting are merged into it.
- The protocol (`protocol.*`), the state engine (`bar_state.*`) and the daily calibration scheduler
  (`daily_calibration.*`) are plain C++ with no ESPHome dependency, and are covered by unit tests that run on a
  PC.

## Status

The component is functional and tested on hardware with an ESP8266 (NodeMCU, in daily use) and an ESP32
DevKit (ESP-IDF framework). The Arduino framework on the ESP32 has not been tested. Several bars on one
controller are supported by the configuration but have not been tested on hardware.

### Known limitations

- **No feedback channel.** Calibration and the daily calibration shorten the time the state can be wrong, but
  cannot rule it out.
- **Missed remote packets.** When the controller shares the remote's serial, a press it does not receive leaves
  Home Assistant out of step until it is calibrated. The relay mode avoids this. With very fast wheel turns, a
  share of the remote's packets is not received; this only costs wheel steps.
- **16 levels.** Brightness and color temperature have 16 levels each on the bar; values in between are
  rounded.

## Development

The hardware-independent core is tested on the PC with [PlatformIO](https://platformio.org/) and Unity:

```
pio test -e native
```

A host C++17 compiler is required. On Windows, run the tests in WSL; if the repository is on the Windows drive
(`/mnt/c/...`), set `PLATFORMIO_WORKSPACE_DIR` to a folder on the Linux file system, because PlatformIO cannot
install its packages into `.pio/` there.

To compile a firmware against the local checkout, use the configurations in [`example/`](example), which load
the component from `../components`.

## Credits

- [ebinf/lightbar2mqtt](https://github.com/ebinf/lightbar2mqtt) (MIT), by Erik Borowski: the project this one
  descends from. Packet building and the receive realignment are adapted from it; its copyright notice is kept in
  [LICENSE](LICENSE).
- [eugene-reim/lightbar2mqtt-esp8268](https://github.com/eugene-reim/lightbar2mqtt-esp8268) (MIT): the ESP8266
  port of lightbar2mqtt that the earlier rewrite started from.
- [lamperez/xiaomi-lightbar-nrf24](https://github.com/lamperez/xiaomi-lightbar-nrf24) (GPL-3.0): reverse
  engineering of the radio protocol. Only the documented protocol facts and four published sample packets (used
  as test vectors) are used here; none of its code.
- [benallen-dev/xiaomi-lightbar](https://github.com/benallen-dev/xiaomi-lightbar) (MIT): credited by
  lightbar2mqtt as an inspiration.
- [ESPHome](https://esphome.io/): the framework this component is built for.

## License

[MIT](LICENSE). The component's source is MIT-licensed. A firmware built with it also contains ESPHome's C++
runtime, which is licensed under GPLv3, so distributed firmware binaries are subject to the GPLv3 as well.
