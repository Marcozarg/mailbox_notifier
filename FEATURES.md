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
- **Network:** WiFi `skynetIOT`, MQTT broker `192.168.15.100:1883` (Mosquitto, user `MQTT_User`).
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
  - WDT 8 s ticks for heartbeat scheduling.
- **60 s reed-event lockout:** after every reed-triggered TX, eight WDT ticks (~64 s) of WDT-only sleep before reattaching the reed interrupt — prevents bouncy lid double-fire. Also handles "lid stuck open" cleanly: interrupt only re-attaches when the pin reads HIGH (lid actually closed) AND the lockout has elapsed.
- **Heartbeat cadence:**
  - Normal: 24 h (10800 × 8 s WDT ticks).
  - Boost: 6 h (2700 ticks) when battery voltage drops below 3.6 V — early dead-battery warning.
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
id=AA&type=1&seq=42&t=21.43&h=67&p=1013.2&v=3920&r=1&sok=1&boot=12&up=1234&br=cold
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
| `br` | Boot reason (`cold` / `wdt` / `brownout` / `external`) — only in type=4 packets |

Forward-compat by design: receiver ignores unknown keys. Sender can add new ones without coordinated reflashes.

---

## 5. Receiver behaviour

### 5.1 Connectivity

- WiFi auto-reconnect.
- MQTT auto-reconnect with exponential backoff (5 s → 5 min cap), no `while(1)` halts.
- Last-Will-and-Testament: retained `mailbox/receiver/online = false` if disconnected ungracefully.
- ArduinoOTA (no password — trust the LAN). Hostname `arduinomailman`.
- NTP: `fi.pool.ntp.org` + `pool.ntp.org`, Europe/Helsinki TZ with DST. Active wait up to 10 s in setup.
- TX payload buffer raised to 1024 bytes (default 256 was silently dropping discovery JSONs).

### 5.2 Sticky `mailbox/state`

- **Sticky design.** Reed event published to `mailbox/state = "MAIL"` (retained, QoS 1) only on the EMPTY → MAIL transition. Subsequent reed events while state is already MAIL are ignored at the receiver.
- **Cleared by:** HA dashboard button (publishes `OFF` to `mailboxstatus/switch`, receiver subscribes), OR Heltec PRG long-press (≥ 1500 ms).
- **60 s repeat-MAIL guard** on top of the sender's 60 s lockout — defence in depth.
- **Dup-seq guard:** receiver rejects packets with same seq as the last one (except type=4 boot packets, which legitimately reset seq to 0).
- **Sender-alive watchdog:** if no packet for 50 h (24 h heartbeat × 2 + 2 h slack), publishes retained `mailbox/sender_alive = false`.

### 5.3 OLED layout

- Top row: `WiFi` / `MQTT` connection status + NTP-synced clock.
- Big middle: `MAIL` (when state on) or `—` (when empty).
- Bottom rows: latest packet sensor values + RSSI/SNR/seq.
- Auto-off after 10 min idle, woken by PRG short-press.

### 5.4 PRG button

- Short press (50 ms – 1499 ms): wake OLED.
- Long press (≥ 1500 ms): manually clear `mailbox/state`.

---

## 6. Home Assistant integration

### 6.1 MQTT discovery

Receiver publishes 17 retained discovery configs to `homeassistant/.../config` on every boot. HA dedups by `unique_id`. All entities grouped under one device card "Mailbox sensor."

| Entity | Type | Device class | Notes |
|---|---|---|---|
| `binary_sensor.mailbox_state` | binary | occupancy | `MAIL` / `EMPTY`, retained, sticky |
| `sensor.mailbox_temp` | sensor | temperature | °C, BME280 |
| `sensor.mailbox_humidity` | sensor | humidity | %, BME280 |
| `sensor.mailbox_pressure` | sensor | pressure | hPa, BME280 |
| `sensor.mailbox_battery_voltage` | sensor | voltage | V, diagnostic |
| `sensor.mailbox_battery_percent` | sensor | battery | %, computed from LiPo curve |
| `sensor.mailbox_msg_count` | sensor | — | seq counter, diagnostic |
| `sensor.mailbox_rssi` | sensor | signal_strength | dBm, diagnostic |
| `sensor.mailbox_snr` | sensor | — | dB, diagnostic |
| `sensor.mailbox_last_seen` | sensor | timestamp | ISO 8601 NTP timestamp, retained |
| `sensor.mailbox_last_packet_type` | sensor | — | 1/2/3/4, diagnostic |
| `sensor.mailbox_boot_count` | sensor | — | EEPROM-backed, diagnostic |
| `sensor.mailbox_boot_reason` | sensor | — | cold/wdt/brownout/external, diagnostic |
| `binary_sensor.mailbox_sender_alive` | binary | connectivity | timeout-driven, diagnostic |
| `binary_sensor.mailbox_receiver_online` | binary | connectivity | LWT-driven, diagnostic |
| `sensor.mailbox_receiver_wifi_rssi` | sensor | signal_strength | dBm, diagnostic |
| `sensor.mailbox_receiver_uptime` | sensor | duration | s, diagnostic |

Removed in V1.0.4 per user feedback: `binary_sensor.mailbox_lid` (lid status was useless without close-edge TX, which was rolled back).

### 6.2 Backwards-compat MQTT (V1.0.1+)

Receiver also publishes the V2_real-shaped topics so the original HA `configuration.yaml` `mqtt:` block keeps working:

- `mailboxstatus/switch` → `"ON"` on each reed-event state-transition (not retained).
- `mailboxstatus/feather` → JSON `{"temp", "humid", "lipo" (volts), "msgcount", "rssi", "snr"}` on every received packet.

To be removed in V3.1 once Marko has migrated dashboards / automations.

### 6.3 Lovelace dashboard

`Home Assistant configuration/HA mailbox-page V3.yaml` — six cards: tappable mailbox state button, conditional "mail arrived" banner, environment, power+connectivity, history graphs (24 h temp/humid, 7 d battery), diagnostics.

### 6.4 Notification flow (Node-RED → Pushover → iPhone)

- Subscribes to `mailbox/state`, fires on EMPTY→MAIL transition.
- Pushover sound: `magic` (chime, not siren).
- Priority 0 for mail; priority 1 + `falling` for sender/receiver-offline and low-battery alerts.
- Body includes live `temp` and `battery_percent`: e.g. `"Postia laatikossa! 7.2 °C, akku 78%"`.
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

Every code file starts with `// Vmajor.minor[.patch] — YYYY-MM-DD — description`. Runtime version strings (boot SLOG, OLED splash, MQTT discovery `sw_version`) derive from a single `#define FW_VERSION "Vx.y.z"` macro at the top of each file — version drift between the header and runtime can't happen.

---

## 10. Locked design decisions (history)

From `RECEIVER_V3_PLAN.md` §7 / §8 and `project_decisions.md` in agent memory:

- Sender MCU: keep Adafruit Feather 32u4 LoRa (not swapped to ESP32).
- LoRa library: RadioLib on receiver (via `heltec_unofficial`), Sandeep Mistry `LoRa.h` on sender.
- Packet format: key=value ASCII (chosen over CSV / binary struct).
- HA integration: MQTT discovery (chosen over hand-written sensor YAML).
- Heartbeat: 24 h normal / 6 h when low-batt.
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
| `arduino_secrets.h` | WiFi/MQTT credentials — never commit |
| `SENDER_HARDWARE.md` | Canonical sender pinout + wiring reference |
| `RECEIVER_V3_PLAN.md` | Design plan — packet format, MQTT topics, OLED layout, decisions log |
| `WORKFLOW_AND_TESTING.md` | Phased build/test plan |
| `Home Assistant configuration/HA mailbox-page V3.yaml` | Replacement Lovelace tab for V3 entities |
| `Node-Red_code.txt` | Existing Pushover flow (needs topic update to `mailbox/state`) |
| `Old Receiver sketches/` | Archived V2_real and earlier drafts |
| `compile errors/` | Saved error logs from past compile failures |
