# ESP32 Remote — Project Memory

Remote controller for the Philips Surimu 60×60 cm LED panel
(`light.ledtyokkari` in Home Assistant, on Zigbee via Zigbee2MQTT).

---

## 0. Where we left off (read this first)

**Latest firmware:** `ledtyokkari_remote.yaml` at **V1.2.1** (header line shows date/time).

**What works:** display arcs (top = brightness, bottom = colour temp), brightness pot, colour-temp pot (NiMH curve), power button (short = toggle, long = animated 2 s sleep), random button (short = save+random, long = restore previous), 60 s idle sleep, deep-sleep wake from button presses (with external 10 kΩ pull-ups on GPIO6/7 to 3V3), startup splash "Kattopaneeli ohjain / by Marko" for 2 s, RGB readout always visible on left, RND label on right when active, **ADXL345 motion-wake (V1.1.1 confirmed working — boot log #6 shows ~43 s of true silence in deep sleep, wake on pickup).** GPIO2 → INT1, motion_int binary_sensor live, GPIO2 in deep_sleep wake list, ACT_INACT_CTL = 0xF0 (AC-coupled) verified by readback every boot.

**V1.2 / V1.2.1 — display UX updates:**
- **Sleep countdown indicator** drawn on the right side of the round display: a bed pictogram (now 19×11 px in V1.2.1, was 16×8) plus "Ns" text right-aligned at x=225, font_med. Counts 60 → 0 mirroring the idle-sleep timer. Colour grades sleepy-blue (default) → amber (≤ 10 s) → red (≤ 3 s).
- **"Panel is off" minimal view**: when `last_light_on` is false, the display drops the arcs, kelvin number, brightness %, RGB readout, RND label, and the sleep indicator — only "Panel is off" text near the top and the centred battery icon remain.
- **RGB readout** (V1.2.1): each row coloured in its channel — R red, G green, B blue. Bright when random_active, dim otherwise. Font_med (was font_small) and 24 px row spacing (was 18 px).

**Action for next chat:** Marko flashes V1.2 and checks the live look. Watch for:
1. Idle sleep countdown ticks visibly on the right side and stays in sync with the actual sleep moment (when "0s" is shown, the takeover screen should appear immediately).
2. Bed icon is legible at the chosen 16×8 px size — if it looks too small / blocky, swap to a larger size or use the `font_med`-sized "Zzz" text instead (one-line edit at the bottom of the panel-ON path in the display lambda).
3. Pressing the power button so the panel turns off causes the round display to switch to the minimal "Panel is off" view. Pressing it again returns the full UI.

**Tuning knobs available:**
- **Motion threshold:** register `0x24` in `adxl345_init` script. 0x10 = 1.0 g (delta) = brisk pickup. Lower if pickup is too gentle to trigger; raise if random table bumps wake the remote. Sensitivity table is in the YAML comment.
- **Idle timeout:** the `60000` constant appears in two places — the idle-sleep `interval:` block AND the new sleep-countdown computation in the display lambda. Keep them in sync if you change it.
- **AC reference re-capture (latent issue, not yet seen):** AC-coupled activity detection captures its reference from the first sample after `POWER_CTL = 0x08` is written. At our 12.5 Hz rate, that's ~80 ms post-Measure. If the user is holding or jiggling the remote during boot, the reference will be off — symptom would be "pickup doesn't trigger but small bumps do later". Fix if it bites: add a `delay: 200ms` after the `POWER_CTL` write in `adxl345_init`, then re-write `INT_ENABLE = 0x10` to force a fresh AC reference capture from a settled sample. (Source: adxl345.pdf p.24.)

**Lingering V1.0.7 mystery (not blocking):** on_boot priority 600 ("Boot — restored …") and priority -10 ("API connected — remote ready") never produced log lines in any V1.0.6 / V1.0.7 / V1.1 / V1.1.1 boot log. The `interval:` block worked throughout, which is why we route ADXL345 init through it. Both on_boot hooks are left in place as passive probes.

**V2 add-ons in transit:** Pololu #2811 soft-latch power switch, DRV2605L haptic driver, ERM coin motors. All firmware-side preparation is in `V2_HARDWARE.md`. V2 starts when parts arrive.

---

## 1. Environment

- Home Assistant (with ESPHome, Node-RED, Zigbee2MQTT add-ons)
- Target light entity: `light.ledtyokkari`

### On-shelf parts inventory (relevant to this project)
From the parts cabinet pictured in `MyComponents1.jpeg` / `MyComponents2.jpeg`:

| Part | Drawer label | Use in this project |
|---|---|---|
| ESP32-S3-WROOM-1 | "ESP32 S3" | Main MCU |
| 1.28" round TFT | "1.28 TFT LCD" | Already wired in V1 |
| **ADXL345 accelerometer** | "ADXL345" | **Motion wake — V2 §2** (MPU6050 is out of stock) |
| DSN-MINI-360 buck | "DSN-MINI-360" | Already in V1 power tree |
| AMS1117-3.3V LDO | "AMS1117-3.3V" | Backup regulator if buck removed |
| BC547B (NPN) | "BC547B/BC557B" | DIY soft-latch fallback (sub for 2N3904) |
| BC557B (PNP) | "BC547B/BC557B" | Available |
| 1N4148 / 1N4007 diodes | "Diodeja" | Soft-latch decoupling diode |
| Resistor kits | "R150" / "R270 1K" / "R2K-4.7K" / "RE 1k-20k" | All values needed are covered |
| MTS-101 SPST switch | "MTS-101 SPST" | Master power slide switch |
| Reed sensor | "REED SENSOR" | Optional alt-wake (magnet-on-stand) |
| Tactile buttons | "Napei 6x6x5" | Power + random buttons |
| Pin headers | "Pin STRIP" | Mount ESP32 dev board |
| Breadboards | "Breadboards" | Bench assembly |
| BME280 / BME680 | "GY-BME280 3.3" / "BMK680" | Future: ambient light/temp sensing? |

**Ordered (in transit):** Pololu #2811 soft-latch power switch · DRV2605L haptic driver module · ERM coin motors.

### YAML source of truth
**`ledtyokkari_remote.yaml` in this folder is the canonical version.** The user always flashes from this file (synced into the ESPHome dashboard). Any change made on the dashboard side is communicated back to me explicitly. I should not assume the dashboard has diverged unless told.

### Conversation conventions
- **Claude context-usage notifications:** Notify Marko when conversation context usage crosses ~30 %, ~60 %, and ~80 %. Each threshold gets one notification per conversation. I don't have a precise telemetry tool, so estimates are based on my own awareness of how full the working context has gotten — file sizes read, message count, depth of accumulated detail.
- **File versioning convention (applies to ALL coding projects, not just this one):**
  - Every file starts with a header line: `## V<major>.<minor>[.<patch>] — YYYY-MM-DD HH:MM — <description>`
  - Increment scheme:
    - `V1.0.x` = bug fix, comment, typo, cosmetic-only edit
    - `V1.x`   = feature change, behaviour tweak, display/UX polish
    - `V2`     = major hardware/architecture change (new peripheral block, MCU swap, schema rewrite)
  - Time is in the user's local zone (Europe/Helsinki). I should fetch current time when writing the header.

### Mechanical / CAD toolchain
| Tool | Use |
|---|---|
| **Prusa MK4S** | 3D printer for enclosure, knobs, button caps |
| **PrusaSlicer** | Slicing — output `.gcode` / `.bgcode` for the MK4S |
| **Fusion 360** | CAD modelling |

**File format preference (highest → lowest):**
1. **Fusion 360 native** (`.f3d` / `.f3z`) — preferred when achievable.
2. **STEP** (`.step` / `.stp`) — fully editable parametric import in Fusion 360. **Use this whenever native Fusion files aren't possible** (e.g. anything generated by code).
3. **STL** — only as a last-resort fallback. Mesh-only, not parametrically editable.

When delivering 3D-printable parts: always provide an editable source (STEP or scripted OpenSCAD) alongside any STL, so geometry can be tweaked in Fusion before reslicing.

## 2. V1 — Locked Decisions

| Decision | Choice |
|---|---|
| Microcontroller | ESP32-S3-WROOM-1 |
| Display | 1.28" round 240×240, **GC9A01 controller, SPI** |
| Inputs | 2× B10K pots (brightness, color temp) + 2 buttons (power, randomizer) |
| Firmware | **ESPHome** (HA-native, OTA) |
| Button gestures | **Standard** (short + long press) |
| Power management | **Auto deep-sleep after 30 s idle**, wake on any button |
| Power source | 4× AA → DSN-MINI-360 buck → 3.3 V rail |

### Why not I2C for the display
A 1.28" round 240×240 display in this category uses the **GC9A01** controller, which is **SPI-only**. I2C tops out around 1 MHz; refreshing 240×240×16-bit pixels needs tens of MHz, so SPI is mandatory. I2C remains free on the board for future expansion (battery gauge, GPIO expander, secondary OLED, etc.).

### Button gesture map (V1)
| Button | Short press | Long press (≥1.5 s) |
|---|---|---|
| Power | Toggle `light.ledtyokkari` on/off | Deep-sleep the remote |
| Randomizer | Snapshot current settings, then random color | **Restore the snapshot** (undo the random) |

(Rainbow-loop mode was removed in 2026-04-30 revision — felt like clutter; restore-previous is the more useful behaviour.)

Pots are read continuously with debounce/dead-band so HA isn't flooded — only changes ≥ ~1 % are pushed.

### Power management (V1)
- Idle for 30 s ⇒ display off, MCU enters deep sleep
- Wake-up source: either button (RTC GPIO `ext1` wake)
- DSN-MINI-360 stays powered (the converter itself draws ~6 mA quiescent — acceptable for V1; consider a P-MOSFET high-side switch in V2 for longer battery life)

---

## 3. Hardware checklist

### You already have
- ESP32-S3-WROOM-1 module (assumed on a dev board with USB)
- 1.28" round TFT 240×240 (GC9A01, SPI) — please confirm controller
- 2× B10K linear potentiometers
- 2× momentary push buttons
- 4× AA battery holder
- DSN-MINI-360 buck converter
- Hookup wire, basic resistors / caps

### Probably need / nice-to-have
| Item | Qty | Purpose |
|---|---|---|
| 100 µF electrolytic cap | 1 | Bulk decoupling on 3.3 V rail near ESP32 |
| 0.1 µF ceramic cap | 2–3 | Bypass near ESP32 VDD, near display VCC |
| 10 µF ceramic / tantalum | 1 | Smooths display backlight current |
| 10 kΩ resistor | 2 | Pull-ups for buttons (or use ESP32 internal — see below) |
| 100 kΩ + 47 kΩ resistor | 1 each | Voltage divider for battery-level ADC (optional but recommended) |
| Slide switch SPDT | 1 | Hard power switch on the case (cuts battery + before the buck) |
| Perfboard or solder breadboard | 1 | Final assembly |
| Pin headers (male + female) | — | Mount ESP32 dev board socketed |
| 3D-printed enclosure | 1 | You've got the printer — design after V1 firmware works |

**External 10 kΩ pull-ups on GPIO6 and GPIO7 are required** (revised 2026-04-30). The ESP32 internal pull-ups are disabled in deep sleep, so without externals the wake pins float low and the chip wakes ~6-10 s after entering sleep — defeating the whole power-saving feature. Internal pull-ups still work fine while the chip is awake; the externals are specifically there to hold the line high during sleep.

---

## 4. Pin assignments (ESP32-S3-WROOM-1)

Picked to satisfy: ADC1 only (ADC2 is blocked when WiFi is on), RTC-capable for wake-from-sleep on buttons, no strapping pins on critical lines, no flash pins.

| Function | GPIO | Notes |
|---|---|---|
| **Display SPI** | | All free of strapping conflicts |
| TFT_SCLK | GPIO 12 | SPI clock |
| TFT_MOSI | GPIO 11 | SPI data (display has no MISO) |
| TFT_CS   | GPIO 10 | Chip select |
| TFT_DC   | GPIO 9  | Data/Command |
| TFT_RST  | GPIO 14 | Reset (or tie to EN with RC delay if pin-starved) |
| ~~TFT_BLK~~ | ~~GPIO 13~~ | **Removed from V1** — the 7-pin GC9A01 module exposes only RST/CS/DC/SDA/SCL/GND/VCC. Backlight is hardwired to VCC, no software dim. GPIO 13 is now free. |
| **Pots (ADC1)** | | Must be ADC1 channels |
| Brightness pot wiper | GPIO 4 | ADC1_CH3 |
| Color-temp pot wiper | GPIO 5 | ADC1_CH4 |
| **Buttons (RTC GPIO for wake)** | | Internal pull-ups enabled |
| Power button | GPIO 6 | RTC-capable, EXT1 wake |
| Randomizer button | GPIO 7 | RTC-capable, EXT1 wake |
| **Optional** | | |
| Battery sense ADC | GPIO 8 | Through 100 k / 47 k divider from BAT+ |
| I2C SDA (future) | GPIO 17 | Reserved |
| I2C SCL (future) | GPIO 18 | Reserved |

Avoided: GPIO 0/3/45/46 (strapping), GPIO 19/20 (native USB), GPIO 26-32 (flash), GPIO 33-37 (octal flash on R8 variants — safe on N8/N16 but reserved here for safety).

---

## 5. Wiring guide

### Power tree

```
4× AA (≈4.8–6.4 V)
   │
   ├── SPDT slide switch (master on/off, on case)
   │
   ├── DSN-MINI-360 IN+
   │      ⚠  Pre-tune Vout to 3.30 V with a multimeter BEFORE
   │         connecting any 3.3 V load. Out-of-the-box it can be ~12 V.
   │
   └── DSN-MINI-360 OUT+ ──┬── ESP32-S3 3V3 pin
                            ├── TFT VCC
                            ├── 100 µF // 0.1 µF to GND  (near ESP32)
                            └── 10 µF  // 0.1 µF to GND  (near display)

   GND rail: battery −, DSN GND, ESP32 GND, TFT GND, all button/pot grounds — single star point.
```

### Display (GC9A01 SPI) — 8 wires

| Display pin | Goes to |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SCL / SCK | GPIO 12 |
| SDA / MOSI | GPIO 11 |
| CS | GPIO 10 |
| DC | GPIO 9 |
| RST | GPIO 14 |
| BL / BLK | GPIO 13 |

### Potentiometers (2×)

```
3.3 V ── pin 1 (CW end)
ADC   ── pin 2 (wiper)   ── GPIO 4 (brightness) / GPIO 5 (color temp)
GND   ── pin 3 (CCW end)
```

Add a 100 nF cap from wiper to GND on each pot if readings are jittery.

### Buttons (2×)

```
GPIO 6 ──┬── button ── GND        (Power)
         └── internal pull-up enabled in ESPHome
GPIO 7 ──┬── button ── GND        (Randomizer)
         └── internal pull-up enabled in ESPHome
```

**Always include external 10 kΩ pull-ups** — without them the chip wakes from deep sleep almost immediately (internal pull-ups don't survive deep sleep). Internal pull-ups still active in ESPHome config for normal operation.

### Optional battery monitor

```
BAT+ ── 100 kΩ ──┬── 47 kΩ ── GND
                 └── GPIO 8 (ADC1_CH7)
```
Divider ratio = 47 / (100 + 47) = 0.32. At 6.4 V battery: 2.05 V at ADC (safe). At 4.0 V: 1.28 V. Calibrate in ESPHome with two known points.

---

## 6. Project workflow

Five phases. Don't move on until the previous one demonstrably works.

### Phase 1 — Bench prototype on USB
Goal: every component proven individually, on breadboard, ESP32 powered from USB.
- Wire ESP32 + display + pots + buttons per the table above.
- Flash a "hello world" ESPHome config: log pot ADC values and button states.
- Confirm display lights up (e.g. `display.gc9a01` with a test text).
- Acceptance: serial log shows pot values 0–4095 and button events; display shows test pattern.

### Phase 2 — ESPHome firmware skeleton
Goal: the remote controls the panel from USB power.
- Add `homeassistant.service` calls on pot change → set `brightness_pct` and `color_temp_kelvin` on `light.ledtyokkari`.
- Wire the four button gestures (short/long on each).
- Add debounce (50 ms) and dead-band (≥1 % change) on pots to avoid flooding HA.
- Acceptance: turning pots changes the panel smoothly, both buttons behave as specified, no HA log spam.

### Phase 3 — UI on the round display
Goal: useful display output that mirrors what HA reports.
- Use ESPHome's `lvgl` component (preferred — cleaner than `display:` lambdas for a round screen).
- Layout: brightness arc (left half), color-temp arc (right half), state text in centre ("ON 2700 K · 75 %"), small icon for randomizer/rainbow mode.
- Add an `homeassistant` text/number sensor so the display reflects the *real* light state (handles changes made elsewhere — voice, app, automations).
- Acceptance: display reflects panel state within ~1 s, even when changed from the HA app.

### Phase 4 — Battery + power management
Goal: runs on 4× AA, sleeps when idle.
- Pre-tune DSN-MINI-360 to 3.30 V on the bench before connecting.
- Add `deep_sleep:` with `run_duration: 30s` and `wakeup_pin` on EXT1 covering both buttons.
- Pots are *not* sleep wake sources — turning a knob alone won't wake; press a button first. (V2 idea: tilt switch / accelerometer wake.)
- Add battery-level reporting (optional divider) to a HA sensor.
- Acceptance: device boots in <2 s on button press, sleeps after 30 s, idle current acceptable on a multimeter.

### Phase 5 — Enclosure + final wiring
- Move from breadboard to perfboard. Socket the ESP32 dev board on female headers so it's removable.
- Design 3D-printed shell on the Prusa: round window for the TFT, two pot shafts, two button caps, slide-switch slot, AA bay.
- Final cable management, glue battery wires, hot-glue strain-relief on the buck converter.
- Acceptance: the remote is one piece you can pick up and use without thinking.

---

## 7. ESPHome config

Full V1 firmware lives in **`ledtyokkari_remote.yaml`** in this folder. It follows the structure of the user's existing `Esphome_template.yaml` (substitutions, sectioned comment headers, `!secret` based WiFi/OTA/AP). Drop it into your ESPHome dashboard or `esphome/` folder.

Required `secrets.yaml` keys (already present in user's template-based setup):
`wifi_SSID`, `iot_password`, `fallback_ssid`, `fallback_password`, `ota_password`.

Adjust `device_ip_address` substitution to a free IP in your `192.168.15.0/24` subnet.

Sketch of the inputs section (full file: `ledtyokkari_remote.yaml`):

```yaml
esphome:
  name: ledtyokkari-remote
esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
api:
ota:
logger:

# --- inputs ---
binary_sensor:
  - platform: gpio
    pin: { number: GPIO6, mode: { input: true, pullup: true }, inverted: true }
    name: "Power button"
    on_click:
      - min_length: 30ms
        max_length: 800ms
        then:
          - homeassistant.service:
              service: light.toggle
              data: { entity_id: light.ledtyokkari }
      - min_length: 1500ms
        max_length: 5000ms
        then:
          - deep_sleep.enter:
  - platform: gpio
    pin: { number: GPIO7, mode: { input: true, pullup: true }, inverted: true }
    name: "Randomizer button"
    on_click:
      - min_length: 30ms
        max_length: 800ms
        then:
          - lambda: |-
              auto h = random(0, 360);
              id(set_random_color).execute(h);
      - min_length: 1500ms
        max_length: 5000ms
        then:
          - script.execute: rainbow_loop

sensor:
  - platform: adc
    pin: GPIO4
    name: "Brightness pot"
    update_interval: 100ms
    attenuation: 11db
    filters:
      - throttle: 100ms
      - delta: 0.03
    on_value:
      - homeassistant.service:
          service: light.turn_on
          data:
            entity_id: light.ledtyokkari
            brightness_pct: !lambda 'return (int)(x / 3.3 * 100);'
  - platform: adc
    pin: GPIO5
    name: "Color temp pot"
    update_interval: 100ms
    attenuation: 11db
    filters:
      - throttle: 100ms
      - delta: 0.03
    on_value:
      - homeassistant.service:
          service: light.turn_on
          data:
            entity_id: light.ledtyokkari
            color_temp_kelvin: !lambda 'return (int)(2200 + (x / 3.3) * (6500 - 2200));'

# --- display (filled in during Phase 3) ---
spi:
  clk_pin: GPIO12
  mosi_pin: GPIO11
display:
  - platform: gc9a01
    cs_pin: GPIO10
    dc_pin: GPIO9
    reset_pin: GPIO14
    # lambda or LVGL goes here

deep_sleep:
  id: deep_sleep_1
  run_duration: 30s
  wakeup_pin:
    number: GPIO6
    inverted: true
```

(Helper `script:` blocks for `set_random_color` and `rainbow_loop` to be written in Phase 2.)

---

## 8. V2 ideas — status

- ✅ **Save last state across deep sleep** — done in firmware via `restore_value: yes` globals (`last_brightness_pct`, `last_color_temp_k`, `last_light_on`, `last_battery_pct`). On_boot priority 600 reads them and forces a display update before WiFi/API. NVS-backed on ESP32, only writes when the value changes.
- ✅ **Battery-level icon on the display** — done. ADC on GPIO8 via 100k/47k divider, polled every 60 s, drawn as a small green/amber/red battery rectangle at the top of the round screen.
- 📐 **High-side P-MOSFET to power-gate the buck during deep sleep** — Pololu **#2811** ordered (SV variant, 4.5–20 V, fits 4×AA). On-shelf hold until parts arrive. Wiring in `V2_HARDWARE.md` §1.
- ✅ **Motion wake (pick-up to wake)** — landed in V1.1, AC-couple bug fixed in V1.1.1 (2026-05-01). I2C on GPIO17/18, INT1 on GPIO2, active-LOW. ACT_INACT_CTL = 0xF0 (AC-coupled, all 3 axes — gravity removed from threshold comparison). THRESH_ACT = 0x10 (~1 g delta = brisk pickup). DEVID, DATA_FORMAT, and ACT_INACT_CTL readbacks all logged at boot. `motion_int` binary_sensor and GPIO2 in `deep_sleep.esp32_ext1_wakeup.pins` are both live. Init is kicked from the `adxl_init_kick` interval (~5 s post-boot) because the on_boot priority-(-10) trigger never fires logs in this build.
- 📐 **Haptic feedback on long-press** — using **DRV2605L** module (ordered) + ERM coin motors (ordered). On-shelf hold until parts arrive. I2C-driven, 123 pre-loaded waveforms, shares I2C bus with ADXL345. Config in `V2_HARDWARE.md` §3. (2N7000 discrete driver kept as fallback.)
- 💡 Replace single-color randomizer with a curated palette
- ✅ ~~Confirm GC9A01 controller~~ — confirmed by photo.

Legend: ✅ done · 📐 spec ready, awaiting parts · 💡 idea

---

## 9. Files in this project folder

| File | Purpose |
|---|---|
| `PROJECT_MEMORY.md` | This document — V1 spec, wiring, workflow, decisions log. |
| `Esphome_template.yaml` | User's reusable ESPHome boilerplate (style/structure reference for all ESPHome code in this project). |
| `ledtyokkari_remote.yaml` | **V1 firmware.** Generated to match the template's structure (substitutions + sectioned headers + `!secret` WiFi/OTA). |
| `WIRING_DIAGRAM.svg` | Visual schematic — colour-coded wires for power / GND / ADC / SPI clock / SPI data / control / button signals. |
| `WIRING_GUIDE.md` | Step-by-step assembly guide with verification checks at each stage and a troubleshooting cheat sheet. |
| `V2_HARDWARE.md` | Spec sheet for V2 add-ons: P-MOSFET soft-latch, IMU/tilt wake, haptic feedback. Specific part numbers + ESPHome configs. |
| `MyComponents1.jpeg` / `MyComponents2.jpeg` | Photos of Marko's parts cabinet (drawer labels). Useful for picking parts without asking. |
| `esphome_logs/` | Most-recent ESPHome dashboard log dumps from runs/installs. New logs land here as Marko exports them. |
| `datasheets/` | Local copies of chip datasheets — `adxl345.pdf` (Rev. G, 36 pp), `GC9A01A.pdf` (192 pp), `drv2605l.pdf` (Rev. D, 76 pp). **Always read the relevant pages here before guessing at register semantics.** The V1.1 AC/DC bit bug would have been caught instantly with adxl345.pdf §"Activity/Inactivity Detection" (register 0x27 bit map) in front of us. |

---

*Last updated: 2026-05-01 — V1.2.1 firmware (RGB rows colour-coded by channel + font_med everywhere on the small-text indicators; sleep countdown bed scaled up to match). V1.1.1 motion-wake confirmed working in boot log #6.*
