# Mailbox Notifier

LoRa-based mailbox sensor that pings Home Assistant (and your phone via Pushover) when the postman opens your mailbox lid. Sender lives 50 m away in the mailbox, runs on a 2000 mAh LiPo for ~150 days. Receiver bridges LoRa packets to MQTT and Home Assistant.

Field-tested at -58 dBm RSSI / +11 dB SNR through one large tree at 50 m.

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

## What it does

- **Mail-arrival notification** — opens lid → reed switch closes → LoRa packet → MQTT → iPhone Pushover within ~2 seconds.
- **Sticky state** in Home Assistant. Cleared by tapping a dashboard button or long-pressing the receiver's PRG button after you've checked the mailbox.
- **Environmental sensors** — temperature, humidity, pressure inside the mailbox (BME280).
- **Battery monitoring** — voltage + percentage, with a 6 h "low battery" heartbeat boost (vs the normal 24 h cadence) when it dips below 3.6 V.
- **Three-layer dedup** — sender 60 s lockout + receiver guard + Node-RED Trigger node — prevents bouncy lid double-notifications.
- **Auto-discovered** in HA via MQTT discovery: 17 entities under one "Mailbox sensor" device card, no manual `configuration.yaml` editing.

## Repo layout

```
.
├── firmware/                                      Arduino sketches (each in its own folder, IDE-ready)
│   ├── mailbox_sender_V3/                         Sender firmware
│   └── mailbox_receiver_V3/                       Receiver firmware
├── docs/
│   ├── SENDER_HARDWARE.md                         Sender pinout + wiring reference
│   ├── RECEIVER_V3_PLAN.md                        Receiver design plan + locked decisions
│   └── WORKFLOW_AND_TESTING.md                    Phased build & test plan
├── FEATURES.md                                    Canonical feature list
├── LICENSE                                        MIT
└── README.md                                      This file
```

## Hardware

- **Sender** (in the mailbox)
  - Adafruit Feather 32u4 LoRa (RFM95 SX1276, 868 MHz)
  - BME280 6-pin module (GY-BME280, I2C 0x76)
  - Normally Open reed switch + magnet
  - 2000 mAh 3.7 V LiPo
  - 8.2 cm wire-stub antenna (quarter-wave for 868 MHz)
  - Optional: blue LED on D5 for prototyping
- **Receiver** (in the house)
  - Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 + OLED)
  - USB-powered, on a window with near line-of-sight to the mailbox
- **Backend**
  - Home Assistant + Mosquitto MQTT add-on
  - Node-RED add-on for the Pushover bridge
  - Pushover account + iPhone

Pin maps and wiring details: [`docs/SENDER_HARDWARE.md`](docs/SENDER_HARDWARE.md).

## Quickstart

1. **Clone the repo**
   ```
   git clone https://github.com/Marcozarg/mailbox_notifier.git
   ```

2. **Set up the receiver's secrets** — only the receiver uses WiFi/MQTT.
   ```
   cd firmware/mailbox_receiver_V3/
   cp arduino_secrets.h.example arduino_secrets.h
   # edit arduino_secrets.h with your WiFi + MQTT credentials
   ```
   The sender doesn't need an `arduino_secrets.h` — it only talks LoRa, no
   WiFi or MQTT.

3. **Install Arduino IDE 2.x** and the following:
   - **Receiver libraries:** `heltec_esp32_lora_v3` (by ropg), `ArduinoMqttClient` (by Arduino).
     Plus board package: ESP32 by Espressif, board target "Heltec WiFi LoRa 32(V3)".
   - **Sender libraries:** `LoRa` (by Sandeep Mistry), `Adafruit BME280 Library` (by Adafruit), `Low-Power` (by Rocket Scream Electronics).
     Plus board package: Adafruit AVR Boards by Adafruit, board target "Adafruit Feather 32u4".

4. **Wire up the sender** per [`docs/SENDER_HARDWARE.md`](docs/SENDER_HARDWARE.md). The reed-switch mounting is non-obvious — make sure to read §1.5 of that doc.

5. **Flash the receiver first**, watch its Serial Monitor for the `[disc] Publishing 15 entity configs` line. The "Mailbox sensor" device should appear in HA → Settings → Devices & Services → MQTT.

6. **Flash the sender**. Note the build-time toggles at the top of the `.ino`:
   - `DEBUG_NOSLEEP=1` for bench debugging (chip stays awake, prints sensor values once a second in Serial Plotter format).
   - All toggles set to `0` for field deployment.

7. **Drop in the dashboard** from `home_assistant/HA_mailbox_dashboard.yaml` and the Pushover flow from `home_assistant/nodered_pushover_flow.json`.

## Documentation

- [`FEATURES.md`](FEATURES.md) — what the system does, every behaviour, pin map, packet format, MQTT topics.
- [`docs/SENDER_HARDWARE.md`](docs/SENDER_HARDWARE.md) — bench wiring guide.
- [`docs/RECEIVER_V3_PLAN.md`](docs/RECEIVER_V3_PLAN.md) — design rationale and locked decisions.
- [`docs/WORKFLOW_AND_TESTING.md`](docs/WORKFLOW_AND_TESTING.md) — phased build + test plan with acceptance criteria.

## License

MIT — see [`LICENSE`](LICENSE).
