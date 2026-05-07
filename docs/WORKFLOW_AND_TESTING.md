## V1.0.0 — 2026-05-01 — Mailbox notifier workflow & testing plan

# Mailbox Notifier — Workflow & Testing Plan

End-to-end plan from "what's on the bench today" to "the mailbox pings my phone via Home Assistant when the postman opens the lid." Each phase has an entry condition, work items, and an acceptance test. Don't move on until the acceptance test passes.

The phases assume the hardware shown in the project photos:
- **Sender:** Adafruit Feather 32u4 LoRa (RFM95, SX1276, 868 MHz) + 2000 mAh LiPo + reed switch + DHT22.
- **Receiver:** Heltec WiFi LoRa 32 V3 (ESP32-S3 + **SX1262** + OLED) on USB power in the house.
- **Backend:** Home Assistant on `192.168.15.0/24`, Mosquitto MQTT broker at `192.168.15.100`.

---

## Known issues to fix as you go

The state of the existing code, before any of this plan is executed:

1. **Receiver sketch is mismatched to the board.** It includes `<ESP8266WiFi.h>` (wrong family — Heltec V3 is ESP32-S3), uses `LoRa.h` (Sandeep Mistry — only drives SX127x, will not talk to the SX1262 on Heltec V3), and the pin assignments inside (CS=5, RST=14, IRQ=2, OLED 0x3D, LED=0, button=12) are leftovers from a different board. **Treat the receiver as a near-rewrite.** Correct radio chipset + library: SX1262 driven via **RadioLib** or **Heltec_ESP32_LoRa_v3** library. Correct pins: NSS=8, MOSI=10, MISO=9, SCK=11, RST=12, BUSY=13, DIO1=14; OLED at 0x3C on SDA=17/SCL=18 with RST=21; user LED on 35; PRG button on 0; VBAT on 1.
2. **Receiver does not parse incoming LoRa packets.** It currently only publishes "ON" to MQTT when its local pushbutton is pressed. The dashboard expects MailboxTemp/Humidity/Lipo/Msg/RSSI/SNR — nothing publishes those today.
3. **MQTT topic mismatch.** Sketch publishes `arduino/mailbox/switch`; HA dashboard publishes the reset to `mailboxstatus/switch`. Pick one prefix per topic and stick to it (`mailbox/<entity>` recommended) — and add the `homeassistant/.../config` discovery messages so HA auto-creates the entities instead of relying on hand-written YAML.
4. **Sender wake-from-PWR_DOWN polarity is wrong.** `attachInterrupt(..., HIGH)` won't wake an ATmega32u4 from `SLEEP_MODE_PWR_DOWN` — only level-LOW interrupts on INT0/INT1 do. With the reed wired NO + INT pulled-up, the line is HIGH at rest and goes LOW when the magnet leaves the switch — change to `LOW` and re-validate the wake direction (`door open → wake` is what we want).
5. **Sender `loop()` re-`begin()`s the radio each cycle.** That works but wastes ~20 ms and code clarity. Move LoRa init to `setup()`/post-wake only; in `loop()` just `LoRa.idle()` → send → `LoRa.sleep()` → `enterSleep()`.
6. **No CRC / addressing on the LoRa packet.** Sender sends `t;h;Vbat;count` plain. Add `LoRa.enableCrc()` on both ends and prefix the packet with a 1-byte device ID (e.g. `0xAA` from the existing constants) so future neighbours' LoRa traffic can't accidentally trigger the mailbox sensor.

---

## Phase 0 — Decisions to lock before flashing anything

Each of these has a default if you don't want to think about it; the default is the lower-risk option.

| Decision | Options | Default / recommendation |
|---|---|---|
| Receiver LoRa library | RadioLib (multi-chip, well documented) vs Heltec_ESP32_LoRa_v3 (board-tailored, simpler API) | **RadioLib** — broader docs, works on both ends if you ever swap the sender. |
| Trigger semantics | "Mail arrived" = lid opened, OR "Mail arrived" = lid opened AND lid closed again | **Lid opened** (single edge) for V1; closure-debounce in V2. |
| Notification cadence in HA | Single ping per opening, OR sticky "Mail waiting" until the user clears it | **Sticky** — the receiver publishes `mailbox/state = MAIL`, the HA mailbox button you already have publishes `mailbox/state = EMPTY` to clear. |
| Discovery | Hand-write HA YAML sensors, OR publish MQTT discovery from the receiver | **MQTT discovery** — entities self-create, you'll never need to hand-edit `configuration.yaml` again. |
| Sender deep sleep target | "Wake on reed only" (lowest power) vs "Wake on reed + heartbeat every N hours" (lets HA detect a dead sender) | **Reed + 6 h heartbeat** — adds ~4 TX/day, negligible vs reed event but gives you "low battery" visibility. |
| Sender LoRa params | Match existing (SF9, BW 250 kHz, +20 dBm) vs tighter range/lower power | Keep current — there's plenty of link budget at 50 m and SF9/250k gives ~200 ms airtime. |

If any of these defaults don't match what you want, change them now. Everything downstream depends on them.

---

## Phase 1 — Bench: get the link working at 30 cm

**Entry condition:** both boards on USB on the same desk, antennas attached.

Work items:
1. **Receiver:** new sketch, RadioLib + Heltec V3 pinout. Loop just listens and prints `[RX] <hex bytes> rssi=… snr=…` to Serial and the OLED. WiFi/MQTT not in yet.
2. **Sender:** strip the existing sketch down — remove sleep, send `"HELLO %d"` once a second with `msgCount` increment. Verify the existing `LoRa.setSpreadingFactor(9)` / `setSignalBandwidth(250E3)` / `setTxPower(20)` settings on both ends.
3. Confirm bidirectional **frequency, SF, BW, sync word** — these MUST match. Mismatch is the #1 cause of "the radios are fine but I see nothing."

**Acceptance test (Phase 1):** Receiver Serial shows `HELLO 0`, `HELLO 1`, … incrementing once per second with stable RSSI > −60 dBm at 30 cm distance. Same string visible on the OLED.

---

## Phase 2 — Bench: real packet format + reed trigger

**Entry condition:** Phase 1 passed.

Work items:
1. **Sender:** restore the real payload. Header `[0xAA][seq:1][reason:1][len:1][payload…]` where `reason` is `0x01 = reed`, `0x02 = heartbeat`, `0x03 = test`. Payload is binary little-endian: `int16 t_c10` (temp × 10), `uint8 h_pct`, `uint16 vbat_mv`, `uint16 msg_count`. **Don't ship CSV strings** — you're paying for every byte of airtime, twice (TX power, and time-on-air which limits duty cycle in the EU 868 band).
2. **Receiver:** parse the same header. On valid packet, print to OLED and Serial (`MAIL #N · 21.4 °C · 67 % · 3.92 V · −68 dBm · SNR 9.5`).
3. Wire reed switch on sender to interrupt pin (INT0 = pin 3 on Feather 32u4, with internal `INPUT_PULLUP`). Confirm one TX per magnet-removal event using `Serial` while still on USB (sleep stays disabled this phase).

**Acceptance test (Phase 2):** moving the magnet away from the reed switch produces exactly one packet on the receiver within 200 ms; values match a thermometer/hygrometer in the room within ±1 °C / ±5 %; battery voltage matches a multimeter reading within ±50 mV; sequence counter is monotonic.

---

## Phase 3 — Sender deep sleep + battery measurement

**Entry condition:** Phase 2 passed.

Work items:
1. Re-enable `SLEEP_MODE_PWR_DOWN` on the sender. **Fix the wake polarity** (`LOW`, not `HIGH` — see Known Issues #4). Disable ADC (`ADCSRA &= ~_BV(ADEN);`) before sleeping, re-enable after wake — saves ~200 µA.
2. Add a 6-hour heartbeat using the watchdog timer (`WDTCSR` 8 s ticks → counter, see "AVR sleep with WDT wake" pattern). On heartbeat, send a `reason=0x02` packet so the receiver sees the sender is alive even if no mail arrives.
3. Move LoRa `begin()` into `setup()` only; on wake, just `LoRa.idle()` → `beginPacket()` → … → `LoRa.sleep()`.
4. Battery measurement: existing A9 divider math is correct for Adafruit Feather (`measuredvbat *= 2 * 3.3 / 1024.0`). Sample 8× and average — 32u4 ADC noise is real.

**Acceptance test (Phase 3):**
- With reed open and idle: **measured current ≤ 1 mA average** (LiPo charger, 32u4 sleeping, RFM95 in `LoRa.sleep()`). Aim is ~300–500 µA realistically — the 32u4's USB regulator is the main offender.
- Reed event from sleep produces a valid packet within 500 ms.
- Heartbeat fires within ±5 % of 6 h (8 s × 2700 WDT ticks).
- Battery voltage reading matches multimeter ±50 mV across a 4.0 V → 3.5 V battery range.

> **Reality check on battery life:** 2000 mAh LiPo, average draw 500 µA → ~167 days. Average draw 2 mA (if the 32u4 USB block leaks more than expected) → ~42 days. If you measure >5 mA in sleep, something is wrong — most likely the DHT22 (which idles at ~0.5 mA constantly) or the LiPo charger LED. Power-gate the DHT22 from a GPIO if needed.

---

## Phase 4 — Receiver: MQTT + Home Assistant discovery

**Entry condition:** Phase 3 passed; receiver still printing parsed packets to Serial/OLED.

Work items:
1. WiFi connect with `WiFiManager` (or hard-coded from `arduino_secrets.h`), then MQTT connect to `192.168.15.100:1883` with `MQTT_User`. Maintain reconnect loop.
2. On boot, publish **MQTT discovery configs** to `homeassistant/sensor/mailbox_temp/config`, `_humidity`, `_lipo`, `_msg`, `_rssi`, `_snr`, and `homeassistant/binary_sensor/mailbox_state/config` (device_class: `occupancy` or `door`). Each payload includes `state_topic`, `unit_of_measurement`, `device_class`, and `device` block so they all group under one HA device. Discovery happens once per boot — HA caches.
3. On received LoRa packet, publish to `mailbox/temp`, `mailbox/humidity`, `mailbox/lipo`, `mailbox/msg`, `mailbox/rssi`, `mailbox/snr`. For the reed-trigger packet, also publish `mailbox/state = MAIL` (retained).
4. Subscribe to `mailbox/state/set` — the existing HA dashboard button already publishes here in spirit; map it so a press from HA publishes `EMPTY` to `mailbox/state` (retained), clearing the indicator.
5. OLED layout: top line `WiFi/MQTT` status icons, big middle "MAIL" / "—", bottom line last packet timestamp + RSSI/SNR.

**Acceptance test (Phase 4):**
- Reboot the receiver → all 7 entities appear in HA with the right device grouping, no `configuration.yaml` edits needed.
- Trigger the reed → within 2 s HA shows `binary_sensor.mailbox_state = on` and the temp/humidity/lipo update.
- Tap the existing "Mailbox" button on the HA dashboard → `binary_sensor.mailbox_state = off` within 1 s.
- Power-cycle the receiver → it picks the retained `MAIL` state back up correctly.

---

## Phase 5 — Range test in place

**Entry condition:** Phase 4 passed; receiver mounted at its final spot in the house.

Work items:
1. Walk the sender (USB-powered, no enclosure yet) along the 50 m path, sending one packet every 5 s with sequence counter. Log RSSI/SNR + lost packets at the receiver.
2. Worst case to test: sender inside the metal mailbox with the lid closed, antenna in its final orientation. Metal enclosures eat 10–20 dB easily — you may need to route the wire-antenna out through a hole or use a small whip.
3. Do this twice — once in dry weather, once in heavy rain (water on the antenna shifts tuning).

**Acceptance test (Phase 5):**
- Packet loss ≤ 1 % across 100 packets.
- RSSI margin: receiver should show ≥ 15 dB above its sensitivity floor (for SF9/250 kHz, sensitivity is around −127 dBm → target ≥ −112 dBm). If you're tighter than that, drop to SF10 or BW 125 kHz for more margin.

---

## Phase 6 — Field deployment (mailbox)

**Entry condition:** Phase 5 passed.

Work items:
1. Move sender from breadboard to perfboard or solid prototyping board. Keep it socketed (female headers) so the Feather can be removed.
2. **Enclose** in a weatherproof IP65+ box mounted inside or under the mailbox. Reed switch goes on the lid hinge with magnet on the lid (or vice-versa). DHT22 vent to outside air via a small slot with a filter.
3. Battery: full charge (4.20 V) before installing. Note install date and starting voltage.
4. Confirm reed orientation: door open = magnet leaves the switch = reed contacts open = pulled-up pin goes HIGH. **Wake polarity must agree** (Phase 3 fix).

**Acceptance test (Phase 6):** for **7 consecutive days** the mailbox pings HA on each real mail event, the heartbeat appears every 6 h, the lipo voltage curve drops at the expected rate (< 0.05 V/week is healthy), and no events are missed against an independent log (you note each time the postman comes).

---

## Phase 7 — Polish (optional, time-permitting)

- Auto-disable the Feather's onboard charge LED when running on battery only — saves a few mA when the LiPo isn't being charged.
- Power-gate the DHT22 via a GPIO so it only draws current during the ~250 ms read.
- Replace DHT22 with **BME280** (in your parts cabinet) — I2C, lower power, more accurate, also gives barometric pressure.
- Swap the wire antenna for a tuned 868 MHz whip with SMA connector; route it outside the metal mailbox.
- Add a small PV cell + diode to trickle-charge the LiPo (overkill at one event/day, but fun).
- Encrypt the LoRa payload with AES-128 (RadioLib supports it) — overkill for a mailbox sensor but a clean exercise.

---

## Test matrix summary

| # | Test | Tool | Pass criterion |
|---|---|---|---|
| T-LINK | LoRa link bench | Serial monitor | RSSI > −60 dBm @ 30 cm, no loss over 60 packets |
| T-PARSE | Packet decode | Serial / OLED | Temp ±1 °C, humidity ±5 %, vbat ±50 mV |
| T-WAKE | Reed wake | Magnet + Serial | One TX per magnet-remove, ≤ 500 ms latency |
| T-SLEEP | Sleep current | Multimeter (µA) | Idle avg ≤ 1 mA, ideally < 500 µA |
| T-HEART | Heartbeat | 24 h log | One packet every 6 h ± 18 min |
| T-DISC | MQTT discovery | HA "Devices" page | All 7 entities present, grouped, populated |
| T-CLEAR | State clear | HA dashboard button | `binary_sensor.mailbox_state = off` within 1 s |
| T-RANGE | Range in place | Walk test, RSSI log | ≤ 1 % loss, ≥ 15 dB margin over sensitivity |
| T-FIELD | 7-day field | Postman log | 0 missed events, 0 false events, voltage stable |

---

## Things to verify before each board flash

A short paranoia checklist — every flash is a chance to brick something:

1. Right board selected in Arduino IDE? (Heltec WiFi LoRa 32 V3 ≠ Heltec WiFi LoRa 32 V2 ≠ ESP32 Dev Module.)
2. Right port? (USB hubs reorder — check the COM number didn't change.)
3. Sender unplugged from LiPo before plugging USB? (Charging logic handles both, but uploading while running on battery has bricked Feather sketches before — undefined behaviour during reset spike.)
4. `arduino_secrets.h` not opened in another editor that's silently saving placeholder values?
5. Header version bumped on the file you just edited? (Per the coding-style rules.)
