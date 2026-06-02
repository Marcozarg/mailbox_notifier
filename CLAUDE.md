# Mailbox Notifier — Claude context

This file is read by Claude Code on every session. Keep it up to date when
decisions change. The canonical behaviour reference is `FEATURES.md`.

---

## Project in one paragraph

Detects mail arriving in a physical mailbox (~50 m from the house) and sends
an iPhone notification via Pushover. A battery-powered Adafruit Feather 32u4
LoRa sender in the mailbox sends key=value LoRa packets on lid-open (reed
switch) and on a 48 h heartbeat. A Heltec WiFi LoRa 32 V3 receiver inside the
house relays packets to Home Assistant via MQTT. Node-RED bridges MQTT to
Pushover. The system is **deployed and live** — treat every change as an
iteration on a running system, not greenfield work.

```
Feather 32u4 ── LoRa 866 MHz SF9 BW250 ──► Heltec V3 ── WiFi/MQTT ──► HA ──► Node-RED ──► Pushover ──► iPhone
```

---

## Current versions

| Component | Version | File |
|---|---|---|
| Sender | V1.1.0 | `firmware/mailbox_sender_V3/mailbox_sender_V3.ino` |
| Receiver | V1.2.6 | `firmware/mailbox_receiver_V3/mailbox_receiver_V3.ino` |

---

## Workflow rules — read these first

- **Plan before coding.** When asked to change something, explain the approach
  and ask for confirmation before writing any code. Surface options +
  recommendation. Only write code once Marko says to proceed.
- **Sender flashing requires a physical trip** to the mailbox with a USB cable.
  Flag this explicitly whenever a sender change is proposed.
- **Receiver is OTA-flashable** from inside the house (see README §8). No
  password — trust the LAN.
- **`FEATURES.md` is the canonical behaviour reference.** If a request
  contradicts it, say so before proceeding.
- **`docs/SENDER_HARDWARE.md` is the single source of truth for sender
  pinout.** Never guess pin assignments.

---

## Coding conventions (both files)

### File header
Every `.ino` starts with a version history block, newest version first:
```cpp
// V1.2.4 — 2026-05-25 — Short description of this version
//
// V1.2.4 changes:
//   • ...
//
// V1.2.3 — 2026-05-22 — ...
```

### Version macro
One `#define FW_VERSION "V1.2.4"` near the top is the **single source of
truth**. The boot Serial log, OLED display, and MQTT discovery `sw_version`
all derive from it. Never hardcode the version string anywhere else.

### Versioning rules
| Bump | Triggers |
|---|---|
| `Vx.y.z` patch | Bug fix, comment, typo, cosmetic only |
| `Vx.y` minor | Feature, behaviour tweak, UX change |
| `Vx` major | Hardware swap, MCU change, packet format break |

### Section banners
```cpp
////////////////////////////////////////////////////////////////////////////////
// Section name
////////////////////////////////////////////////////////////////////////////////
```

### Comments
- Add a `// why` comment on every non-obvious decision.
- Reference the version that introduced a behaviour where helpful
  (e.g. `// V1.0.6 fix A:`).
- Forward-declarations go near the top of the file so helpers can be defined
  after `setup()` / `loop()` without prototype surprises.

---

## Architecture decisions (locked — ask before changing)

| Decision | Choice |
|---|---|
| Sender MCU | Adafruit Feather 32u4 LoRa (ATmega32u4 + RFM95 SX1276 @ 868 MHz). Keep — already wired, has LiPo charger. |
| Receiver MCU | Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 + OLED). |
| Receiver LoRa library | `heltec_unofficial.h` (wraps RadioLib for SX1262). |
| Packet format | Key=value ASCII: `id=AA&type=1&seq=42&t=21.4&h=67&p=1013&v=3920&r=1&sok=1&boot=12&up=1234&br=normal&fw=V1.1.0` |
| Heartbeat cadence | 48 h normal / 6 h when vbat < 3.6 V (low-batt urgency mode). |
| Sender-alive timeout | 98 h = (48 h × 2) + 2 h slack — tolerates one missed heartbeat. |
| Sticky mail state | Receiver publishes retained `mailbox/state = MAIL`. HA dashboard button publishes `EMPTY` (retained) directly to `mailbox/state` to clear. No auto-clear. |
| MQTT cleanSession | ArduinoMqttClient defaults to `cleanSession=true`. Broker discards subscriptions on disconnect. `connectMqtt()` therefore re-subscribes to `T_STATE` on every connect (V1.2.4 fix — without this, dashboard clears are silently lost after Mosquitto restarts). |
| Reed-switch debounce | 3-layer: (1) sender 60 s WDT lockout, (2) receiver dup-seq + repeat-MAIL-60s guard, (3) Node-RED Trigger 60 s block. |
| HA integration | MQTT discovery — receiver publishes 18 `homeassistant/.../config` payloads (retained) at boot. HA auto-creates all entities under one "Mailbox" device card. |
| OTA password | None — trust the LAN. Leave the Arduino IDE password field blank. |
| Watchdog | 30 s `esp_task_wdt`, kicked from `loop()`. Also kicked from OTA `onProgress` callback (V1.1.1) to survive multi-second flash erases. |
| Tier-3 deferred features | Buzzer, AES encryption, web status page, HA reboot command — all deferred indefinitely. |

---

## Sender hardware (summary — canonical source: `docs/SENDER_HARDWARE.md`)

| Pin | Function |
|---|---|
| D0 (INT2) | Reed switch, INPUT_PULLUP, inverted mount — reed CLOSED when lid OPEN |
| D2 / D3 | BME280 SDA / SCL (I2C 0x76: CSB→3V3, SDO→GND) |
| D5 | Blue debug LED, firmware-gated by `#define DEBUG_LED` |
| D9 / A9 | LiPo voltage divider (÷2) |
| Internal | RFM95 SX1276 (LoRa radio) |

Reed switch type: **Normally Open (NO)** — contact closes when magnet is near
(lid closed). External antenna via u.FL pigtail (+2 dBi 868 MHz).

---

## LoRa link parameters (must match on both ends)

| Parameter | Value |
|---|---|
| Frequency | 866.0 MHz |
| Bandwidth | 250 kHz |
| Spreading factor | 9 |
| TX power (sender) | +20 dBm |

---

## MQTT topic tree (V1.1.0+ — no legacy aliases)

```
mailbox/state                        ← retained, MAIL/EMPTY, bidirectional
mailbox/sender/temperature
mailbox/sender/humidity
mailbox/sender/pressure
mailbox/sender/battery_voltage
mailbox/sender/battery_percent
mailbox/sender/packet_seq
mailbox/sender/last_packet_type      ← human label: mail/heartbeat/heartbeat (low batt)/boot
mailbox/sender/boot_count
mailbox/sender/boot_reason
mailbox/sender/sensor_ok             ← side channel, no HA entity
mailbox/sender/alive                 ← retained, true/false
mailbox/sender/version
mailbox/receiver/rssi
mailbox/receiver/snr
mailbox/receiver/last_seen           ← ISO 8601 NTP timestamp
mailbox/receiver/online              ← retained LWT, true/false
mailbox/receiver/wifi_rssi
mailbox/receiver/uptime              ← days, 2 decimals
```

---

## HA entity naming rules (V1.2.0+)

Discovery payloads must **not** include the `mailbox_` prefix in `name`,
`unique_id`, or `object_id`. HA prepends the device slug automatically.

| Field in discovery JSON | Example |
|---|---|
| `name` | `"Sender temperature"` (not `"Mailbox sender temperature"`) |
| `unique_id` | `"sender_temperature"` (not `"mailbox_sender_temperature"`) |
| `object_id` | same as `unique_id` |

HA composes: device `"Mailbox"` + name `"Sender temperature"` → display
`"Mailbox › Sender temperature"`, entity_id `sensor.mailbox_sender_temperature`.

Naming scheme for entity_ids:
- `sensor.mailbox_sender_<thing>` — data the Feather produces
- `sensor.mailbox_receiver_<thing>` — data the receiver measures
- `binary_sensor.mailbox_state` — device-level headline (no prefix)

When adding a new entity, slot it under `sender_` or `receiver_` accordingly.
Never invent a new top-level `mailbox/<thing>` topic outside the scheme.

---

## Packet types

| type= | Meaning |
|---|---|
| 1 | Reed event (mail arrived) |
| 2 | Heartbeat (normal, 48 h) |
| 3 | Heartbeat (low battery, 6 h) |
| 4 | Boot |

---

## Sender build-time toggles

| Macro | Production | Bench |
|---|---|---|
| `DEBUG_NOSLEEP` | 0 | 1 — stays awake, prints sensor values, 5 s reed lockout |
| `ENABLE_SERIAL` | 0 | 1 |
| `DEBUG_LED` | 0 | 1 — blue LED on D5 blinks ~100 ms per TX |

---

## Key files

| File | Purpose |
|---|---|
| `FEATURES.md` | Canonical "what the system does" — authoritative on behaviour |
| `firmware/mailbox_sender_V3/mailbox_sender_V3.ino` | Sender (Feather 32u4) |
| `firmware/mailbox_receiver_V3/mailbox_receiver_V3.ino` | Receiver (Heltec V3) |
| `docs/SENDER_HARDWARE.md` | Canonical sender pinout + wiring |
| `docs/RECEIVER_V3_PLAN.md` | Original design plan, decisions log |
| `docs/WORKFLOW_AND_TESTING.md` | Phased build/test plan |

---

## Notable bugs fixed (don't reintroduce)

| Version | Bug |
|---|---|
| V1.0.5 | PRG short-press didn't wake OLED — `heltec_display_power(false)` cuts Vext; switched to soft `displayOff()`/`displayOn()`. |
| V1.0.6 | `mailState` was set before MQTT publish — if broker was down, flag flipped silently. Now only mutated after successful publish. |
| V1.0.6 | `onMqttMessage` subscribed to T_STATE but ignored it — retained state never restored on boot. Fixed. |
| V1.1.1 | OTA killed by 30 s WDT mid-upload (`WinError 10054`) — now kicked from `onProgress` callback. |
| V1.2.4 | `connectMqtt()` never re-subscribed to T_STATE — after Mosquitto restart, dashboard clears were silently lost and reed events were dropped as "already MAIL". Fixed by adding `subscribe(T_STATE)` inside `connectMqtt()`. |
| V1.2.5 | Reed event arriving while receiver was in MQTT exponential-backoff (HA rebooted, receiver not yet reconnected) was permanently lost. `pendingMailState` flag added; connectMqtt() publishes MAIL before subscribing if flag is set. |
| V1.2.6 | State never published when sender's `r=0` in a type=1 packet. Root cause: sender reads the reed pin at packet-build time (up to 8 s after ISR + BME280 read); lid may already be closed. Removed `lastPkt.reedOpen` gate from the type=1 state check — pktType==1 is the authoritative mail-arrived signal. |
