# Mailbox Notifier

LoRa-based mailbox sensor that pings Home Assistant (and your phone via Pushover) when
the postman opens your mailbox lid. Sender lives 50 m away in the mailbox, runs on a
2000 mAh LiPo for ~150 days. Receiver bridges LoRa packets to MQTT and Home Assistant.

Field-tested at **-58 dBm RSSI / +11 dB SNR** through one large tree at 50 m.

```
┌────────────────────────┐                              ┌──────────────────────────┐
│  Mailbox sender        │                              │  House receiver          │
│  Adafruit Feather      │      LoRa @ 866 MHz, SF9     │  Heltec WiFi LoRa 32 V3  │
│   32u4 LoRa            │      ──────────────────►     │  (ESP32-S3 + SX1262)     │
│  + BME280              │      reed event +            │                          │
│  + reed switch         │      heartbeat packets       │  WiFi → MQTT → HA        │
│  + 2000 mAh LiPo       │                              │  → Node-RED → Pushover   │
└────────────────────────┘                              └──────────────────────────┘
```

---

## What it does

- **Mail-arrival notification** — lid opens → reed switch closes → LoRa packet → MQTT →
  iPhone Pushover within ~2 seconds.
- **Sticky state** in Home Assistant. Cleared by tapping a dashboard button or
  long-pressing the receiver's PRG button after you've checked the mailbox.
- **Environmental sensors** — temperature, humidity, pressure inside the mailbox (BME280).
- **Battery monitoring** — voltage + percentage, with a 6 h "low battery" heartbeat boost
  (vs the normal 48 h cadence) when it dips below 3.6 V.
- **Three-layer dedup** — sender 60 s lockout + receiver guard + Node-RED Trigger node —
  prevents bouncy lid double-notifications.
- **21 HA entities** auto-discovered under one "Mailbox" device card — no manual
  `configuration.yaml` editing.
- **Remote receiver reboot** — one-tap button in the HA device card.
- **Packet loss counter + frequency error sensor** — LoRa link health diagnostics.

---

## Hardware

### Sender (in the mailbox)

- Adafruit Feather 32u4 LoRa (RFM95 SX1276, 868 MHz)
- BME280 6-pin module (GY-BME280, I2C 0x76)
- Normally Open reed switch + magnet
- 2000 mAh 3.7 V LiPo
- External 868 MHz stubby antenna (+2 dBi) on u.FL pigtail

Full pinout, power tree, and reed mounting diagrams: [`HARDWARE.md`](HARDWARE.md).

### Receiver (in the house)

- Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 + OLED)
- USB-powered, mounted on a window

### Backend

- Home Assistant + Mosquitto MQTT add-on
- Node-RED add-on (Pushover bridge)
- Pushover account + iPhone

---

## LoRa link parameters

Both ends must match exactly.

| Parameter | Value |
|---|---|
| Frequency | 866.0 MHz |
| Bandwidth | 250 kHz |
| Spreading factor | 9 |
| TX power (sender) | +20 dBm |
| Hardware CRC | enabled |
| Coding rate | 4/5 (library default) |

---

## Packet format

Key=value ASCII, ~60–70 bytes, ~190 ms airtime at SF9/250 kHz.

```
id=AA&type=1&seq=42&t=21.43&h=67&p=1013.2&v=3920&r=1&sok=1&boot=12&up=1234&br=normal&fw=V1.1.0
```

| Key | Meaning |
|---|---|
| `id` | Device ID — always `AA` |
| `type` | `1` reed/mail · `2` heartbeat · `3` low-batt heartbeat · `4` boot |
| `seq` | Sequence counter (uint8, wraps 0–255, resets on boot) |
| `t` | Temperature °C (BME280) |
| `h` | Humidity % (BME280) |
| `p` | Pressure hPa (BME280) |
| `v` | Battery voltage mV |
| `r` | Reed state at TX time (0 = lid closed, 1 = lid open). Diagnostic only — does **not** gate the mail-arrived state publish (see V1.2.6 bug fix) |
| `sok` | BME280 read OK (1 = fresh, 0 = sensor failed, values are last-known cache) |
| `boot` | EEPROM-backed boot count |
| `up` | Uptime in minutes since this boot |
| `br` | Boot reason: `power-on` / `external reset` / `watchdog` / `brown-out` / `normal`. Present in every packet (V1.1.0+) so HA always has a value |
| `fw` | Sender firmware version string (V1.0.9+) |

Forward-compatible by design — receiver ignores unknown keys.

---

## MQTT topic tree

| Topic | Direction | Retained | Notes |
|---|---|---|---|
| `mailbox/state` | RX ↔ HA | yes | `MAIL` / `EMPTY` — sticky, bidirectional |
| `mailbox/sender/temperature` | RX → HA | no | °C |
| `mailbox/sender/humidity` | RX → HA | no | % |
| `mailbox/sender/pressure` | RX → HA | no | hPa |
| `mailbox/sender/battery_voltage` | RX → HA | yes | V |
| `mailbox/sender/battery_percent` | RX → HA | yes | %, computed from LiPo curve |
| `mailbox/sender/packet_seq` | RX → HA | yes | sequence counter |
| `mailbox/sender/last_packet_type` | RX → HA | yes | human label: mail / heartbeat / heartbeat (low batt) / boot |
| `mailbox/sender/boot_count` | RX → HA | yes | EEPROM-backed |
| `mailbox/sender/boot_reason` | RX → HA | yes | power-on / external reset / watchdog / brown-out / normal |
| `mailbox/sender/sensor_ok` | RX → HA | no | side channel, no HA entity |
| `mailbox/sender/alive` | RX → HA | yes | true/false, timeout-driven (98 h) |
| `mailbox/sender/version` | RX → HA | yes | sender FW string |
| `mailbox/receiver/rssi` | RX → HA | yes | dBm of last RX packet |
| `mailbox/receiver/snr` | RX → HA | yes | dB of last RX packet |
| `mailbox/receiver/last_seen` | RX → HA | yes | ISO 8601 NTP timestamp |
| `mailbox/receiver/online` | RX LWT | yes | true/false, LWT-managed |
| `mailbox/receiver/wifi_rssi` | RX → HA | no | dBm |
| `mailbox/receiver/uptime` | RX → HA | no | days, 2 decimals |
| `mailbox/receiver/packet_loss` | RX → HA | yes | cumulative missed-seq count (V1.3.0+) |
| `mailbox/receiver/freq_error` | RX → HA | no | Hz, sender crystal drift (V1.3.0+) |
| `mailbox/cmd/reboot` | HA → RX | — | any payload → ESP.restart() (V1.3.0+) |

---

## Home Assistant integration

### MQTT discovery — 21 entities

Receiver publishes 21 retained `homeassistant/.../config` payloads at every boot. HA
auto-creates all entities under one **"Mailbox"** device card — no `configuration.yaml`
editing needed.

| Entity | Platform | Device class | Notes |
|---|---|---|---|
| `binary_sensor.mailbox_state` | binary | occupancy | `MAIL` / `EMPTY`, sticky |
| `sensor.mailbox_sender_temperature` | sensor | temperature | °C |
| `sensor.mailbox_sender_humidity` | sensor | humidity | % |
| `sensor.mailbox_sender_pressure` | sensor | pressure | hPa |
| `sensor.mailbox_sender_battery_voltage` | sensor | voltage | V, diagnostic |
| `sensor.mailbox_sender_battery` | sensor | battery | %, diagnostic |
| `sensor.mailbox_sender_packet_seq` | sensor | — | diagnostic |
| `sensor.mailbox_sender_last_packet_type` | sensor | — | diagnostic |
| `sensor.mailbox_sender_boot_count` | sensor | — | diagnostic |
| `sensor.mailbox_sender_boot_reason` | sensor | — | diagnostic |
| `binary_sensor.mailbox_sender_alive` | binary | connectivity | diagnostic |
| `sensor.mailbox_sender_version` | sensor | — | diagnostic |
| `sensor.mailbox_receiver_rssi` | sensor | signal_strength | dBm, diagnostic |
| `sensor.mailbox_receiver_snr` | sensor | — | dB, diagnostic |
| `sensor.mailbox_receiver_last_seen` | sensor | timestamp | retained |
| `binary_sensor.mailbox_receiver_online` | binary | connectivity | LWT, diagnostic |
| `sensor.mailbox_receiver_wifi_rssi` | sensor | signal_strength | dBm, diagnostic |
| `sensor.mailbox_receiver_uptime` | sensor | duration | days, diagnostic |
| `sensor.mailbox_receiver_packet_loss` | sensor | — | total_increasing, diagnostic |
| `sensor.mailbox_receiver_freq_error` | sensor | — | Hz, diagnostic |
| `button.mailbox_receiver_reboot` | button | restart | triggers ESP.restart() |

### Entity naming rule (V1.2.0+) — important for firmware changes

HA auto-composes entity IDs as `<device_slug>_<object_id>`. The device slug is
`mailbox`. Therefore discovery payloads must **not** include `mailbox_` in `name`,
`unique_id`, or `object_id` — HA prepends it automatically.

| Discovery field | Correct | Wrong |
|---|---|---|
| `name` | `"Sender temperature"` | `"Mailbox sender temperature"` |
| `unique_id` | `"sender_temperature"` | `"mailbox_sender_temperature"` |
| `object_id` | `"sender_temperature"` | `"mailbox_sender_temperature"` |

Violating this rule produces doubled entity IDs like
`sensor.mailbox_mailbox_sender_temperature`. V1.1.0/V1.1.1 had this bug; V1.2.0 fixed it.

### Lovelace dashboard

The dashboard needs a button that publishes `EMPTY` (retained) directly to
`mailbox/state` to clear the sticky mail state. The receiver's `T_STATE` subscription
picks it up and syncs `mailState` locally.

---

## Node-RED flows

Three flow exports in `Node-RED/`. Import via HA Node-RED: **Menu → Import → paste
contents of each `.txt` file**.

| File | Trigger | Message | Priority |
|---|---|---|---|
| `Node-RED_mail_arrived.txt` | `mailbox/state` → MAIL | "Postia laatikossa! (-95 dBm)" with live RSSI | 0, sound `siren` |
| `Node-RED_battery_low.txt` | `mailbox/sender/last_packet_type` = `"heartbeat (low batt)"` | Low battery alert | 1, sound `falling` |
| `Node-RED_sender_boot.txt` | `mailbox/sender/boot_count` changes (rbe node blocks retained replay) | "Sender rebooted (reason: …, boot #N)" | 0, sound `siren` |

All flows use broker `HomeassistantMQTT` (localhost:1883), Pushover device `iphone`,
title `Mailbox`.

> **Note:** The battery low switch node must match the string `"heartbeat (low batt)"`
> — the human label published by the receiver — not the raw integer `"3"`.

---

## Building

### Prerequisites

**Tools:**
- Arduino IDE 2.x (tested on 2.3.9)
- Git

**Board packages** (Arduino IDE → Board Manager):
- `esp32` by Espressif Systems — for the Heltec V3 receiver
- `Adafruit AVR Boards` by Adafruit — for the Feather 32u4 sender

**Libraries** (Arduino IDE → Library Manager):

| Library | For | Install name |
|---|---|---|
| `heltec_esp32_lora_v3` by ropg | Receiver | `heltec_esp32_lora_v3` |
| `ArduinoMqttClient` by Arduino | Receiver | `ArduinoMqttClient` |
| `LoRa` by Sandeep Mistry | Sender | `LoRa` |
| `Adafruit BME280 Library` by Adafruit | Sender | `Adafruit BME280 Library` |
| `Low-Power` by Rocket Scream | Sender | `Low-Power` |

Board targets:
- Receiver: **Heltec WiFi LoRa 32(V3)**
- Sender: **Adafruit Feather 32u4**

---

### Sender wiring

Full wiring reference with diagrams: [`HARDWARE.md`](HARDWARE.md).

**Critical non-obvious points:**

> ⚠ **Reed switch mounting is INVERTED.** The reed is Normally Open (NO) type. Mount
> so the reed is **closed only when the lid is OPEN** (magnet near reed = lid open).
> The standard mounting (magnet on lid, reed on body, aligned when lid closed) is the
> **wrong polarity** — it would drain the battery and never trigger mail events.
> See `HARDWARE.md §1.5` for mounting diagrams.

| Connection | Feather silkscreen | Notes |
|---|---|---|
| BME280 VCC | **3V** | Use 3V3 only — not BAT/USB rails |
| BME280 GND | **GND** | |
| BME280 SDA | **SDA** | |
| BME280 SCL | **SCL** | |
| BME280 CSB | **3V** | Forces I2C mode (LOW = SPI → wrong) |
| BME280 SDO | **GND** | Sets I2C address to **0x76** |
| Reed terminal 1 | **RX** (= D0, INT2) | |
| Reed terminal 2 | **GND** | No external resistor — uses AVR internal pull-up |

Verify BME280 I2C address with a scanner sketch before continuing — should report `0x76`.
If you see `0x77`, SDO is not at GND.

---

### arduino_secrets.h

The receiver uses `firmware/mailbox_receiver_V3/arduino_secrets.h` (not in git).

```bash
cd firmware/mailbox_receiver_V3/
cp arduino_secrets.h.example arduino_secrets.h
# edit arduino_secrets.h
```

Required fields:

| Field | Value |
|---|---|
| `SECRET_SSID` | Your WiFi network name |
| `SECRET_PASS` | Your WiFi password |
| `SECRET_MQTT_BROKER` | MQTT broker IP (e.g. `192.168.1.x`) |
| `SECRET_MQTT_USER` | Mosquitto username |
| `SECRET_MQTT_PASS` | Mosquitto password |
| `SECRET_DOMAINNAME` | Your LAN domain (e.g. `homenet.io`) — used for DHCP hostname |

The sender has no `arduino_secrets.h` — it only talks LoRa, no WiFi or MQTT.

---

### Build toggles — sender

Set these **before** compiling. All live at the top of `mailbox_sender_V3.ino`.

| Macro | Production | Bench | Effect |
|---|---|---|---|
| `DEBUG_NOSLEEP` | `0` | `1` | Bench mode: chip stays awake, prints sensor values once/second in Serial Plotter format, 5 s reed lockout |
| `DEBUG_HEARTBEAT_30S` | `0` | optional | Sub-toggle inside `DEBUG_NOSLEEP`: also fire heartbeat every 30 s |
| `ENABLE_SERIAL` | `0` | `1` | Compile Serial debug in/out |
| `DEBUG_LED` | `0` | `1` | Blue LED on D5 blinks ~100 ms after every LoRa TX |
| `BOOT_UPLOAD_WINDOW_MS` | `10000` | n/a | Holds USB-CDC alive 10 s after boot for OTA-less re-flash via auto-reset |

**For field deployment:** all set to `0`.

### Build toggles — receiver

| Macro | Default | Effect |
|---|---|---|
| `ENABLE_SERIAL` | `1` | Receiver is USB-powered — Serial diagnostics always on |

---

### Flash sequence

> Flash **receiver first**. The sender can't do anything useful until the receiver is
> listening and publishing discovery to HA.

**1. Flash receiver**

- Open `firmware/mailbox_receiver_V3/mailbox_receiver_V3.ino` in Arduino IDE
- Board: **Heltec WiFi LoRa 32(V3)**, Port: USB COM port
- Upload → watch Serial Monitor (115200 baud)
- Wait for: `[disc] Publishing 21 entity configs`
- In HA → Settings → Devices & Services → MQTT: "Mailbox" device should appear with
  21 entities. If it doesn't appear within 30 s, check that `arduino_secrets.h` has
  the correct broker address.

**2. Verify HA entities**

- Mailbox device card should show all 21 entities grouped correctly
- `binary_sensor.mailbox_state` should be `EMPTY`
- `binary_sensor.mailbox_receiver_online` should be `true`

**3. Flash sender**

- Set `DEBUG_NOSLEEP 1`, `ENABLE_SERIAL 1` for bench test first
- Open `firmware/mailbox_sender_V3/mailbox_sender_V3.ino`
- Board: **Adafruit Feather 32u4**, Port: USB COM port
- **Unplug LiPo before plugging USB** (undefined behaviour during reset spike)
- Upload
- Watch Serial Monitor: should print temperature, humidity, pressure, battery voltage
- Move a magnet near the reed switch → one LoRa packet should fire within 8 s

**4. Verify end-to-end**

- Trigger reed → `mailbox/state` should flip to `MAIL` in MQTT Explorer and HA
- Pushover notification should arrive on iPhone within ~5 s
- HA dashboard button (publishes `EMPTY` retained to `mailbox/state`) should clear the state

**5. Set sender to production mode**

- Set `DEBUG_NOSLEEP 0`, `ENABLE_SERIAL 0`, `DEBUG_LED 0`
- Re-flash the sender
- After `setup()` there is a 10 s upload window (`BOOT_UPLOAD_WINDOW_MS`). If you need
  to re-flash without opening the mailbox: press Reset on the Feather, wait 3 s, click
  Upload in Arduino IDE.

---

### Node-RED import

1. Open Node-RED in HA (HA → Add-ons → Node-RED → Open Web UI)
2. Menu (top right) → **Import**
3. Paste the contents of `Node-RED/Node-RED_mail_arrived.txt` → Import
4. Repeat for `Node-RED_battery_low.txt` and `Node-RED_sender_boot.txt`
5. Click **Deploy**
6. Trigger the sender → Pushover notification should arrive

---

### OTA updates (receiver only)

Once the receiver is running, Arduino IDE lists it as a network port:
`mailbox at <IP>` (mDNS: `mailbox.local`).

- Select the network port in Arduino IDE
- Click Upload
- When prompted for a password: **leave the field blank and press OK**
  (the receiver has no OTA password — trust the LAN)
- OLED shows live `OTA xx%` during upload

The sender has no OTA — flashing it requires USB cable and the 10 s upload window
(or a physical trip to the mailbox to press Reset).

---

### Pre-flash checklist

Before every flash — sender or receiver:

1. **Right board selected?** Heltec WiFi LoRa 32 V3 ≠ V2 ≠ ESP32 Dev Module.
   Adafruit Feather 32u4 ≠ Feather M0 ≠ generic Arduino.
2. **Right port?** USB hubs reorder COM numbers — verify it didn't change.
3. **Sender: LiPo unplugged before connecting USB.** Charging logic handles both, but
   uploading while on battery has bricked Feather sketches (undefined behaviour during
   reset spike).
4. **`arduino_secrets.h` not open in another editor** that might silently save
   placeholder values.
5. **Version header bumped** in the `.ino` file you edited (per project coding rules).

---

### Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| "Mailbox" device never appears in HA | MQTT TX buffer overflow — discovery JSONs (~350 bytes each) silently dropped | Verify `mqttClient.setTxPayloadSize(1024)` is in receiver setup (V1.0.2+ fix) |
| State stays EMPTY after reed trigger, sensor data updates | `r=0` gate bug (pre-V1.2.6) or receiver lost `mailbox/state` subscription | Flash V1.2.6+; `connectMqtt()` must call `subscribe(T_STATE)` on every reconnect |
| Mail event missed after HA reboot | Reed packet arrived during MQTT backoff window | Flash V1.2.5+; `pendingMailState` flag defers the publish until reconnect |
| OTA upload dies at ~50% (WinError 10054) | 30 s watchdog trips during 4 KB flash erase | Flash V1.1.1+; OTA callbacks must kick `esp_task_wdt_reset()` |
| PRG short-press doesn't wake OLED | `heltec_display_power(false)` cuts Vext, needs full re-init | Flash V1.0.5+; use `display.displayOff()` / `displayOn()` instead |
| No packets received at all | LoRa parameter mismatch | Verify both ends: 866.0 MHz, SF9, BW 250 kHz. Even 866 vs 866.0 can cause issues in some libs |

---

## Operating the system

### Sticky mail state

- Receiver publishes `mailbox/state = MAIL` (retained) on EMPTY→MAIL transition only.
- Clear by: HA dashboard button (publishes `EMPTY` retained to `mailbox/state`) **or**
  Heltec PRG long-press (≥ 1500 ms).
- State survives receiver reboots — the retained value is replayed from Mosquitto on
  reconnect and the receiver's `mailState` flag syncs via the `onMqttMessage` callback.

### PRG button

| Press | Action |
|---|---|
| Short (50 ms – 1499 ms) | Wake OLED if it timed out |
| Long (≥ 1500 ms) | Manually clear `mailbox/state` to `EMPTY` |

### OLED layout

```
WiFi MQTT  10:42           V1.3.0   ← top row: connection status + clock + FW version
                                    
         M A I L !                  ← big state (or "—" when empty)
                                    
21.4°C  67%  1013 hPa               ← latest sender values
Bat: 3.92V (78%)                    
RSSI -84  SNR 9.5  #42              ← receiver link quality + seq
```

Auto-off after 10 min idle. Woken by PRG short-press.

### Sender-alive watchdog

If no packet is received for **98 h** (48 h heartbeat × 2 + 2 h slack), the receiver
publishes retained `mailbox/sender/alive = false`. HA can trigger a "battery dead?"
alert. The 98 h window tolerates one missed heartbeat before flagging.

### Three-layer reed dedup

The mailbox lid can bounce or swing open multiple times per mail event. Three layers
prevent duplicate notifications:

1. **Sender lockout (60 s):** after every reed TX, the sender detaches the reed
   interrupt and sleeps for ~64 s before re-arming. Implemented as WDT-only sleep
   (not `delay()`) so battery cost is negligible (~30 mAs total vs ~300 mAs awake-poll).
2. **Receiver guard:** rejects same-seq duplicate packets; also rejects a new MAIL
   state if one was published within the last 60 s.
3. **Node-RED Trigger node (60 s block):** on the Pushover output. Adjustable from
   the Node-RED UI without reflashing anything.

---

## Design decisions

| # | Decision | Choice | Why |
|---|---|---|---|
| 1 | Packet format | Key=value ASCII | Human-readable, forward-compatible (receiver ignores unknown keys), easy to debug on OLED. Binary would save ~80 ms airtime — not worth it at 1–2 packets/day |
| 2 | MQTT discovery | Yes | HA auto-creates all 21 entities; no `configuration.yaml` to maintain |
| 3 | Heartbeat cadence | 48 h normal, 6 h when vbat < 3.6 V | 48 h halves TX count vs original 24 h; low-batt boost preserves dead-battery visibility without cost |
| 4 | Sender-alive timeout | 98 h (48 h × 2 + 2 h slack) | Tolerates one missed heartbeat before alerting |
| 5 | Sticky mail state | Manual clear only | Auto-clear on lid-close adds complexity with no benefit — user clears when they pick up the mail |
| 6 | Reed lockout | WDT-sleep 64 s | 10× less battery than staying awake for 60 s (`delay()`) |
| 7 | Reed dedup layers | 3 layers (sender + receiver + Node-RED) | Each layer protects against different failure modes; adjustable at Node-RED without reflashing |
| 8 | LoRa library (receiver) | RadioLib via `heltec_unofficial` | Multi-chip, well documented, SX1262 support |
| 9 | LoRa library (sender) | Sandeep Mistry `LoRa.h` | Already on board, works with SX1276, simple API |
| 10 | Sender MCU | Adafruit Feather 32u4 LoRa | Already wired, has LiPo charger, AVR PWR_DOWN sleep (~5 µA) |
| 11 | MQTT cleanSession | true | Receiver re-subscribes to `mailbox/state` and `mailbox/cmd/reboot` on every connect — no stale subscription state |
| 12 | OTA password | None | Trust the LAN — password adds friction to updates with no real security gain on a home network |
| 13 | Legacy topic compat | Removed in V1.1.0 | Clean break; old `mailboxstatus/*` topics were confusing and stale |
| 14 | BME280 I2C address | 0x76 (SDO→GND) | Frees 0x77 if a second sensor is ever added |
| 15 | Deferred features | AES-128, buzzer, web page | Require sender reflash (physical trip) or significant complexity for marginal benefit |

---

## Build-time toggles

### Sender

| Macro | Production | Bench | Effect |
|---|---|---|---|
| `DEBUG_NOSLEEP` | `0` | `1` | Stays awake forever, Serial Plotter output, 5 s reed lockout |
| `DEBUG_HEARTBEAT_30S` | `0` | optional | Sub-toggle: fire heartbeat every 30 s while `DEBUG_NOSLEEP` is on |
| `ENABLE_SERIAL` | `0` | `1` | Compile Serial debug in/out |
| `DEBUG_LED` | `0` | `1` | Blue LED on D5 blinks ~100 ms per TX |
| `BOOT_UPLOAD_WINDOW_MS` | `10000` | n/a | USB-CDC stays alive 10 s after boot for re-flash via auto-reset |

### Receiver

| Macro | Default | Effect |
|---|---|---|
| `ENABLE_SERIAL` | `1` | Serial diagnostics always on (USB-powered device) |

---

## Versioning

| Bump | Triggers |
|---|---|
| `Vx.y.z` patch | Bug fix, comment, typo, cosmetic only |
| `Vx.y` minor | Feature, behaviour tweak, UX change |
| `Vx` major | Hardware swap, MCU change, packet format break |

Every `.ino` has one `#define FW_VERSION "Vx.y.z"` near the top. The boot Serial log,
OLED display, and MQTT discovery `sw_version` all derive from it — the version string
can't drift between header and runtime.

---

## Deferred features

| Feature | Why deferred |
|---|---|
| AES-128 encryption | Requires a new AVR-compatible AES library on the sender + physical trip to the mailbox to reflash. Breaking packet format change → major version bump on both sides |
| Buzzer | Useful in-house alert, but requires GPIO wiring. Low priority when Pushover already works |
| Web status page | Tiny HTTP server on the receiver. Convenient, but the OLED is right there |

---

## Repo layout

```
.
├── firmware/
│   ├── mailbox_sender_V3/          Sender firmware (Feather 32u4)
│   └── mailbox_receiver_V3/        Receiver firmware (Heltec V3)
├── Node-RED/                       Flow exports (import into HA Node-RED)
│   ├── Node-RED_mail_arrived.txt
│   ├── Node-RED_battery_low.txt
│   └── Node-RED_sender_boot.txt
├── HARDWARE.md                     Sender pinout, wiring, reed mounting diagrams
├── CHANGELOG.md                    Full version history
├── README.md                       This file
└── LICENSE                         MIT
```

---

## License

MIT — see [`LICENSE`](LICENSE).
