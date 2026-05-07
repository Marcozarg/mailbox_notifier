# V2 Hardware Add-ons — ESPHome-compatible

Three optional upgrades for the Ledtyokkari Remote, each with parts you can buy from Mouser/DigiKey/AliExpress and wiring/config that drops into the existing `ledtyokkari_remote.yaml`. Pick any subset — they're independent.

| Add-on | Goal | Cost | Effort |
|---|---|---|---|
| 1. Soft-latch power switch (P-MOSFET) | Cut buck quiescent in deep sleep | ~€2 DIY or ~€4 module | Medium |
| 2. IMU / tilt wake | "Pick remote up → it wakes" | €0.50 (tilt) – €3 (IMU) | Low – Medium |
| 3. Haptic feedback | Buzz on long-press confirm | ~€2 | Low |

Pin budget after V2 (relative to V1 PROJECT_MEMORY):

| Function | Pin | Where it comes from |
|---|---|---|
| Power-latch hold | GPIO15 | New — RTC GPIO so it stays asserted when CPU stops? No — held by latch hardware. Just any GPIO. |
| IMU INT (motion → wake) | GPIO2 | New — must be RTC GPIO (GPIO 0–21 on S3) |
| IMU SDA | GPIO17 | Already reserved as I2C in V1 |
| IMU SCL | GPIO18 | Already reserved as I2C in V1 |
| Haptic motor | GPIO13 | Freed when we removed display BL pin |

---

## 1. Soft-latch power switch (P-MOSFET)

### What it solves
The DSN-MINI-360 still draws ~6 mA when its output has no load (i.e. when the ESP32 is in deep sleep). On a 2500 mAh AA pack that's ≈ 17 days of "nothing" before the cells die. A high-side P-MOSFET in front of the buck cuts that path entirely — quiescent current drops to the MOSFET leakage (< 1 µA).

The catch: if you cut buck power, the ESP32 dies too. So you need a **soft latch** — the user presses a button to *start* power, the ESP32 holds power on while it runs, and releases it before final shutdown.

### Easiest path: Pololu Mini Pushbutton Power Switch
Pre-built, no soldering of small SMD parts. **Critical: pick a 4.5–20 V (SV / HP) variant, NOT the LV one** — fresh 4×AA alkalines deliver up to 6.4 V which destroys the LV part.

| Pololu # | Variant | Input range | Fits 4×AA? |
|---|---|---|---|
| **#2811** | SV | 4.5 – 20 V | ✅ Recommended — pick this |
| **#2812** | SV w/ rev. protection | 4.5 – 20 V | ✅ Recommended — adds reverse-battery protection |
| #2810 | HP (high current) | 4.5 – 20 V | ✅ Works, overkill for ~80 mA load |
| ⚠ #2808 | LV (Low Voltage) | 2.5 – 5.5 V | ❌ **Will be damaged by fresh 4×AA (6.4 V)** |

If your only available stock is #2808, do NOT use it with 4×AA. Either order an SV/HP variant, switch to the DIY circuit below, or skip the latch for V1 and accept the buck's ~6 mA idle draw (~17 days battery life on a 2500 mAh AA pack).

Pinout: `VIN`, `GND`, `VOUT`, `OFF`, `A`, `B` (button terminals).

```
4×AA + ── VIN          VOUT ── DSN-MINI-360 IN+
4×AA − ── GND
                       OFF ── ESP32 GPIO15
                       A ──┐
                            ├── physical pushbutton (replaces or parallels current power button)
                       B ──┘
```

Behaviour: pressing the button briefly pulls the latch on. Driving `OFF` high for ≥100 ms releases the latch and powers everything down.

### ESPHome config

```yaml
# Add to existing config:

output:
  - platform: gpio
    pin: GPIO15
    id: power_off_pulse

switch:
  - platform: output
    name: "Hard power off"
    id: hard_off_switch
    output: power_off_pulse
    internal: true

# Modify the long-press of the power button:
binary_sensor:
  - platform: gpio
    id: btn_power
    # ... existing config ...
    on_click:
      - min_length: 1500ms
        max_length: 5000ms
        then:
          - script.execute: save_state
          - delay: 200ms
          - switch.turn_on: hard_off_switch   # raise OFF high
          - delay: 200ms                       # latch releases
          # We should never reach here — power is gone.
          - switch.turn_off: hard_off_switch
```

Now long-press = real power off (battery quiescent ≈ 0). Wake = press the button (latch turns on, ESP32 boots).

### DIY version (if you'd rather solder it yourself)
Three parts plus a button. ~€1 in components.

```
              BAT+ ────┬─────────────────┬─── (P-FET source)
                       │ R1 100k         │
                       │                 │
                       └────┬─── P-FET ──┴── DRAIN ── buck IN+
                            │   (gate)
                            │
                       ┌────┴────┐
                  PWR ─┤   D     ├── 100k     R2
                  BTN  └────┬────┘    │
                            │         │
                            │     ┌───┴────┐
                            │     │  NPN   │
                       ESP─┤B├ R3 │ 2N3904 │ ─ GND
                       GPIO15     │  E ─ GND
                                  └────────┘
```

| Part | Suggested | Notes |
|---|---|---|
| P-MOSFET | **AO3401A** (SOT-23) or **DMG3415U** | Logic-level, Vgs(th) < 1 V, Rds(on) < 60 mΩ at -2.5 V Vgs |
| TH alternative | **IRLML6402** (SOT-23) or **FQP27P06** (TO-220) | If you'd rather have a through-hole-ish footprint |
| NPN | **2N3904** or **BC547** | Cheap general-purpose |
| Diode D | **1N4148** | Decouples button from GPIO |
| R1 | 100 kΩ | Gate pull-up |
| R2 | 100 kΩ | Holds NPN base off when GPIO is HiZ |
| R3 | 10 kΩ | Base current limit |

Behaviour:
1. Idle: gate sits at BAT+ via R1 → P-FET off → no power.
2. Press button: gate momentarily pulled to GND through D → P-FET on → ESP boots.
3. ESP boots, asserts GPIO15 HIGH → NPN on → keeps gate at GND → power stays on.
4. Hard-off: ESP drops GPIO15 LOW → NPN off → gate floats up → P-FET off → done.

The same P-FET module/circuit also gives you reverse-battery protection for free.

---

## 2. IMU / Tilt wake

### Two tiers — pick one

#### A. Cheap: ball tilt switch (no firmware needed)
**Part:** `SW-520D` (vibration ball switch) — a few cents on AliExpress, comes in TO-92ish form. Closes momentarily when shaken or tilted past ~15°.

```
ESP32 GPIO2 ──┬── SW-520D ── GND
              └── (internal pull-up)
```

ESPHome config:

```yaml
binary_sensor:
  - platform: gpio
    name: "Tilt wake"
    id: tilt_sw
    pin:
      number: GPIO2
      mode: { input: true, pullup: true }
      inverted: true
    on_press:
      - lambda: 'id(last_user_input_ms) = millis();'

# Add GPIO2 to deep sleep wake pins:
deep_sleep:
  id: ds_1
  esp32_ext1_wakeup:
    pins: [GPIO2, GPIO6, GPIO7]
    mode: ANY_LOW
```

That's it. Pick the remote up, it shakes, the switch closes, ESP wakes. No I2C, no driver, ~3 µA leakage.

The downside: very dumb. It triggers on any vibration including walking past the table. There's no threshold or sensitivity tuning.

#### B. Smart: ADXL345 (you have one in stock)
**Part:** GY-291 ADXL345 module (or bare chip on a breakout). 3-axis accelerometer with on-chip activity / inactivity detection — designed for exactly this wake-up scenario. Active current ~40–100 µA, standby 0.1 µA. **Better than MPU6050 for battery use** (~36× lower active current).

> Originally this guide suggested LIS3DH, then MPU6050. Switched to ADXL345 because that's what's on the shelf — and it has cleaner activity-detect than either.

Wiring (GY-291 ADXL345 module):

| ADXL345 / GY-291 pin | ESP32 pin | Notes |
|---|---|---|
| VCC | 3V3 | Module has on-board LDO, also accepts 5 V. Bare chip is 2.0 – 3.6 V — **never put 5 V on a bare ADXL345**. |
| GND | GND | |
| CS | 3V3 | **Critical:** tie HIGH to force I2C mode. LOW = SPI mode and the chip won't respond on I2C. |
| SDO | GND | Selects I2C address 0x53 (default). Tie HIGH for 0x1D. |
| SDA | GPIO17 | I2C bus shared with DRV2605L (0x5A) |
| SCL | GPIO18 | |
| INT1 | GPIO2 | RTC-capable wake source — configured **active-LOW** below to match the buttons |
| INT2 | leave open | Unused |

ESPHome config (`adxl345_i2c` is in core):

```yaml
i2c:
  id: bus_a
  sda: GPIO17
  scl: GPIO18
  frequency: 400kHz

sensor:
  - platform: adxl345_i2c
    address: 0x53
    accel_x: { name: "Accel X", internal: true }
    accel_y: { name: "Accel Y", internal: true }
    accel_z: { name: "Accel Z", internal: true }
    update_interval: never        # we only care about the INT1 pin

# Activity-detect setup. Done in raw I2C because ESPHome's adxl345_i2c
# platform doesn't expose the activity-detection registers.
esphome:
  on_boot:
    - priority: 200
      then:
        - lambda: |-
            auto* bus = id(bus_a);
            const uint8_t addr = 0x53;
            // BW_RATE (0x2C): 12.5 Hz output rate — plenty for "did it move" + low power
            bus->write_byte(addr, 0x2C, 0x07);
            // DATA_FORMAT (0x31): full resolution, ±2 g, INT_INVERT=1 (active-LOW)
            bus->write_byte(addr, 0x31, 0x28);
            // THRESH_ACT (0x24): 4 → ~250 mg threshold (62.5 mg per LSB). Tweak for sensitivity.
            bus->write_byte(addr, 0x24, 0x04);
            // ACT_INACT_CTL (0x27): activity = AC-coupled, all 3 axes participate
            bus->write_byte(addr, 0x27, 0x70);
            // INT_MAP (0x2F): all interrupts → INT1 (default — bit set = INT2)
            bus->write_byte(addr, 0x2F, 0x00);
            // INT_ENABLE (0x2E): activity interrupt enabled
            bus->write_byte(addr, 0x2E, 0x10);
            // POWER_CTL (0x2D): start measurements (Measure=1)
            bus->write_byte(addr, 0x2D, 0x08);
            // Read INT_SOURCE (0x30) once to clear any stale interrupt
            uint8_t dummy;
            bus->read_byte(addr, 0x30, &dummy);

binary_sensor:
  - platform: gpio
    name: "Motion wake"
    id: motion_int
    pin:
      number: GPIO2
      mode: { input: true, pullup: true }
      inverted: true            # INT_INVERT=1 above made it active-LOW
    on_press:
      - lambda: 'id(last_user_input_ms) = millis();'
      # Clear the latched interrupt by reading INT_SOURCE
      - lambda: |-
          uint8_t s;
          id(bus_a)->read_byte(0x53, 0x30, &s);

deep_sleep:
  id: ds_1
  esp32_ext1_wakeup:
    pins: [GPIO2, GPIO6, GPIO7]
    mode: ANY_LOW              # everything is active-LOW — clean and consistent
```

> **Polarity is consistent now.** INT_INVERT in DATA_FORMAT (bit 5) makes INT1 active-LOW, matching the buttons' wake-on-LOW. No NPN inverter needed.

> **Tuning sensitivity:** `THRESH_ACT` register is the sensitivity dial. 62.5 mg per LSB. Default `0x04` ≈ 250 mg — needs a deliberate pickup. Lower (`0x02`) wakes on lighter motion; higher (`0x08`) needs a real shake. Tune after first power-on.

#### Power-saving extras
- For absolute lowest power, set `POWER_CTL = 0x18` (auto-sleep + link). The chip drops into ~50 µA sleep when no activity is detected for `THRESH_INACT` / `TIME_INACT`, and wakes on activity. The catch: standby in this mode is less consistent than full standby; only worth it if you measure real savings.
- Activity threshold tuning matters — too low and you'll wake from a fan vibration on the desk.

#### Alternatives worth knowing (not needed now)
- **LIS3DH** — even lower current but you'd need to buy one.
- **MPU6886** — better than 6050, similar API. Out of scope, you don't have one.
- **REED switch** (in your drawer "REED SENSOR") — magnetic-actuated wake. Stick a magnet to a docking stand: pulling the remote off triggers wake. No I2C. Different UX though.

---

## 3. Haptic feedback (coin motor)

### Goal
A short buzz on long-press confirmations so the user knows "yes, you did hold it long enough" without looking at the screen. Especially nice for the deep-sleep long-press (otherwise you don't know it registered until the screen goes black).

### Recommended: DRV2605L module (you ordered one)

I2C haptic driver with 123 pre-loaded waveform effects (sharp clicks, ramps, double-buzzes). Drops on the existing I2C bus next to the MPU6050. Pros: no PWM patterns to design, auto-calibrates to the motor, one-write-and-go.

Wiring:

| DRV2605L pin | ESP32 pin / motor | Notes |
|---|---|---|
| VCC / VDD | 3V3 | Module has on-board regulation; some accept 5 V |
| GND | GND | |
| SDA | GPIO17 | Same I2C bus as MPU6050 |
| SCL | GPIO18 | |
| IN/TRIG | leave open | Only used in PWM mode; we drive via I2C |
| OUT+ | coin motor + | |
| OUT− | coin motor − | |

I2C address: 0x5A (fixed, can't conflict with the 0x68 MPU6050).

ESPHome config — there's no native `drv2605` component, so we use raw I2C writes wrapped in scripts:

```yaml
# Effect IDs from the TI/Immersion library (selection — full list in DRV2605L datasheet):
#  1   = strong click 100%
#  10  = double click 100%
#  14  = strong buzz 100%
#  17  = strong click 60%
#  24  = sharp click 100%
#  47  = buzz alert (long)
#  52  = ramp down medium
#  84  = transition click 1, 100%

script:
  # Play a single waveform slot from the library
  - id: drv_play
    parameters:
      effect: int
    then:
      - lambda: |-
          auto* bus = id(bus_a);
          bus->write_byte(0x5A, 0x01, 0x00);              // MODE: internal trigger (waveform from library)
          bus->write_byte(0x5A, 0x03, 0x00);              // Library = TS2200 library A (ERM)
          bus->write_byte(0x5A, 0x04, (uint8_t) effect);  // Waveform sequencer slot 1 = effect
          bus->write_byte(0x5A, 0x05, 0x00);              // Sequencer slot 2 = end
          bus->write_byte(0x5A, 0x0C, 0x01);              // GO

  # One-shot configuration — call once at boot
  - id: drv_init
    then:
      - lambda: |-
          auto* bus = id(bus_a);
          bus->write_byte(0x5A, 0x01, 0x07);              // Out of standby
          delay(10);
          bus->write_byte(0x5A, 0x16, 0x90);              // RATED_VOLTAGE for 3 V ERM
          bus->write_byte(0x5A, 0x17, 0xC8);              // OD_CLAMP for 3.6 V ERM
          bus->write_byte(0x5A, 0x1A, 0x36);              // FEEDBACK_CTRL: ERM mode, medium loop gain
          bus->write_byte(0x5A, 0x1C, 0x93);              // CONTROL3: ERM open loop
          bus->write_byte(0x5A, 0x01, 0x00);              // MODE: internal trigger

esphome:
  on_boot:
    - priority: 100
      then:
        - script.execute: drv_init

# Wire into long-press handlers:
binary_sensor:
  - platform: gpio
    id: btn_power
    on_click:
      - min_length: 1500ms
        max_length: 5000ms
        then:
          - script.execute:
              id: drv_play
              effect: 10            # double click — "going to sleep" cue
          - delay: 300ms
          - script.execute: save_state
          - deep_sleep.enter: ds_1

  - platform: gpio
    id: btn_random
    on_click:
      - min_length: 1500ms
        max_length: 5000ms
        then:
          - script.execute:
              id: drv_play
              effect: 24            # sharp click — rainbow toggle confirm
          - if:
              condition:
                lambda: 'return id(rainbow_active);'
              then: [ script.execute: cancel_rainbow ]
              else:
                - globals.set: { id: rainbow_active, value: 'true' }
                - script.execute: rainbow_loop
```

You can also pre-load up to 8 effects into the sequencer (registers 0x04–0x0B) for a chained pattern like "click-pause-click-buzz-fade".

### Fallback driver: 2N7000 + flyback diode
~€1 in parts. Use this only if the DRV2605L doesn't arrive. Drives any small ERM coin motor.

```
                  3V3 ──── motor + ──── motor − ──┬──── (drain) 2N7000
                                                  │
                                       diode 1N4148 (cathode to 3V3)
                                                  │
                                                  └── GND (anti-parallel)

  GPIO13 ──── R1 1k ────── (gate) 2N7000
                                       (source) ── GND

                                   gate ── R2 10k ── GND  (pulldown)
```

Parts:

| Part | Suggested | Notes |
|---|---|---|
| Motor | **10 mm 3 V coin ERM** (e.g. Vybronics VC1234B or any AliExpress equivalent) | Draws ~80 mA at 3 V, fine on the buck output |
| FET | **2N7000** (TO-92) or **BS170** | Logic-level NMOS. Or **AO3400A** if SMD |
| Diode | **1N4148** or **1N4007** | Flyback — required, motor is inductive |
| R1 | 1 kΩ | Gate drive |
| R2 | 10 kΩ | Holds gate low during boot / sleep |

If you want PWM strength control, drive the gate from an LEDC PWM pin instead of GPIO. Skip PWM and just use a plain GPIO if you only need on/off vibration.

### ESPHome config

```yaml
output:
  - platform: ledc
    pin: GPIO13
    id: haptic_pwm
    frequency: 19531Hz       # above audible — no whine

# Wrap as a switch for action calls
switch:
  - platform: output
    name: "Haptic"
    id: haptic
    output: haptic_pwm
    internal: true

script:
  # Single short buzz (~80 ms)
  - id: buzz_short
    then:
      - output.set_level:
          id: haptic_pwm
          level: 70%
      - delay: 80ms
      - output.turn_off: haptic_pwm

  # Two-pulse "confirmed" pattern
  - id: buzz_double
    then:
      - output.set_level: { id: haptic_pwm, level: 80% }
      - delay: 60ms
      - output.turn_off: haptic_pwm
      - delay: 80ms
      - output.set_level: { id: haptic_pwm, level: 80% }
      - delay: 60ms
      - output.turn_off: haptic_pwm

# Wire into the long-press handlers in the main yaml:
# (replace the existing long-press blocks)
binary_sensor:
  - platform: gpio
    id: btn_power
    on_click:
      - min_length: 1500ms
        max_length: 5000ms
        then:
          - script.execute: buzz_double      # confirmation buzz
          - script.execute: save_state
          - deep_sleep.enter: ds_1

  - platform: gpio
    id: btn_random
    on_click:
      - min_length: 1500ms
        max_length: 5000ms
        then:
          - script.execute: buzz_short        # rainbow toggle confirm
          - if:
              condition:
                lambda: 'return id(rainbow_active);'
              then: [ script.execute: cancel_rainbow ]
              else:
                - globals.set: { id: rainbow_active, value: 'true' }
                - script.execute: rainbow_loop
```

Result: any long-press gets a short tactile confirmation; deep-sleep gets a double-buzz "goodbye" pattern.

---

## Combined V2 pinout summary

| Function | Pin | Component |
|---|---|---|
| 3V3 / GND | 3V3, GND | Power rails |
| Brightness pot wiper | GPIO4 | B10K |
| Color-temp pot wiper | GPIO5 | B10K |
| Power button | GPIO6 | Tactile / external soft-latch |
| Random button | GPIO7 | Tactile |
| Battery sense | GPIO8 | 100 k / 47 k divider |
| Motion wake INT | GPIO2 | LIS3DH INT1 (or tilt switch) |
| Haptic | GPIO13 | 2N7000 gate (PWM) |
| Hard-off pulse | GPIO15 | Pololu OFF or NPN base |
| TFT DC | GPIO9 | GC9A01 |
| TFT CS | GPIO10 | GC9A01 |
| TFT SDA / MOSI | GPIO11 | GC9A01 |
| TFT SCL / SCLK | GPIO12 | GC9A01 |
| TFT RST | GPIO14 | GC9A01 |
| I2C SDA | GPIO17 | LIS3DH (and any future I2C peripherals) |
| I2C SCL | GPIO18 | LIS3DH |

Total used: 14 GPIOs + 3V3/GND. Plenty of headroom on ESP32-S3 (~30 free pins).

---

*Last updated: 2026-04-30 — V2 add-on hardware specified, all parts ESPHome-supported.*
