# Mailbox Notifier — Agreed Feature List

V1.0.0 — 2026-05-03 — Snapshot of everything decided through field-test of sender V1.0.5 / receiver V1.0.4. Field test result: RSSI ~ -58 dBm, SNR ~ +11 dB across 50 m + one tree.

This is the canonical "what the system does" reference. When in doubt about an existing behaviour, this list is correct; when designing a new behaviour, this list lists the constraints to respect.

---

## 1. Hardware

### 1.1 Sender (mailbox end)

- **MCU board:** Adafruit Feather 32u4 LoRa (ATmega32u4 + RFM95 SX1276 @ 868 MHz, 3.3 V logic).
- **Battery:** 2000 mAh 3.7 V LiPo (103450, dated 2024-07), via the Feather's JST connector. On-board MCP73831 charger.
- **Reed switch:** Normally Open (NO) type, paired with magnet. **Inverted mounting:** reed CLOSED only when lid is OPEN.
- **Environment sensor:** BME280 (6-pin GY-BME280 module), I2C address `0x76` (SDO→GND, CSB→3V3 = I2C mode).
- **Debug LED:** Blue LED on D5 (pin labelled "5" on silkscreen). Wire stays installed in the field, firmware-gated.
- **Antenna:** Wire-stub quarter-wave for 868 MHz (~8.2 cm).

### 1.2 Sender pin map (canonical: `SENDER_HARDWARE.md`)

| Code pin | Silkscreen | Function |
|---|---|---|
| D0 | RX | Reed switch (INT2, INPUT_PULLUP, no external resistor) |
| D2 | SDA | BME280 SDA |
| D3 | SCL | BME280 SCL |
| D4 | (internal) | LoRa RST |
| D5 | 5 | Blue debug LED (DEBUG_LED gated) |
| D7 | (internal) | LoRa DIO0 / IRQ |
| D8 | (internal) | LoRa CS |
| D9 / A9 | 9 / "BAT" trace | LiPo voltage divider (÷2) |
| D11 | 11 | (free — was DHT22 pre-V3) |
| D13 | 13 | On-board red LED (unused by us) |

### 1.3 Receiver (house end)

- **Board:** Heltec WiFi LoRa 32 V3 (HTIT-WB32LA V3.2 — ESP32-S3 + SX1262 + 0.96" OLED + on-board user button + battery measurement).
- **Power:** USB, always on, mounted on a window.
- **Network:** WiFi `skynetIOT`, MQTT broker `192.168.xxx.xxx:1883` (Mosquitto, user `MQTT_User`).
- **Antenna:** factory wire pigtail.

---

## 2. LoRa link parameters (must match on both ends)

| Parameter | Value |
|---|---|
| Frequency | 866.0 MHz (EU 868 ISM) |
| Bandwidth | 250 kHz |
| Spreading factor | 9 |
| TX power (sender) | +20 dBm |
| Hardware CRC | enabled |
| Coding rate | library default (4/5) |

---

## 3. Power management

### 3.1 Sender (battery-critical)

- **Sleep mode:** AVR `PWR_DOWN`, ADC off, BOD off, ~5 µA chip + ~0.1 µA radio.
- **Wake sources:**
  - Reed switch interrupt (INT2 LOW level) — lid-OPEN edge.
  - WDT ticks for heartbeat scheduling. One logical tick = 32 s (V1.0.9 sender), implemented by chaining 4× `LowPower.powerDown(SLEEP_8S, …)` since the 32u4's WDT prescaler caps at 8 s. The 4× chain bails out the moment a reed wake fires, so reed-event latency stays ≤ 8 s.
- **60 s reed-event lockout:** after every reed-triggered TX, two 32 s WDT ticks (~64 s) of WDT-only sleep before reattaching the reed interrupt — prevents bouncy lid double-fire. Also handles "lid stuck open" cleanly: interrupt only re-attaches when the pin reads HIGH (lid actually closed) AND the lockout has elapsed.
- **Heartbeat cadence:**
  - Normal: 48 h (5400 × 32 s WDT ticks).
  - Boost: 6 h (675 ticks) when battery voltage drops below 3.6 V — early dead-battery warning.
  - Reassessed on every heartbeat using fresh battery reading.
- **Sequence counter:** RAM-only, resets to 0 on every cold boot. Receiver sees a type=4 boot packet and adapts its dup-tracking.
- **EEPROM persistence:** boot count + reed event count, with magic byte for first-boot detection.
- **Estimated battery life:** ~150 days at 1 mail event/day (sleep current dominates; TX cost is ~30 mAs per packet).

### 3.2 Receiver

- USB-powered, no power management.
- Hardware watchdog: 30 s, `esp_task_wdt`, kicked from main loop.

---

## 4. Packet format

Key=value ASCII over LoRa. ~60–70 bytes typical. ~190 ms airtime at SF9/250 kHz.

```
id=AA&type=1&seq=42&t=21.43&h=67&p=1013.2&v=3920&r=1&sok=1&boot=12&up=1234&br=cold&fw=V1.0.9
```

| Key | Meaning |
|---|---|
| `id` | Device ID — always `AA` |
| `type` | `1` reed (mail arrived) / `2` heartbeat / `3` low-batt heartbeat / `4` boot |
| `seq` | Sequence counter (uint8 wraps; resets on boot) |
| `t` | Temperature °C |
| `h` | Humidity % |
| `p` | Pressure hPa |
| `v` | Battery voltage mV |
| `r` | Reed state at TX time (0 = lid closed, 1 = lid open) |
| `sok` | BME280 read OK flag (1 = fresh values, 0 = sensor failed, values are last-known cache) |
| `boot` | EEPROM-backed boot count |
| `up` | Uptime in minutes since this boot |
| `br` | Boot reason (`power-on` / `external reset` / `watchdog` / `brown-out` / `normal`) — in EVERY packet from sender V1.1.0+ (was type=4-only pre-V1.1.0; the `normal` default replaces the unhelpful `unknown` since the 32u4 Caterina bootloader usually clears MCUSR before we can read it). Receiver publishes the value verbatim to `mailbox/sender/boot_reason`. |
| `fw` | Sender firmware version string (V1.0.9+) — receiver publishes to `mailbox/sender/version` (was `mailbox/sender_fw` pre-V1.1.0) |

Forward-compat by design: receiver ignores unknown keys. Sender can add new ones without coordinated reflashes.

---

## 5. Receiver behaviour

### 5.1 Connectivity

- WiFi auto-reconnect.
- MQTT auto-reconnect with exponential backoff (5 s → 5 min cap), no `while(1)` halts.
- Last-Will-and-Testament: retained `mailbox/receiver/online = false` if disconnected ungracefully.
- ArduinoOTA (no password — trust the LAN). Hostname `arduinomailman`. Arduino IDE prompts for a password on every network upload — **leave the field blank and press OK**, the receiver ignores whatever is typed. V1.1.1+ keeps the 30 s task-watchdog kicked from the OTA progress callback so multi-second flash erases don't trip it (manifested as `WinError 10054` mid-upload pre-V1.1.1), and the OLED shows live `OTA xx%`.
- NTP: `fi.pool.ntp.org` + `pool.ntp.org`, Europe/Helsinki TZ with DST. Active wait up to 10 s in setup.
- TX payload buffer raised to 1024 bytes (default 256 was silently dropping discovery JSONs).

### 5.2 Sticky `mailbox/state`

- **Sticky design.** Reed event published to `mailbox/state = "MAIL"` (retained, QoS 1) only on the EMPTY → MAIL transition. Subsequent reed events while state is already MAIL are ignored at the receiver.
- **Cleared by:** HA dashboard button (publishes `EMPTY` retained to `mailbox/state`; receiver subscribes and adopts via the V1.0.6 Fix B path), OR Heltec PRG long-press (≥ 1500 ms). The legacy `mailboxstatus/switch` route was removed in V1.1.0.
- **60 s repeat-MAIL guard** on top of the sender's 60 s lockout — defence in depth.
- **Dup-seq guard:** receiver rejects packets with same seq as the last one (except type=4 boot packets, which legitimately reset seq to 0).
- **Sender-alive watchdog:** if no packet for 98 h (48 h heartbeat × 2 + 2 h slack), publishes retained `mailbox/sender/alive = false` (was `mailbox/sender_alive` pre-V1.1.0).

### 5.3 OLED layout

- Top row: `WiFi` / `MQTT` connection status + NTP-synced clock on the left, receiver `FW_VERSION` right-justified (V1.0.8+).
- Big middle: `MAIL` (when state on) or `—` (when empty).
- Bottom rows: latest packet sensor values + RSSI/SNR/seq.
- Auto-off after 10 min idle, woken by PRG short-press.
- Boot splash: `Mailbox RX` / `by Marko` / `Booting...` (version moved off boot splash in V1.0.8 since it's now always visible on the main screen).

### 5.4 PRG button

- Short press (50 ms – 1499 ms): wake OLED.
- Long press (≥ 1500 ms): manually clear `mailbox/state`.

---

## 6. Home Assistant integration

### 6.1 MQTT discovery

Receiver publishes 18 retained discovery configs to `homeassistant/.../config` on every boot. HA dedups by `unique_id`. All entities grouped under one device card "Mailbox" (V1.1.0+; was "Mailbox sensor" pre-V1.1.0).

Naming scheme (V1.2.0+): every entity_id follows the pattern **`<platform>.mailbox_<side>_<thing>`** where `<side>` is `sender` (data the Feather produces) or `receiver` (data the receiver measures). The discovery payload itself sends only the **un-prefixed `<side>_<thing>`** part — HA composes the `mailbox_` prefix automatically from the device slug (V1.1.0/V1.1.1 sent the doubled "mailbox_mailbox_*" because the prefix was redundantly included in both fields). The headline `mailbox_state` represents the device as a whole.

| Entity | Type | Device class | Notes |
|---|---|---|---|
| `binary_sensor.mailbox_state` | binary | occupancy | `MAIL` / `EMPTY`, retained, sticky |
| `sensor.mailbox_sender_temperature` | sensor | temperature | °C, BME280 |
| `sensor.mailbox_sender_humidity` | sensor | humidity | %, BME280 |
| `sensor.mailbox_sender_pressure` | sensor | pressure | hPa, BME280 |
| `sensor.mailbox_sender_battery_voltage` | sensor | voltage | V, diagnostic |
| `sensor.mailbox_sender_battery` | sensor | battery | %, computed from LiPo curve |
| `sensor.mailbox_sender_packet_seq` | sensor | — | seq counter, diagnostic |
| `sensor.mailbox_sender_last_packet_type` | sensor | — | mail / heartbeat / heartbeat (low batt) / boot (V1.2.1+ — was numeric 1/2/3/4 pre-V1.2.1), diagnostic |
| `sensor.mailbox_sender_boot_count` | sensor | — | EEPROM-backed, diagnostic |
| `sensor.mailbox_sender_boot_reason` | sensor | — | power-on / external reset / watchdog / brown-out / normal (V1.1.0+ sender — was cold/wdt/brownout/external/unknown pre-V1.1.0; also now published on every packet so HA never shows "Unknown"), diagnostic |
| `binary_sensor.mailbox_sender_alive` | binary | connectivity | timeout-driven, diagnostic |
| `sensor.mailbox_sender_version` | sensor | — | sender FW string, diagnostic (V1.0.8+) |
| `sensor.mailbox_receiver_rssi` | sensor | signal_strength | dBm of last RX packet, diagnostic |
| `sensor.mailbox_receiver_snr` | sensor | — | dB of last RX packet, diagnostic |
| `sensor.mailbox_receiver_last_seen` | sensor | timestamp | ISO 8601 NTP timestamp, retained |
| `binary_sensor.mailbox_receiver_online` | binary | connectivity | LWT-driven, diagnostic |
| `sensor.mailbox_receiver_wifi_rssi` | sensor | signal_strength | dBm, diagnostic |
| `sensor.mailbox_receiver_uptime` | sensor | duration | d (days, 2 decimals), diagnostic |

Removed in V1.0.4: `binary_sensor.mailbox_lid` (close-edge TX rolled back per user feedback).
Renamed in V1.1.0: every `sensor.mailbox_*` entity except the headline `mailbox_state` (sender_/receiver_ scheme introduced).
Re-slugged in V1.2.0: discovery payload no longer includes the "mailbox_" prefix — HA prepends it via device slug. Net entity_id is the same as the V1.1.0 *intent*; the V1.1.0/V1.1.1 firmware accidentally produced `sensor.mailbox_mailbox_*` for fresh entities created under modern HA. V1.2.0 fixes the discovery so newly-created entities come out clean. Existing doubled-name entities go "Unavailable" — one bulk delete in **Settings → Devices & services → MQTT → entities** (filter "unavailable", search "mailbox_mailbox") clears them.

### 6.2 MQTT topic tree

V1.1.0 restructure — all topics now namespaced. Pre-V1.1.0 flat topics (`mailbox/temp`, `mailbox/rssi`, `mailbox/sender_fw`, etc.) and the legacy `mailboxstatus/*` compat tree are **gone** (no aliases kept).

- **Headline:** `mailbox/state` — `"MAIL"` / `"EMPTY"`, retained, bidirectional. Receiver publishes on reed events + PRG long-press. HA dashboard may publish `"EMPTY"` (retained) directly to clear.
- **Sender-derived (RX → HA):** `mailbox/sender/temperature`, `mailbox/sender/humidity`, `mailbox/sender/pressure`, `mailbox/sender/battery_voltage`, `mailbox/sender/battery_percent`, `mailbox/sender/packet_seq`, `mailbox/sender/last_packet_type`, `mailbox/sender/boot_count`, `mailbox/sender/boot_reason`, `mailbox/sender/sensor_ok` (side channel, no HA entity), `mailbox/sender/alive`, `mailbox/sender/version`.
- **Receiver-measured (RX → HA):** `mailbox/receiver/rssi`, `mailbox/receiver/snr`, `mailbox/receiver/last_seen`, `mailbox/receiver/online`, `mailbox/receiver/wifi_rssi`, `mailbox/receiver/uptime`.

### 6.3 Lovelace dashboard

`Home Assistant configuration/HA mailbox-page V3.yaml` — six cards: tappable mailbox state button, conditional "mail arrived" banner, environment, power+connectivity, history graphs (24 h temp/humid, 7 d battery), diagnostics.

### 6.4 Notification flow (Node-RED → Pushover → iPhone)

- Subscribes to `mailbox/state`, fires on EMPTY→MAIL transition.
- Pushover sound: `magic` (chime, not siren).
- Priority 0 for mail; priority 1 + `falling` for sender/receiver-offline and low-battery alerts.
- Body subscribes to `mailbox/sender/temperature` and `mailbox/sender/battery_percent` (V1.1.0+ topic paths) for live values: e.g. `"Postia laatikossa! 7.2 °C, akku 78%"`. **Pre-V1.1.0 flows** subscribing to `mailbox/temp` and `mailbox/battery_percent` need updating.
- Trigger node 60 s block as third dedup layer.
- No auto-clear — manual via dashboard or PRG long-press.

---

## 7. Three-layer reed dedup

Defence in depth — same 60 s window applied at each stage:

1. **Sender lockout** — 60 s of reed-interrupt-detached WDT-only sleep after each reed-triggered TX. Battery-friendly because the chip stays asleep through the lockout.
2. **Receiver guards** — duplicate-seq rejection + repeat-MAIL within 60 s rejection.
3. **Node-RED Trigger node** — 60 s block on the Pushover output. Adjustable from the UI without reflashing firmware.

---

## 8. Build-time toggles

### 8.1 Sender

| Macro | Production | Bench debug | Effect |
|---|---|---|---|
| `DEBUG_NOSLEEP` | 0 | 1 | Bench mode keeps chip awake forever, prints sensor values once a second in Serial Plotter format, 5 s reed lockout. |
| `DEBUG_HEARTBEAT_30S` | 0 | optional | Sub-toggle inside DEBUG_NOSLEEP. 1 = also send heartbeat every 30 s. 0 = reed-only TX. |
| `ENABLE_SERIAL` | 0 | 1 | Compiles Serial debug in/out. 0 saves a few mA from the USB block. |
| `DEBUG_LED` | 0 | 1 | When 1, blue LED on D5 blinks ~100 ms after every TX. When 0, pin is left tristate — wire stays installed. |
| `BOOT_UPLOAD_WINDOW_MS` | 10000 | n/a | (V1.0.5) hold USB-CDC alive at end of setup() so future uploads work via auto-reset. Field-mode only. |

### 8.2 Receiver

| Macro | Default | Effect |
|---|---|---|
| `ENABLE_SERIAL` | 1 | Receiver is USB-powered, Serial diagnostics always useful. |

---

## 9. Versioning rules (project-wide)

| Bump | Triggers |
|---|---|
| `Vx.y.z` (patch) | Bug fix, comment, typo, cosmetic-only edit. |
| `Vx.y` (minor) | Feature change, behaviour tweak, UX polish. |
| `Vx` (major) | Hardware change, MCU swap, packet format break. |

Every code file starts with `// Vmajor.minor[.patch] — YYYY-MM-DD — description`. Runtime version strings (boot SLOG, OLED main-screen top-right corner, MQTT discovery `sw_version`) derive from a single `#define FW_VERSION "Vx.y.z"` macro at the top of each file — version drift between the header and runtime can't happen. (Pre-V1.0.8 the version was on the boot splash instead of the main screen.)

---

## 10. Locked design decisions (history)

From `RECEIVER_V3_PLAN.md` §7 / §8 and `project_decisions.md` in agent memory:

- Sender MCU: keep Adafruit Feather 32u4 LoRa (not swapped to ESP32).
- LoRa library: RadioLib on receiver (via `heltec_unofficial`), Sandeep Mistry `LoRa.h` on sender.
- Packet format: key=value ASCII (chosen over CSV / binary struct).
- HA integration: MQTT discovery (chosen over hand-written sensor YAML).
- Heartbeat: 48 h normal / 6 h when low-batt (was 24 h normal until 2026-05-07).
- Tier 3 features in V3: OTA only. Buzzer / AES / web page / HA-reboot deferred.
- BME280: 6-pin module, CSB→3V, SDO→GND → address 0x76.
- Mail-state auto-clear: none — manual only via HA button or PRG long-press.
- Build order: receiver first, then sender.

---

## 11. Files in this project (reference)

| File | Purpose |
|---|---|
| `mailbox_receiver_V3.ino` | Receiver firmware (Heltec V3) |
| `mailbox_sender_V3.ino` | Sender firmware (Feather 32u4 LoRa) |
| `SENDER_HARDWARE.md` | Canonical sender pinout + wiring reference |
| `RECEIVER_V3_PLAN.md` | Design plan — packet format, MQTT topics, OLED layout, decisions log |
| `WORKFLOW_AND_TESTING.md` | Phased build/test plan |

