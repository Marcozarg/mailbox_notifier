# Mailbox Notifier — Claude context

This file is read by Claude Code on every session. Keep it up to date when
decisions change. The canonical behaviour reference is `README.md`.

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
| Receiver | V1.3.0 | `firmware/mailbox_receiver_V3/mailbox_receiver_V3.ino` |

---

## Workflow rules — read these first

- **Plan before coding.** When asked to change something, explain the approach
  and ask for confirmation before writing any code. Surface options +
  recommendation. Only write code once Marko says to proceed.
- **Sender flashing requires a physical trip** to the mailbox with a USB cable.
  Flag this explicitly whenever a sender change is proposed.
- **Receiver is OTA-flashable** from inside the house (see README §OTA updates).
  No password — trust the LAN.
- **`README.md` is the canonical behaviour reference.** If a request
  contradicts it, say so before proceeding.
- **`HARDWARE.md` is the single source of truth for sender pinout.**
  Never guess pin assignments.

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
| MQTT cleanSession | `cleanSession=true`. `connectMqtt()` re-subscribes to `T_STATE` and `T_CMD_REBOOT` on every connect (V1.2.4 fix). |
| Reed-switch debounce | 3-layer: (1) sender 60 s WDT lockout, (2) receiver dup-seq + repeat-MAIL-60s guard, (3) Node-RED Trigger 60 s block. |
| HA integration | MQTT discovery — receiver publishes 21 `homeassistant/.../config` payloads (retained) at boot. HA auto-creates all entities under one "Mailbox" device card. |
| HA entity naming (V1.2.0+) | Discovery payloads must NOT include `mailbox_` prefix in `name`/`unique_id`/`object_id` — HA prepends the device slug automatically. Violating this produces `sensor.mailbox_mailbox_*` doubled IDs. |
| OTA password | None — trust the LAN. Leave the Arduino IDE password field blank. |
| Watchdog | 30 s `esp_task_wdt`, kicked from `loop()`. Also kicked from OTA `onProgress` callback (V1.1.1). |
| Deferred features | AES encryption, buzzer, web status page. |

---

## Sender hardware (summary — canonical source: `HARDWARE.md`)

| Pin | Function |
|---|---|
| D0 (INT2) | Reed switch, INPUT_PULLUP, inverted mount — reed CLOSED when lid OPEN |
| D2 / D3 | BME280 SDA / SCL (I2C 0x76: CSB→3V3, SDO→GND) |
| D5 | Blue debug LED, firmware-gated by `#define DEBUG_LED` |
| D9 / A9 | LiPo voltage divider (÷2) |
| Internal | RFM95 SX1276 (LoRa radio) |

Reed switch type: **Normally Open (NO)** — inverted mount, reed CLOSED = lid OPEN.
External antenna via u.FL pigtail (+2 dBi 868 MHz).

---

## Key files

| File | Purpose |
|---|---|
| `README.md` | Canonical behaviour reference + full build guide |
| `HARDWARE.md` | Canonical sender pinout + wiring + reed mounting diagrams |
| `CHANGELOG.md` | Full version history |
| `firmware/mailbox_sender_V3/mailbox_sender_V3.ino` | Sender (Feather 32u4) |
| `firmware/mailbox_receiver_V3/mailbox_receiver_V3.ino` | Receiver (Heltec V3) |

---

## Notable bugs fixed (don't reintroduce)

| Version | Bug |
|---|---|
| V1.0.5 | PRG short-press didn't wake OLED — `heltec_display_power(false)` cuts Vext; switched to soft `displayOff()`/`displayOn()`. |
| V1.0.6 | `mailState` was set before MQTT publish — if broker was down, flag flipped silently. Now only mutated after successful publish. |
| V1.0.6 | `onMqttMessage` subscribed to T_STATE but ignored it — retained state never restored on boot. Fixed. |
| V1.1.1 | OTA killed by 30 s WDT mid-upload (`WinError 10054`) — now kicked from `onProgress` callback. |
| V1.2.0 | All 18 entities had doubled `mailbox_mailbox_*` IDs — HA prepends the device slug; discovery must not also include it. Fixed by removing `mailbox_` prefix from `name`/`unique_id`/`object_id` in all discovery payloads. |
| V1.2.4 | `connectMqtt()` never re-subscribed to T_STATE — after Mosquitto restart, dashboard clears were silently lost and reed events were dropped as "already MAIL". Fixed. |
| V1.2.5 | Reed event arriving while receiver was in MQTT backoff was permanently lost. `pendingMailState` flag added; connectMqtt() publishes MAIL before subscribing if flag is set. |
| V1.2.6 | State never published when sender's `r=0` in a type=1 packet. Removed `lastPkt.reedOpen` gate — pktType==1 is the authoritative mail-arrived signal. |
