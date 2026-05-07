## V1.0.0 — 2026-05-02 — mailbox_receiver_V3 design plan (no code yet)

# `mailbox_receiver_V3.ino` — Design plan

Planning document for the Heltec WiFi LoRa 32 V3 receiver firmware. Target file: `O:\Claude\Mailbox notifier\mailbox_receiver_V3.ino` (project root). Existing baseline: `Old Receiver sketches/mailbox_receiver_V2_real.ino` (works, but minimal).

---

## 1. The data the sender CAN send (full menu)

Everything the Feather 32u4 LoRa + BME280 + reed + LiPo combo could put on the wire. The receiver doesn't have to forward all of it — pick what's useful.

### 1.1 Core mailbox signals (must-have)

| Field | Type | Range / unit | Notes |
|---|---|---|---|
| **Packet type** | uint8 | `0x01`=reed event, `0x02`=heartbeat, `0x03`=boot, `0x04`=test | Lets HA differentiate "mail arrived" from a routine ping |
| **Sequence counter** | uint8 | 0–255, wraps | Receiver detects gaps → "X packets lost" |
| **Battery voltage** | uint16 | mV (e.g. 3920 = 3.92 V) | Convert to % at receiver from LiPo discharge curve |
| **Reed state at TX** | uint8 (bit) | 0 = closed, 1 = open | On heartbeat tells you the lid is currently open right now |

### 1.2 Environment sensors (worth sending — BME280 in cabinet)

| Field | Type | Range / unit | Notes |
|---|---|---|---|
| **Temperature** | int16 | °C × 100 (so 2143 = 21.43 °C) | BME280 ±1 °C accuracy. Useful for cold-weather battery alerts |
| **Humidity** | uint8 | % RH (0–100) | BME280 ±3 %. Cheap to send |
| **Pressure** | uint16 | hPa × 10 (so 10132 = 1013.2 hPa) | New with BME280. Local barometer for fun, can correlate with weather automations |

### 1.3 Diagnostics (cheap to add, useful for debugging)

| Field | Type | Range / unit | Notes |
|---|---|---|---|
| **Boot count** | uint16 | 0–65535 | Stored in EEPROM, ++ each cold boot. Detects unexpected resets / brown-outs |
| **Uptime since boot** | uint32 | minutes | 32u4 millis() rolls every 49 days; sender stores in EEPROM as minute counter |
| **Reed event count** | uint16 | 0–65535 | Total mail events since last reset. Sanity-check against HA history |
| **Charging state** | uint8 (bit) | 0 = not charging, 1 = charging | Adafruit Feather has a CHG pin — useful only if you ever add solar/USB to the mailbox. Default no |

### 1.4 Computed at the receiver (no airtime cost)

| Field | Source | Notes |
|---|---|---|
| **RSSI** | `radio.getRSSI()` | dBm of received packet — direction-independent link quality |
| **SNR** | `radio.getSNR()` | dB — better than RSSI for marginal links |
| **Frequency error** | `radio.getFrequencyError()` | Hz — detects sender crystal drift over winter cold |
| **Receive timestamp** | NTP via WiFi | "last seen at HH:MM" on OLED + HA `last_seen` sensor |
| **Packet loss %** | seq counter gaps | rolling window of last 100 packets |
| **Battery percent** | LiPo curve from voltage | piecewise: 4.20=100, 3.85=50, 3.60=20, 3.30=0 |

### 1.5 What I'd drop

| Field | Why skip |
|---|---|
| Free RAM on sender | 32u4 has 2 KB — nothing useful varies |
| LoRa airtime totals | Diagnostic fluff; you can measure from RSSI + count |
| GPS coordinates | Mailbox doesn't move (and there's no GPS module on the bench) |
| Min/max temp since last TX | Adds 4 bytes + EEPROM bookkeeping for marginal value — pressure already gives you weather context |

---

## 2. Packet format options

Three honest options. All assume LoRa hardware CRC is enabled (RadioLib does this by default).

### Option A — Keep CSV, extend it
Today's sender sends `t;h;Vbat;count`. V3 would extend to:

```
AA;1;42;21.43;67;1013.2;3920;1;0
│  │ │  │     │  │      │    │ └─ flags (bit field)
│  │ │  │     │  │      │    └─── reed state
│  │ │  │     │  │      └──────── battery mV
│  │ │  │     │  └─────────────── pressure hPa
│  │ │  │     └────────────────── humidity %
│  │ │  └──────────────────────── temperature °C
│  │ └─────────────────────────── sequence
│  └───────────────────────────── packet type
└──────────────────────────────── device ID
```

**Pros:** human-readable on OLED + Serial, parser stays simple, V2_real-style code, easy to debug. **Cons:** ~33 bytes (vs 14 binary) → ~190 ms airtime SF9/250 kHz vs ~123 ms. Order-dependent — adding a field later means renumbering.

### Option B — Key=value (URL-encoded style)

```
t=21.43&h=67&p=1013.2&v=3920&r=1&s=42&type=1&id=AA
```

**Pros:** human-readable, self-describing (order doesn't matter, can drop fields), forward-compatible (receiver ignores unknown keys). **Cons:** ~50 bytes (worst), parser is slightly more work.

### Option C — Binary packed struct

```c
struct __attribute__((packed)) Packet {
  uint8_t  device_id;     // 0xAA
  uint8_t  packet_type;   // 1=reed, 2=heartbeat, 3=boot
  uint8_t  seq;
  int16_t  temp_c100;     // °C * 100
  uint8_t  humidity_pct;
  uint16_t pressure_hpa10; // hPa * 10
  uint16_t vbat_mv;
  uint8_t  reed_state;    // 0 or 1
  uint8_t  flags;         // bit field for charging, low-batt, etc
}; // 12 bytes total
```

**Pros:** smallest airtime (~110 ms), lowest sender power, room for many more sends per duty-cycle window. **Cons:** receiver must parse with matching struct — version-skew between sender/receiver firmware will produce nonsense. Endianness needs care. Not human-readable on OLED without formatting.

### Recommendation
**Option B (key=value).** Penny per packet of extra airtime is negligible (you send ~5 packets/day), and the forward-compatibility wins matter more — you can add fields without coordinated reflashing of both sides. The sender just adds `&new_field=X`, the receiver forwards keys it knows about and ignores unknowns. Human-readable on the OLED for free debugging.

(If you'd rather minimize battery drain absolutely — Option C. The savings are real but small at this duty cycle.)

---

## 3. Receiver feature list (priority-tagged)

Build the V3 receiver in three priority tiers. Tier 1 must work; tier 2 is "make it good"; tier 3 is "extras if there's time."

### Tier 1 — Required for V3 to ship

- **Versioned header + section banners** per coding-style rule.
- **Drop the dead `<LoRa.h>` include** that V2_real has at the top.
- **Remove `while (!Serial)`** so it boots without USB.
- **MQTT auto-reconnect with backoff** instead of `while(1)` halt — reconnect every 5s up to 1 min, then back off to 5 min.
- **WiFi auto-reconnect** — same pattern.
- **Hardware watchdog** — 30 s WDT, kicked from `loop()`. Recover from any hang.
- **Parse Tier-1.1 + Tier-1.2 fields** (temp, humidity, pressure, battery, reed, seq, type) and publish each to its own MQTT topic.
- **Sticky `mailbox/state`** retained; cleared by HA dashboard.
- **Subscribe to clear topic** — receiver listens for an "EMPTY" message on `mailboxstatus/switch` (the topic your existing dashboard button publishes to) and resets `mailbox/state`. No HA-side YAML change needed.
- **OLED status:** WiFi/MQTT icons, big "MAIL" or "—" indicator, last packet info row.

### Tier 2 — "as good as possible"

- **MQTT discovery** — receiver publishes `homeassistant/sensor/.../config` for each entity on every boot. HA self-creates the device + entities. Means you could delete the manual sensor YAML you may have in `configuration.yaml` (one source of truth). Big maintainability win.
- **Last Will & Testament** — receiver tells the broker `mailbox/receiver/online = false` if it disconnects unexpectedly → HA shows "receiver offline" instead of stale data.
- **Sender-alive heartbeat detector** — if no packet for >12 h (heartbeat-interval × 2 + slack), publish `mailbox/sender_alive = false`. HA can alert "battery dead?".
- **Battery percent calculation** — applied at receiver from LiPo curve, published as a separate sensor.
- **Boot count + uptime + WiFi RSSI + free heap** — receiver self-diagnostics, all `entity_category: diagnostic` so they hide by default.
- **NTP time sync** + `mailbox/last_seen` ISO timestamp.
- **Loss percentage** rolling over last 100 packets.
- **OLED display timeout** — turn the panel off after 10 min idle, wake on user button. Saves OLED life and looks tidier.
- **User-button actions** on the Heltec PRG button: short = wake display, long = manually clear `mailbox/state` (in case HA is down).

### Tier 3 — Optional polish

- **Arduino OTA** — flash receiver from your laptop without touching it physically. ~20 lines, requires the `ArduinoOTA` library.
- **Web status page** — tiny built-in HTTP server showing the OLED contents in a browser. Useful, but the OLED is right there.
- **Buzzer output** — you have a 3V buzzer in the cabinet. Wire it to a free GPIO and beep on mail event. Optional in-house alert that doesn't need your phone.
- **Encrypted LoRa packet** — RadioLib supports AES-128 with a shared key in flash. Spoofing a mailbox sensor is low-stakes but it's a clean exercise.
- **HA service-call pass-through** — if the dashboard publishes `mailbox/cmd/reboot`, the receiver reboots itself. Handy for remote recovery.
- **Frequency-error logging to HA** — long-term plot of sender crystal drift vs temperature is a fun side-project.

---

## 4. MQTT topic + HA entity layout

Proposed topic structure under `mailbox/`:

| Topic | Direction | Type | HA entity | Notes |
|---|---|---|---|---|
| `mailbox/state` | RX → HA | retained | `binary_sensor.mailbox_state` (occupancy) | "MAIL" or "EMPTY" — sticky |
| `mailboxstatus/switch` | HA → RX | n/a | (existing dashboard button) | RX subscribes to this; "OFF" clears state |
| `mailbox/lid` | RX → HA | not retained | `binary_sensor.mailbox_lid` (door) | Reed state at TX time |
| `mailbox/temp` | RX → HA | not retained | `sensor.mailbox_temp` (°C, temperature) | |
| `mailbox/humidity` | RX → HA | not retained | `sensor.mailbox_humidity` (%, humidity) | |
| `mailbox/pressure` | RX → HA | not retained | `sensor.mailbox_pressure` (hPa, pressure) | |
| `mailbox/battery_voltage` | RX → HA | retained | `sensor.mailbox_battery_v` (V, voltage) | |
| `mailbox/battery_percent` | RX → HA | retained | `sensor.mailbox_battery_pct` (%, battery) | computed at RX |
| `mailbox/msg_count` | RX → HA | retained | `sensor.mailbox_msg` | |
| `mailbox/rssi` | RX → HA | retained | `sensor.mailbox_rssi` (dBm, signal_strength) | diagnostic |
| `mailbox/snr` | RX → HA | retained | `sensor.mailbox_snr` (dB) | diagnostic |
| `mailbox/last_seen` | RX → HA | retained | `sensor.mailbox_last_seen` (timestamp) | NTP-stamped on RX |
| `mailbox/sender_alive` | RX → HA | retained | `binary_sensor.mailbox_sender_alive` | timeout-based |
| `mailbox/receiver/online` | RX LWT | retained | `binary_sensor.mailbox_receiver_online` | LWT-managed |
| `mailbox/receiver/wifi_rssi` | RX → HA | not retained | `sensor.mailbox_receiver_wifi` | diagnostic |
| `mailbox/receiver/uptime` | RX → HA | not retained | `sensor.mailbox_receiver_uptime` | diagnostic |

**Discovery:** all entities published once at receiver boot to `homeassistant/<platform>/<unique_id>/config` with the right `device_class`, `unit_of_measurement`, `state_class`, `entity_category`, and a shared `device` block grouping them under one HA device card called "Mailbox sensor."

**Backwards-compat:** keep publishing the old `mailboxstatus/feather` JSON blob too, so anything in your existing HA setup that reads it doesn't break. Mark it deprecated in a comment; remove in a later version.

---

## 5. OLED layout proposal

Heltec V3's OLED is 128×64. Compact, but enough.

```
┌──────────────────────────┐
│ ⌬ MQTT  📶 -64dBm  10:42 │   ← top status row, font 1
│                          │
│   ╔══════════════════╗   │
│   ║      MAIL!       ║   │   ← big state, font 2 or 3
│   ╚══════════════════╝   │
│                          │
│ Last: 21.4°C 67% 1013    │   ← latest sensor row
│ Bat: 3.92V (78%)         │
│ RSSI -84 SNR 9.5  #42    │
└──────────────────────────┘
```

When state is `EMPTY` the big middle box becomes "—" and the status rows are dimmer. After 10 min idle the OLED powers off; press PRG to wake.

Boot screen for the first 2 s: version string + IP address (matches the splash convention from your ESP32 Remote-project).

---

## 6. Sender-side impact (what this means for the Feather sketch)

You're not asking for sender code today, but the receiver design assumes specific sender changes — listing them here so we don't forget.

> **Wiring details have moved to a dedicated reference: `SENDER_HARDWARE.md`.** That document is the single source of truth for the pin map. The summary below is the firmware-level impact only.

### 6.1 BME280 sensor swap (firmware impact)

- I2C bring-up at the start of `setup()` (`Wire.begin()`).
- Replace DHT22 read with BME280 forced-mode read (~10 ms vs 2 s).
- New fields in the packet: pressure (hPa × 10).
- Reed pin moves from D3 → **D0 (INT2)** with **internal pull-up** and **inverted reed mounting** (NO reed; reed CLOSED only when lid is OPEN). No external resistors. Average idle current ~0 µA. See `SENDER_HARDWARE.md` for the mounting diagram.
- Blue debug LED on D5: **blinks once (~100 ms) after every LoRa TX** as visual confirmation the radio fired. Wire stays in field, firmware-gated by `#define DEBUG_LED 1/0`.

### 6.2 BME280 read sequence

- Wake from sleep
- Power up I2C bus (`Wire.begin()`)
- Trigger BME280 forced-mode read (~10 ms)
- Read temp/humidity/pressure registers
- Build packet (key=value string)
- TX
- `Wire.end()` to release I2C lines
- Re-arm reed interrupt
- Sleep

Total awake time: ~250 ms instead of DHT22's ~2 s. Big battery win.

### 6.3 Other sender-side tweaks (ride-along)

- Wake polarity bug fix (`HIGH` → `LOW` for PWR_DOWN wake).
- Move LoRa `begin()` out of `loop()` into `setup()`.
- Disable ADC before sleep (`ADCSRA &= ~_BV(ADEN);`).
- WDT-based 6 h heartbeat using `WDTCSR` 8 s ticks × 2700.
- Disable LiPo charge LED unless USB is plugged in (saves ~3 mA continuous).
- Switch CSV → key=value packet format.

---

## 7. Decisions locked (2026-05-02)

| # | Question | Choice |
|---|---|---|
| 1 | Packet format | **Key=value** (Option B) |
| 2 | MQTT discovery | **Yes** — receiver self-publishes `homeassistant/.../config` on boot |
| 3 | Heartbeat interval | **24 h** |
| 4 | Tier 3 features | **OTA only** — buzzer / AES / web / HA-reboot deferred |

**Implication of 24 h heartbeat:** receiver's `sender_alive` timeout must be 48–72 h (not 12 h, which was based on the 6 h heartbeat default). HA alert "sender dead?" will fire ~2 days after a real outage rather than half a day. Acceptable for a once/day mail event use case.

## 8. Final scope locked (2026-05-02)

| # | Question | Choice |
|---|---|---|
| 5 | BME280 module | 6-pin variant. Wire CSB → 3V3 (I2C mode), SDO → GND → I2C address **`0x76`**. |
| 6 | Old MQTT topic compat | **Keep both old + new for one release.** V3 publishes new `mailbox/*` AND keeps `mailboxstatus/feather` + `mailboxstatus/switch`. Cull old in V3.1. |
| 7 | Low-battery heartbeat boost | **Yes.** Heartbeat cadence is 24 h normally, but jumps to 6 h whenever `vbat < 3.6 V`. Early dead-battery warning at near-zero average airtime cost. |

### Implication on sender packet `type` field

With the boosted-heartbeat behaviour, packet types become:
- `0x01` reed event (mail arrived)
- `0x02` heartbeat (24 h cadence — battery healthy)
- `0x03` heartbeat low-battery (6 h cadence — vbat < 3.6 V)
- `0x04` first boot

HA can use the `type` field to fire different alerts: `0x01` → push notification, `0x03` → "replace battery soon" notification.

---

## 9. Notification flow — Node-RED → Pushover → iPhone

The existing Node-RED flow (`Node-Red_code.txt`) is wired to the **wrong MQTT topic** — it subscribes to `arduino/mailbox/switch`, which only the abandoned `Mailbox_receiver.ino` published to. The V2_real receiver publishes to `mailboxstatus/switch`. So **the Pushover notifications haven't actually been firing** when V2_real was running. Worth re-flashing the topic in the Node-RED node after V3 is up.

### 9.1 Existing flow (today)

```
[mqtt in: arduino/mailbox/switch] ──► [debug]
                                  ──► [switch ON/OFF] ──► (ON) ──► [change: "Postia laatikossa!"] ──► [pushover: iPhone, "Ilmoitus", siren, prio 0]
                                                          (OFF) ──► (nothing)
```

Pushover details: device=`iphone`, title=`Ilmoitus`, priority=`0`, sound=`siren`, message=`Postia laatikossa!`.

### 9.2 Proposed V3 flow

The receiver V3 will publish multiple topics, not just one. Different events warrant different alerts. Two layouts to choose from:

**Option A — One subscription, switch on packet type (simpler)**

Subscribe to `mailbox/state` (sticky). When it transitions `EMPTY → MAIL`, fire Pushover. Use a **trigger** node to deduplicate within 60 s (belt-and-braces against bouncy reed even if the sender debounce fails).

```
[mqtt in: mailbox/state] ──► [filter: only on change] ──► [switch by payload]
                                                            (MAIL) ──► [trigger: 60s block] ──► [function: build msg w/ sensor data] ──► [pushover: "Postia laatikossa!"]
                                                            (EMPTY) ──► (nothing)

[mqtt in: mailbox/sender_alive] ──► [filter: only on change to "false"] ──► [pushover: "Postilaatikon lähetin offline"]
[mqtt in: mailbox/battery_percent] ──► [filter: < 15%] ──► [trigger: 24h block] ──► [pushover: "Akku heikko: X%"]
[mqtt in: mailbox/receiver/online] ──► [filter: only on change to "false"] ──► [pushover: "Vastaanotin offline"]
```

Pushover messages can pull live sensor values into the body via Node-RED context — e.g. "Postia laatikossa! 7.2 °C, akku 78 %." Useful in winter.

**Option B — Subscribe to `mailbox/+` and route by packet type field (more flexible)**

Receiver publishes the packet `type` (`0x01` / `0x02` / `0x03` / `0x04`) on a separate `mailbox/last_event_type` topic. Node-RED routes by type, so adding new packet types later doesn't need new subscriptions.

**Recommendation: Option A.** Simpler, reads naturally, the 4 alert categories above cover everything we care about.

### 9.3 Pushover priority + sound suggestions

| Event | Priority | Sound | Title |
|---|---|---|---|
| Mail arrived (`type=0x01`) | 0 (normal) | `siren` (current) — or `magic` for less-dramatic | "Postia laatikossa!" |
| Sender low-battery first detection | 1 (high) | `falling` | "Postilaatikon akku heikko" |
| Sender offline > 48 h | 1 (high) | `falling` | "Postilaatikon lähetin offline" |
| Receiver offline (LWT) | 1 (high) | `falling` | "Vastaanotin offline" |
| Heartbeat / boot / state cleared | — | — | (no notification, log only) |

Priority 0 = quiet hours respected. Priority 1 = bypasses quiet hours but no acknowledgement required. Priority 2 (emergency, requires ack) is overkill for this.

### 9.4 Backwards-compat

Keep the current Node-RED node listening on `arduino/mailbox/switch` and `mailboxstatus/switch` as additional inputs to the same trigger pipeline during the V3 transition. Removes risk of "nothing notifies" between flashes.

---

## 10. Reed-switch debounce — three layers

The mailbox lid bounces / cycles when the postman uses it. A single mail event can trip the reed switch 2–5 times within ~5 seconds. We want **one notification per opening**.

Defence in depth — apply the dedup at **all three** stages. They're cheap and they protect against different failure modes.

### 10.1 Layer 1 — Sender (battery-saving) — 60 s lockout window

After the sender wakes on a reed event and sends the LoRa packet, it enters a **60 s lockout** during which further reed wakes are ignored. Then it rearms the reed interrupt and goes back to deep sleep waiting for the next opening.

Two implementation styles:

**Style A — Sleep through the lockout with WDT wake**
- Detach reed interrupt
- Sleep with WDT 8 s ticks × 8 (= 64 s)
- Re-attach reed interrupt
- Sleep waiting for reed
- **Power cost: ~30 mAs per event** (mostly the radio TX)
- Simpler power profile

**Style B — Stay awake polling for 60 s**
- Disable reed interrupt
- `delay(60000)` (chip awake)
- Re-enable interrupt, sleep
- **Power cost: ~300 mAs per event** (5 mA × 60 s awake)
- 10× more battery use per event

**Recommend Style A** — same behaviour, 10× the battery life.

What it costs you: if the postman comes back exactly within 60 s of his first visit (very unusual), the second visit is missed. Acceptable trade-off.

### 10.2 Layer 2 — Receiver (defensive) — duplicate sequence ignore

The sender's packet carries a sequence counter. Receiver remembers the last seq and:
- **Ignores any packet with the same seq + type** as the previous one (LoRa CRC means duplicates shouldn't happen, but if RadioLib retries internally we'd notice).
- **Rejects new "MAIL" state if `mailbox/state` is already MAIL** within 60 s of the previous one — even if sender debounce somehow failed.

Cost: ~10 lines of state in the receiver.

### 10.3 Layer 3 — Node-RED (visible knob) — Trigger node 60 s block

The Pushover branch passes through a **Trigger** node configured: "send first message immediately, block subsequent messages for 60 s." This means even if both sender and receiver layers misbehave, you don't get spammed.

Bonus: this knob is editable in the Node-RED UI without re-flashing anything. If the postman ever causes a false double-alert, you can tweak it to 90 s with one click.

### 10.4 Power-budget impact of the lockout

The sender currently sends roughly 1 mail event/day + 1 heartbeat/day = 2 packets × 30 mAs = 60 mAs/day TX cost.

With the 60 s lockout:
- Mail event: 30 mAs (unchanged) + 60 s of WDT-sleep (~30 mAs at ~0.5 mA average) = 60 mAs total
- Heartbeat: unchanged
- **Daily TX/wake budget: ~90 mAs/day**

On a 2000 mAh LiPo (= 7.2 M mAs), with sleep current dominating at ~500 µA (43 mAs/day), total is ~135 mAs/day → **~150 days battery life**. Lockout doesn't materially affect this — sleep current is the dominant term.

### 10.5 Why lockout, not classic 50 ms hardware debounce

Mechanical reed switches and tilt-events typically settle in milliseconds, but in a mailbox we're not protecting against contact bounce — we're protecting against **the lid swinging back open** as the postman drops mail in or as wind catches it. That's a multi-second physical phenomenon. Hardware RC debounce won't help; we need a software lockout sized to the real event.

---

## 11. Updated decision summary

Adding to §7 / §8:

| # | Question | Choice |
|---|---|---|
| 8 | Reed lockout window | **60 s** |
| 9 | Lockout implementation | **WDT-sleep through it** (Style A) — saves ~270 mAs per event vs awake-poll |
| 10 | Notification deduplication | **All three layers** — sender lockout + receiver state-already-MAIL guard + Node-RED Trigger node |
| 11 | Pushover priorities | Mail event = priority 0 (siren), all alerts (low batt, offline) = priority 1 (falling) |
| 12 | Node-RED transition | Keep current node listening on the old `arduino/mailbox/switch` and `mailboxstatus/switch` topics during V3 flash, add `mailbox/state` as new input. Cull old after V3 verified. |
| 13 | Pushover mail sound | **'magic'** (or 'pushover') — pleasant chime. Siren reserved for nothing (the alert tier uses 'falling' instead). |
| 14 | Pushover mail body | `Postia laatikossa! <temp> °C, akku <battery_pct>%` — Node-RED change/template node pulls `mailbox/temp` and `mailbox/battery_percent` from flow context. |
| 15 | Auto-clear sticky state | **None.** Manual only — HA dashboard button or Heltec PRG long-press. |
