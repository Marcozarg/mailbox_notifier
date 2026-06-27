# Changelog

All firmware changes for the mailbox notifier system — sender (Adafruit Feather 32u4
LoRa) and receiver (Heltec WiFi LoRa 32 V3). Ascending order, oldest first.

---

## Sender

### V1.0.0
Initial release. Battery-powered reed-switch sensor: deep-sleeps in AVR PWR_DOWN,
wakes on lid-open ISR, reads BME280, transmits key=value LoRa packet at +20 dBm,
60 s reed lockout, 48 h heartbeat, boot packet with reset reason.

### V1.0.1
- New build toggle `DEBUG_NOSLEEP` (default 1 for bench). Keeps chip awake forever;
  prints sensor readings in Arduino Serial Plotter format (label:value) once per second.
- `DEBUG_NOSLEEP` mode uses 5 s reed lockout (vs 60 s) for faster bench iteration.
- Added `while (!Serial && millis() < 3000)` at boot so USB-CDC enumerates before the
  first Serial log line.

### V1.0.2
- New sub-toggle `DEBUG_HEARTBEAT_30S` (default 0). When `DEBUG_NOSLEEP` is on, the
  loop no longer auto-fires a heartbeat every 30 s unless this is set to 1.
- All visible version strings (boot banner) now derive from a single `FW_VERSION` macro.

### V1.0.3 *(reverted in V1.0.4)*
- New packet type `0x05` = "lid closed". Triggered on reed-open falling edge.
  Reverted per user feedback — lid-close status was not useful.

### V1.0.4
- Reverted V1.0.3: removed packet type 5, `needLidCloseTx` state flag, lid-close
  polling, and DEBUG_NOSLEEP falling-edge detection. Sender goes back to V1.0.2
  single-edge (TX only on lid-open) behaviour.
- `DEBUG_NOSLEEP` reed debounce restored to 5 s.

### V1.0.5
- Switched to production toggles: `DEBUG_NOSLEEP=0`, `DEBUG_LED=0`, `ENABLE_SERIAL=0`.
- New `BOOT_UPLOAD_WINDOW_MS` (10 000 ms). After `setup()` finishes, the chip stays
  awake for 10 s so the Arduino IDE can trigger a normal upload via 1200-baud-touch
  auto-reset without the Caterina double-tap dance. Only applies in field mode
  (`DEBUG_NOSLEEP=0`).

### V1.0.6
- Cosmetic: sketch and parent folder renamed from `adafruit_Lora_32u4_Mailbox_Sender_V3`
  to `mailbox_sender_V3`. No behaviour change.

### V1.0.7
- Documentation only: antenna upgraded from 8.2 cm wire stub (~0 dBi) to external
  868 MHz stubby (~+2 dBi) on a u.FL pigtail routed outside the metal enclosure.
  Yields ~10–15 dB better RSSI/SNR. TX power left at +20 dBm. No code change.

### V1.0.8
- Normal heartbeat cadence doubled: 24 h → 48 h (`HB_TICKS_NORMAL` 10800 → 21600).
  Halves long-term heartbeat TX count, marginally extends battery life.
  Low-battery heartbeat unchanged at 6 h.

### V1.0.9
- WDT tick stretched from 8 s → 32 s via new `sleep32s()` helper that calls
  `LowPower.powerDown(SLEEP_8S, …)` four times. MCU wakes 4× less often (~2700/day
  vs ~10800/day). Reed events still bail out of the inner loop immediately, so
  reed-trigger latency is unchanged (≤ 8 s worst case).
  Tick-count constants rescaled: `HB_TICKS_NORMAL` 21600→5400, `HB_TICKS_LOW_BATT`
  2700→675, `LOCKOUT_TICKS` 8→2 (all durations preserved).
- Added `&fw=` field to every LoRa packet so the receiver can publish the running
  sender version to HA.

### V2.0.0 — 2026-06-02
- Version milestone bump to align with V2.0.0 receiver. No firmware changes.
  Documentation restructured: README.md is the canonical reference, HARDWARE.md moved
  to repo root, planning docs removed.

### V1.1.0 — 2026-05-22
- `&br=` (boot reason) now included in **every** packet, not just type=4 boot packets.
  Prevents HA from showing "Unknown" for boot reason when the sender reboots while HA
  is offline; the current value arrives on the next packet regardless of timing.
- `bootReasonStr()` labels made more human-readable:
  `PORF`→"power-on", `EXTRF`→"external reset", `WDRF`→"watchdog",
  `BORF`→"brown-out", none→"normal" (was "unknown" — Caterina often clears MCUSR).

### V2.3.1 — 2026-06-07
- Fix: replaced rweather `Crypto` library (`AES128` + `CTR<>`) with an inline
  AES-128-CTR implementation. The library's `RNG.cpp` defines `__vector_12`
  (WDT interrupt), which collides with LowPower — both cannot be linked together.
  Inline implementation uses S-box in PROGMEM (`aesExpandKey`, `aesBlock`,
  `aesCtr128`). Wire protocol unchanged; decryption on the receiver is unaffected.

### V2.3.0 — 2026-06-06
- AES-128-CTR encryption on all outgoing LoRa packets.
  Wire format: `[0xAE][seq][boot_lo][boot_hi][0x00][...ciphertext...]`
  The 5-byte unencrypted prefix carries the magic byte and the IV seed;
  receiver reconstructs the IV from these bytes and decrypts with the same key.
  Key is `LORA_AES_KEY` from `arduino_secrets.h` (gitignored).
  Receiver V2.4.0 already handles both encrypted and legacy plaintext packets.

### V2.2.0 — 2026-06-05
- Boot upload window extended from 10 s → 20 s (`BOOT_UPLOAD_WINDOW_MS 20000`).
  More time to trigger an Arduino IDE upload after resetting the sender in the field.
- USB permanently disabled after the boot window (`disableUsb()` called at end of
  `setup()`, field mode only). Eliminates USB controller, PLL, and voltage regulator
  current between sleep cycles. Battery charging via MCP73831 is unaffected —
  the charger IC is wired directly to VBUS and operates independently of the MCU.
- Analog comparator disabled in `setup()`: `ACSR = (1<<ACD)`. Not used in this
  sketch; eliminates active-mode current during brief inter-sleep wake windows.

### V2.1.1 — 2026-06-05
- `cache` struct gains `vbatMv` field; `readVbatMv()` stores result there before
  returning. `buildPacket()` now reads `cache.vbatMv` directly — one ADC conversion
  per TX event instead of two (heartbeat path previously called `readVbatMv()` in
  `loop()` to determine type, then again inside `buildPacket()`).
- Reed event and boot TX paths: added explicit `readVbatMv()` call before `sendPacket()`
  so `cache.vbatMv` is always fresh.

### V2.1.0 — 2026-06-05
- Heartbeat packets (type=2/3) now transmit at +14 dBm via RFO pin instead of
  +20 dBm PA_BOOST. Measured RSSI is −70 to −84 dBm; dropping 6 dB yields
  worst-case ~−90 dBm, still 35 dB above SF9 sensitivity (~−125 dBm).
  TX current: ~29 mA vs ~120 mA — 4× reduction for routine heartbeats.
  Reed (type=1) and boot (type=4) packets keep full +20 dBm for reliability.
  New constant: `LORA_HB_TX_POWER 14` (RFO pin).
- `readVbatMv()`: removed unnecessary `delay(2)` between ADC samples —
  `analogRead()` already blocks until conversion completes. Sample count
  reduced from 8 → 4 (sufficient for a slowly-varying battery voltage).

---

## Receiver

### V2.8.0 — 2026-06-27
- Non-blocking boot: LoRa armed first (~1 s), WiFi started async, OTA/NTP deferred to first WiFi connect, MQTT/discovery deferred to first MQTT connect. Receiver no longer stalls on boot screen after power outage while router recovers.

### V2.7.1 — 2026-06-15
- Fix compile error: timegm() unavailable in Arduino-ESP32 SDK; replaced with inline civil-date-to-epoch formula.

### V2.7.0 — 2026-06-15
- sender_alive no longer goes disconnected on receiver reboot. lastPacketRxTime (NTP epoch) recovered from retained mailbox/sender/last_seen on every connect; alive check skips until recovered.

### V2.6.0 — 2026-06-15
- Retain all sensor and diagnostic values through HA boot: temperature, humidity, pressure, sensor_ok, freq_error, receiver_wifi_rssi, receiver_uptime now published retained so Mosquitto replays them immediately after HA restart.

### V1.0.0
Initial V3 rewrite (Heltec WiFi LoRa 32 V3, RadioLib via `heltec_unofficial.h`).
Key improvements over V2_real:
- Real MQTT reconnect logic (no more `while(1)` halts)
- 30 s hardware watchdog
- MQTT discovery — HA auto-creates "Mailbox" device + entities
- Last-Will-and-Testament
- Sticky `mailbox/state` retained; cleared by HA dashboard or PRG long-press
- Sender-alive heartbeat detector (98 h timeout)
- ArduinoOTA (no password — trust the LAN)
- OLED auto-off after 10 min, woken by PRG short-press
- NTP-stamped `last_seen` (fi.pool.ntp.org, Europe/Helsinki)

### V1.0.1
- Backward-compatibility publishes to V2_real MQTT topics so existing HA dashboard
  entities (`mailboxstatus/switch`, `mailboxstatus/feather`) keep updating while the
  new `mailbox/*` entities are being validated. Removed in V1.1.0.

### V1.0.2
- **BUG FIX** — MQTT TX payload buffer increased from default 256 → 1024 bytes
  (`mqttClient.setTxPayloadSize(1024)`). Each discovery JSON is ~300–400 bytes, so
  the default buffer silently dropped every discovery message. Symptom: "Mailbox"
  device never appeared in HA.
- All visible version strings now derive from a single `FW_VERSION` macro.

### V1.0.3
- **BUG FIX** — MQTT discovery for `mailbox_msg_count` and `mailbox_boot_count` was
  rejected by HA because they declared `state_class` without `unit_of_measurement`
  (HA requires both or neither). Stripped `state_class` from those two entities.
- Active NTP sync wait (up to 10 s) at end of `setup()` so `last_seen` timestamps are
  correct on the very first received packet.

### V1.0.4
- Reverted receiver side of V1.0.3 lid-close feature: removed `mailbox_lid` entity
  from MQTT discovery and stopped publishing to `T_LID`. The `r=` field is still
  parsed for diagnostic logging but not exposed to HA.

### V1.0.5
- **BUG FIX** — PRG short-press logged "wake OLED" but the screen stayed dark. Root
  cause: `heltec_display_power(false)` cuts Vext, requiring full SSD1306 re-init on
  power-on. Switched to `display.displayOff()` / `display.displayOn()` (soft SSD1306
  commands that toggle the panel without dropping its config).

### V1.0.6
- **BUG FIX A** — `mailState` was set to true *before* the MQTT publish. If the broker
  was down, the flag flipped silently without reaching Mosquitto. On subsequent events
  the receiver thought state was already MAIL and never re-published. Fix: only mutate
  `mailState` after a successful publish.
- **BUG FIX B** — `onMqttMessage` subscribed to `T_STATE` but never acted on incoming
  messages. The "restore sticky state from broker" path never worked. Now `T_STATE =
  MAIL/EMPTY` updates `mailState` locally, keeping receiver in sync with broker and HA
  dashboard clears.

### V1.0.7
- Sender-alive timeout doubled: 50 h → 98 h, in lockstep with sender V1.0.8 heartbeat
  change (24 h → 48 h). Scaling preserved at heartbeat × 2 + 2 h slack; still
  tolerates exactly one missed heartbeat.

### V1.0.8
- `mailbox_receiver_uptime` switched from seconds to days (2 decimal places). HA card
  now shows "1.34 d" instead of "115320 s".
- Receiver firmware version moved from boot splash to the main OLED screen
  (right-justified, top status row) so it is always visible.
- New entity `mailbox_sender_version` — shows the sender's running firmware string in
  HA (from `&fw=` field added by sender V1.0.9). Discovery entity count: 15 → 16.

### V1.1.0 *(breaking — MQTT topics and entity_ids restructured)*
- MQTT topics reorganised: sender-derived values under `mailbox/sender/...`, receiver-
  measured values under `mailbox/receiver/...`, `mailbox/state` as the headline.
  Old topics (`mailbox/temp`, `mailbox/rssi`, `mailbox/sender_fw`, etc.) removed with
  no aliasing.
- Entity_ids reorganised into `sender_` / `receiver_` scheme (e.g.
  `mailbox_temp` → `mailbox_sender_temperature`, `mailbox_rssi` → `mailbox_receiver_rssi`).
- Legacy V2_real compatibility publishes removed (`mailboxstatus/switch`,
  `mailboxstatus/feather` JSON blob). HA dashboard clear now publishes `EMPTY`
  (retained) directly to `mailbox/state`.
- Device card name: "Mailbox sensor" → "Mailbox".

### V1.1.1
- **BUG FIX** — ArduinoOTA upload was dying at ~50 % with WinError 10054. Root cause:
  `ArduinoOTA.handle()` blocks `loop()` during binary streaming and 4 KB flash erases,
  tripping the 30 s task watchdog. Fix: `onStart`/`onProgress`/`onEnd`/`onError`
  callbacks call `esp_task_wdt_reset()`.
- `clearDio1Action()` called on OTA start to prevent RadioLib DIO1 ISR from firing
  during flash erase (SPI race). Re-arms LoRa receive on `onError` so a failed upload
  does not leave the receiver LoRa-deaf until reboot.
- OLED now shows live `OTA xx%` progress during upload.

### V1.2.0 — 2026-05-22 *(breaking — entity naming cleanup)*
- **BUG FIX** — All 18 entities had doubled "mailbox_" in their `entity_id`
  (e.g. `sensor.mailbox_mailbox_sender_battery`). Root cause: modern HA prepends the
  device slug to `object_id`, and `object_id` already contained "mailbox_". Fix: drop
  the "Mailbox" prefix from every entity `name` and the "mailbox_" prefix from every
  `unique_id` / `object_id`.
- **Migration:** V1.1.0/V1.1.1 entities go "Unavailable" after this flash. Delete them
  via Settings → Devices & services → MQTT → filter "unavailable" + search "mailbox_mailbox".

### V1.2.1
- `mailbox/sender/last_packet_type` now publishes a human-readable label
  ("mail" / "heartbeat" / "heartbeat (low batt)" / "boot") instead of the raw
  integer 1/2/3/4. Implemented via `packetTypeLabel()` helper. No wire-format change.

### V1.2.2
- WiFi DHCP hostname changed from `arduinomailman` to `mailbox.<SECRET_DOMAINNAME>`
  (e.g. `mailbox.homenet.io`). New secret documented in `arduino_secrets.h.example`.

### V1.2.3
- ArduinoOTA hostname aligned with the WiFi hostname. Both now use the same `WIFI_HOSTNAME`
  macro (`mailbox`). Arduino IDE network port list now reads `mailbox at <IP>`.

### V1.2.4 — 2026-05-25
- **BUG FIX** — After any Mosquitto restart, the receiver lost its `mailbox/state`
  subscription and never got it back. Root cause: `cleanSession=true` causes the broker
  to discard subscriptions on disconnect; `connectMqtt()` was only publishing the LWT
  online flag, never re-subscribing. Without the subscription: (1) HA dashboard clears
  were silently ignored, and (2) the next real reed event was dropped as "already MAIL".
  Fix: move `mqttClient.subscribe(T_STATE)` into `connectMqtt()` so it fires on every
  connect, not just at boot.

### V1.2.5 — 2026-06-02
- **BUG FIX** — Mail arriving while the receiver's MQTT was in exponential backoff
  (HA had finished rebooting but the receiver had not yet reconnected) was permanently
  lost. The retained sensor values already in Mosquitto made it look like data "came
  through fine", masking the miss. Fix: `pendingMailState` bool; set when a type=1
  reed event arrives with MQTT down. In `connectMqtt()`, publish MAIL *before*
  subscribing to `T_STATE` so the broker's retained value is updated first; the
  subscribe-triggered retained delivery then converges `mailState` correctly.

### V1.2.6 — 2026-06-02
- **BUG FIX** — State never published when the sender's `r=` field was 0 in a type=1
  (mail) packet. Root cause: the sender reads `digitalRead(PIN_REED)` at packet-build
  time — up to 8 s after the ISR fires, plus ~10 ms BME280 read. If the lid closed
  before `buildPacket()` ran, `r=0` even for a genuine mail event. The receiver gated
  on `lastPkt.reedOpen`, so with `r=0` the state publish was silently skipped. Fix:
  removed the `lastPkt.reedOpen` gate entirely. `pktType == 1` is the authoritative
  mail-arrived signal; `r=` is diagnostic only.

### V1.3.0 — 2026-06-02
- **NEW** — HA reboot command: receiver subscribes to `mailbox/cmd/reboot`; any
  incoming message triggers `ESP.restart()`. A `button` entity (device_class restart)
  is published via MQTT discovery and appears as a one-tap button on the Mailbox device
  card in HA.
- **NEW** — Packet loss counter: gaps in the sender's `seq` number (uint8 wrapping
  arithmetic) are accumulated in `packetLossCount` and published to
  `mailbox/receiver/packet_loss` (retained, `total_increasing`) after every packet.
  Type=4 boot packets excluded (sender legitimately resets seq to 0 on boot).
- **NEW** — Frequency error sensor: `radio.getFrequencyError()` published to
  `mailbox/receiver/freq_error` (Hz, `measurement`) after each RX, allowing HA to plot
  sender crystal drift vs. temperature over time.
- **INTERNAL** — `publishOneDiscovery()` now treats `stateTopic` as optional: if null
  or empty the `state_topic` JSON field is omitted. Required for the `button` platform,
  which uses `command_topic` instead. Entity count: 18 → 21.

### V2.1.0 — 2026-06-02
- Five entities reclassified from `receiver_*` to `sender_*` because the values
  describe the sender's signal and behaviour, not the receiver hardware itself:
  - `receiver_rssi` → `sender_rssi` (LoRa RSSI of the sender's signal)
  - `receiver_snr` → `sender_snr` (LoRa SNR of the sender's signal)
  - `receiver_last_seen` → `sender_last_seen` (timestamp of last packet from sender)
  - `receiver_freq_error` → `sender_freq_error` (sender crystal frequency drift)
  - `receiver_packet_loss` → `sender_packet_loss` (gaps in sender's seq counter)
- MQTT topics renamed in lockstep: `mailbox/receiver/{rssi,snr,last_seen,freq_error,packet_loss}`
  → `mailbox/sender/*`.
- Old retained discovery configs cleared at boot so HA automatically removes the ghost
  entities without manual intervention.
- **Migration:** HA entity_ids `sensor.mailbox_receiver_{rssi,snr,last_seen,freq_error,packet_loss}`
  become `sensor.mailbox_sender_*` after OTA flash. Update any dashboard cards that
  reference the old entity_ids.
- Receiver entities that stay as `receiver_*`: `online`, `wifi_rssi`, `uptime`, `reboot`.

### V2.1.1 — 2026-06-02
- **BUG FIX** — Reboot button `unique_id` in MQTT discovery was `"receiver_reboot"` but
  HA assigned `button.mailbox_reboot_receiver` (slug of the name "Reboot receiver").
  Changed unique_id to `"reboot_receiver"` to match. Old retained config topic
  `homeassistant/button/receiver_reboot/config` added to `clearOldDiscovery()`.

### V2.5.0 — 2026-06-07
- OLED line2 now shows `Bat:85% rssi-75 AES` (encrypted sender) or
  `Bat:85% rssi-75 ---` (legacy plaintext sender). Voltage removed to make room.
  `lastPkt.encrypted` flag is set from the raw `0xAE` magic byte before decryption.

### V2.4.0 — 2026-06-06
- AES-128-CTR decryption support (receiver side).
  Encrypted packets are identified by magic byte `0xAE` in position 0, followed
  by a 4-byte IV seed `[seq, boot_lo, boot_hi, 0x00]` and the ciphertext.
  Decryption uses mbedTLS (built into Arduino-ESP32) with pre-shared key
  `LORA_AES_KEY` from `arduino_secrets.h`.
  Legacy plaintext packets (first byte ≠ `0xAE`) are passed through unchanged,
  so the receiver can be OTA-flashed before the sender is upgraded — no service
  gap during the transition. Sender-side encryption (V2.3.0) requires a
  separate trip to the mailbox.

### V2.3.0 — 2026-06-05
- New sensor: estimated days of battery life remaining (`mailbox/sender/battery_days`).
  Computed in the receiver from the sender's vbat using the same piecewise LiPo curve
  as `batteryPercentString()`, scaled by 7.33 days/percent (2000 mAh total capacity
  ÷ 2.73 mAh/day average drain — circuit consumption + self-discharge combined).
  New HA discovery entity: `sensor.mailbox_sender_battery_days` (24th entity, diagnostic).

### V2.2.0 — 2026-06-03
- **NEW** — Last-mail-at sensor: receiver records the NTP timestamp when state
  transitions to MAIL and publishes it retained to `mailbox/last_mail_at`.
  HA entity `sensor.mailbox_last_mail_at` (device_class timestamp) auto-renders
  "X hours ago" / "X days ago". Also published when a deferred MAIL event is
  flushed on MQTT reconnect.
- **NEW** — CRC decode error counter: RadioLib `RADIOLIB_ERR_CRC_MISMATCH`
  responses increment `crcErrorCount`, published retained to
  `mailbox/receiver/crc_errors` (total_increasing, diagnostic).
  Lets you distinguish interference (CRC failures) from sender out-of-range
  (seq gaps in `sender_packet_loss`).
- Entity count: 21 → 23.

### V2.1.2 — 2026-06-03
- Skill test

### V2.0.0 — 2026-06-02
- Version milestone bump to align with V2.0.0 sender. No firmware changes.
  Documentation restructured: README.md is the canonical reference, HARDWARE.md moved
  to repo root, planning docs removed.

---

## Node-RED flows

### V2.2.0 — 2026-06-06
- `Node-RED_battery_low.txt`: 7-day repeat reminder while battery stays low.
  If the sender remains in low-battery heartbeat mode, Pushover re-fires every
  7 days until a normal heartbeat arrives. Uses Node-RED flow context
  (`battLowActive` flag) to track state across packets.

### V2.1.0 — 2026-06-03
- **NEW** — `Node-RED_no_mail_alert.txt`: fires Pushover "Ei postia N päivään"
  (priority 1, sound falling) if `mailbox/last_mail_at` is older than 7 days.
  Checks daily at 09:00 server time via cron inject node. Requires receiver
  firmware V2.2.0+ for the `mailbox/last_mail_at` retained topic.

### V2.0.0 — 2026-06-03
Initial versioned release. Version history is embedded as a comment node inside each
file and visible in the Node-RED editor.

- `Node-RED_mail_arrived.txt`: fixed RSSI subscription from `mailbox/receiver/rssi`
  → `mailbox/sender/rssi` (firmware V2.1.0 reclassified RSSI as a sender metric).
- `Node-RED_battery_low.txt`: no functional changes.
- `Node-RED_sender_boot.txt`: no functional changes.
