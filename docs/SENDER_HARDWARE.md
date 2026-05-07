## V1.0.0 — 2026-05-02 — Mailbox sender wiring reference

# Mailbox Sender — Hardware & Wiring Reference

Canonical pin map for the mailbox sender after the BME280 swap. This is the document to consult when physically wiring the breadboard / perfboard. Decisions captured here override anything older in `RECEIVER_V3_PLAN.md` §6.

**Board:** Adafruit Feather 32u4 LoRa (RFM95, SX1276, EU 868 MHz)
**Power:** 2000 mAh 3.7 V LiPo via the Feather's JST connector
**Sensor:** BME280 (6-pin GY-BME280 module)
**Trigger:** Normally Open reed switch + magnet on mailbox lid
**Indicator:** Blue LED (debug-only — driven only when `DEBUG_LED` is enabled in firmware)

---

## 1. Full pin table

### On-board / fixed (do not reuse)

> **Reading the silkscreen:** Adafruit labels some Feather 32u4 LoRa pins with functions (RX, TX, SDA, SCL) and others with their plain digital number (5, 6, 9, …). The board has **no "D0" / "D1" labels** — the pin you'd call `D0` in code is silkscreened **"RX"**, and `D1` is **"TX"**. Mapping reference:
>
> | Silkscreen label | Arduino code |
> |---|---|
> | RX | D0 (INT2) |
> | TX | D1 (INT3) |
> | SDA | D2 (INT1) |
> | SCL | D3 (INT0) |
> | 5, 6, 9, 10, 11, 12, 13 | D5, D6, D9, D10, D11, D12, D13 |

| Code pin | Silkscreen | Function | Notes |
|---|---|---|---|
| D4 | (none, internal) | LoRa RST | hardwired on PCB |
| D7 | (none, internal) | LoRa DIO0 (IRQ) | hardwired — **NOT free** for reed |
| D8 | (none, internal) | LoRa CS | hardwired |
| D14 / D15 / D16 (ICSP) | ICSP header | LoRa SPI MISO / SCK / MOSI | shared hardware SPI bus |
| A9 (= D9) | "9" / "BAT" trace | Battery voltage divider | ÷2 divider on board, read via `analogRead(A9) * 2 * 3.3 / 1024` |
| D13 | "13" | On-board red LED | leave alone or use as secondary debug |
| USB D+ / D− | (USB connector) | Native USB | for upload + Serial debug |

### New (BME280 — 6-pin GY-BME280 module)

| Module pin | Feather silkscreen | Notes |
|---|---|---|
| VCC | **3V** | module has on-board 3.3 V LDO; safe with 3V3 (do **not** use BAT or USB rails) |
| GND | **GND** | |
| SDA | **SDA** (= code pin D2) | I2C data |
| SCL | **SCL** (= code pin D3) | I2C clock — was previously the reed pin |
| CSB | **3V** | tie HIGH to force I2C mode (LOW = SPI mode — would brick the bus) |
| SDO | **GND** | sets I2C address to **0x76**. Tie to 3V for 0x77 if 0x76 is ever taken (it isn't on this board) |

> The GY-BME280 module already has 4.7 kΩ pull-ups on SDA/SCL. **Don't add more.**

### Reed switch (NO reed, inverted mounting — locked 2026-05-02)

| Connection | Feather silkscreen |
|---|---|
| Reed terminal 1 | **RX** (= code pin D0, also INT2) |
| Reed terminal 2 | **GND** |
| Internal pull-up | **enabled** in firmware (`pinMode(0, INPUT_PULLUP)`) |
| External resistor | **none** (no pull-up, no pull-down — the AVR internal pull-up does the job) |

**Pin choice:** the pin labelled "RX" on the silkscreen is `D0` in Arduino code, which is `INT2` — a true hardware interrupt that wakes from `PWR_DOWN` on LOW level. RX/D0 is shared with Serial1 RX, but Serial1 is unused on the Feather 32u4 (native USB handles all serial debug), so this is a clean repurposing.

**Behaviour with inverted mounting (the key constraint — see §1.5 below):**
- Lid CLOSED → magnet AWAY from reed → reed OPEN → no path to GND → D0 floats HIGH via internal pull-up → **0 current draw**.
- Lid OPEN → magnet NEAR reed → reed CLOSED → D0 connected to GND → INT2 wakes the chip on LOW level. The internal pull-up (~30 kΩ) flows ~110 µA only while the lid is actually open (typically a few seconds of mail-drop time), then back to 0 µA after lid closes.

Average idle current contribution from the reed wiring: **~0 µA**. Battery life on the 2000 mAh LiPo stays in the ~150-day ballpark.

### 1.5 Reed/magnet mounting — REQUIRED INVERSION

A NO reed switch is **closed when a magnet is near it**. The standard mailbox-sensor mounting puts the magnet on the lid and the reed on the body, aligned when the lid is closed — meaning the reed is closed continuously while the lid is closed, which would wake the chip every cycle and drain the battery.

We need the *opposite*: reed CLOSED only when the lid is OPEN.

Two practical mountings (pick whichever fits your physical mailbox):

**Mounting A — magnet on lid edge, reed inside on top wall**

```
Lid CLOSED (idle, no current):              Lid OPEN (wake event):
                                            
                                                  ╱──── magnet ────────►  ▣ reed
                                                 ╱  (now ~5 mm from reed)
                                                ╱   (reed CLOSED)
   ┌─────────── lid ───────────┐              ╱
   │ ⚫ magnet (front edge)    │             ╱
   ├───────────────────────────┤            ╱  ┌── lid hinge
   │  ▣ reed                   │           ╱   │
   │  (top inside wall)        │  ─────────────────────
   │                           │            
   │   mailbox body            │           Magnet was on the front edge of
   │                           │           the lid; rotating ~90° brings it
   └───────────────────────────┘           up next to the reed in the body.
                                            
   Magnet is ~10 cm away from
   the reed → reed OPEN → D0 HIGH → 0 µA
```

**Mounting B — two-magnet test method (simpler if you don't want to remount yet)**

If you want to bench-prototype before committing to a mounting position, use a separate hand-held magnet for testing. Mount the reed and a permanent "keep-shut" magnet near each other so the reed is closed at rest (i.e. mimic the lid-closed state) — then slide a second cancelling magnet near to "open" the reed for the wake event. Useful only on the bench; not how the field installation works.

**The right test before mounting:** put the reed and the magnet together on a desk and verify by ohm-meter that you can reliably trigger reed-OPEN ↔ reed-CLOSED transitions at the distance and orientation you'll have in the actual mailbox. NO reeds typically close at ~10–20 mm from a small neodymium magnet; pull-back range is similar.

**Reed type confirmed by Marko:** NO. Mounting is the responsibility of physical install — firmware assumes inverted-mount semantics (reed CLOSED = lid OPEN = wake event).

### Blue LED (debug — wired permanently, firmware-gated, blinks once per LoRa TX)

| Wire | Feather silkscreen |
|---|---|
| LED anode (+) | **5** (= code pin D5, through ~330 Ω current-limit resistor if not already inline with the LED package) |
| LED cathode (−) | **GND** |

**Behaviour:** the LED **blinks once briefly (~100 ms) immediately after a LoRa packet is transmitted** — visual confirmation that the radio fired. Used to verify on the bench / from inside the mailbox enclosure that reed events, heartbeats, and boot packets all leave the antenna. Not a "the chip is awake" indicator and not a "chip is sleeping" indicator — strictly one blink per TX.

```
Wake (reed or WDT) ──► sensor read ──► LoRa send ──► [LED 100 ms ON] ──► sleep
```

**Production rule:** the LED stays physically wired in the field. The firmware uses `#define DEBUG_LED 1` (prototyping) or `#define DEBUG_LED 0` (production). When 0, no `pinMode`/`digitalWrite` ever touches D5 — the pin is left tristate, the LED stays dark, ~3 mA awake-current saved. **No de-soldering between dev and deployment.** Power impact in prototyping mode: 100 ms × ~5 mA per blink ≈ 0.5 mAs per TX × ~3 TX/day ≈ 1.5 mAs/day — negligible.

### Removed

| Old pin | Old function | Why removed |
|---|---|---|
| D11 | DHT22 data | replaced by BME280 |
| D3 | Reed signal (old) | freed for I2C SCL |

After the swap, **D11 is a free GPIO** for any future use (e.g. power-gating the BME280 from a GPIO if you want absolute minimum sleep current later).

---

## 2. Power tree

```
LiPo 3.7 V (2000 mAh)
   │
   └── Feather JST connector ── on-board PowerPath ──┬── 3V3 LDO ──┬── ESP/MCU rail
                                                     │             ├── BME280 VCC
                                                     │             ├── Reed pull-up (3V3 side)
                                                     │             └── 4.7 kΩ I2C pull-ups (on BME280 module)
                                                     │
                                                     └── BAT pin (raw LiPo) ── A9 divider ── analogRead

USB micro-B (when present): charges LiPo via on-board MCP73831, switches MCU to USB power automatically.
```

The Feather's PowerPath chip transparently switches between LiPo and USB. **Never feed USB voltage directly to 3V3** — always through the Feather's regulator.

---

## 3. Bench wiring sequence (the "what to plug in next" order)

Recommended order so the board is debuggable at each step:

1. **Bare Feather + LiPo + USB.** Confirm board enumerates on Arduino IDE and onboard charge LED behaves (off when LiPo is full and disconnected, orange when charging).
2. **Add the blue debug LED on the "5" pin** (= code D5). Quick `digitalWrite(5, HIGH); delay(500); digitalWrite(5, LOW); delay(500);` blink test.
3. **Add the BME280** (4 wires: VCC→3V, GND→GND, SDA→SDA, SCL→SCL, plus CSB→3V and SDO→GND on the module). Run an I2C scanner sketch — should report device at 0x76. If you see 0x77 instead, your SDO is not actually connected to GND.
4. **Add the reed switch.** Wire one reed terminal to the **"RX"** pin (= code D0), the other terminal to **GND**. No external resistors. In firmware, `pinMode(0, INPUT_PULLUP)` then read `digitalRead(0)` in a loop. Confirm: pin = HIGH when the magnet is **away** from the reed (idle state, simulating "lid closed" with the inverted mounting); pin = LOW when magnet is **near** the reed ("lid open" / mail event).
5. **Verify wake from sleep.** Minimal `attachInterrupt(digitalPinToInterrupt(0), wakeISR, LOW)` + `LowPower.powerDown(...)`. Confirm magnet-near (reed close) wakes the board.
6. Only then bring up the LoRa packet code.

---

## 4. Parts checklist (everything you need on the bench)

| Part | Qty | Source | Notes |
|---|---|---|---|
| Adafruit Feather 32u4 LoRa | 1 | already wired | confirm 868 MHz variant |
| 2000 mAh LiPo (103450) | 1 | already wired | already on hand, dated 2024-07 |
| GY-BME280 6-pin module | 1 | drawer "GY-BME280 3.3" | confirm it has the on-board LDO |
| NO reed switch + magnet | 1 | already wired | the white reed seen in your circuit photos |
| 10 kΩ resistor | 1 | drawer "RE 1k-20k" | any 4.7–47 kΩ works |
| Blue LED | 1 | drawer "LED Blue …" | already wired |
| 330 Ω resistor (LED current limit) | 1 | drawer "R270 1K" or similar | only if not already inline with the LED |
| Hookup wire / breadboard | — | already wired | |
| Wire-stub antenna for 868 MHz | 1 | already wired | quarter-wave ≈ 8.2 cm |

---

## 5. Cross-reference

- **Receiver firmware design:** `RECEIVER_V3_PLAN.md` (the receiver must parse the new key=value packet format with BME280 fields).
- **Workflow & testing phases:** `WORKFLOW_AND_TESTING.md` (Phase 2 covers the BME280 + reed bench tests).
- **Locked design decisions:** captured in agent memory (`project_decisions.md`).

---

*Sender firmware is `mailbox_sender_V3.ino` in `firmware/mailbox_sender_V3/`. (Renamed 2026-05-07 from the longer `adafruit_Lora_32u4_Mailbox_Sender_V3.ino`.)*
