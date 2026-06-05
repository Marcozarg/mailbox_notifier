// V2.1.1 — 2026-06-05 — Eliminate double vbat ADC read per TX event
//
// V2.1.1 changes:
//   • `cache` struct gains `vbatMv` field. `readVbatMv()` stores the result
//     there before returning. `buildPacket()` now reads `cache.vbatMv`
//     directly instead of calling `readVbatMv()` again — one ADC conversion
//     per TX event instead of two.
//   • Reed events and boot TX: added explicit `readVbatMv()` call before
//     `sendPacket()` so cache.vbatMv is always fresh (previously relied on
//     buildPacket() doing it; heartbeat path was unaffected).
//
// V2.1.0 — 2026-06-05 — Lower TX power for heartbeats; ADC sample cleanup
//
// V2.1.0 changes:
//   • Heartbeat packets (type=2/3) now transmit at +14 dBm via RFO pin
//     instead of +20 dBm PA_BOOST. Measured RSSI is −70 to −84 dBm;
//     dropping 6 dB yields worst-case ~−90 dBm, still 35 dB above SF9
//     sensitivity (~−125 dBm). TX current drops from ~120 mA → ~29 mA
//     for heartbeats (4× reduction). Reed and boot packets (type=1/4)
//     keep full +20 dBm PA_BOOST for reliability.
//     New constant: LORA_HB_TX_POWER 14 (RFO pin).
//   • readVbatMv(): removed 8× delay(2) between ADC samples (analogRead()
//     already blocks until conversion completes) and reduced sample count
//     from 8 → 4. Battery voltage changes slowly; 4 samples is plenty.
//
// V2.0.0 — 2026-06-02 — Version milestone: project documentation overhaul
//
// V2.0.0 changes:
//   • Version milestone bump to align with V2.0.0 receiver. No firmware changes.
//     Documentation restructured: README.md is now the canonical project reference,
//     HARDWARE.md (was docs/SENDER_HARDWARE.md) moved to repo root.
//
// V1.1.0 — 2026-05-22 — Mailbox sender V3 (Adafruit Feather 32u4 LoRa, BME280, NO reed)
//
// V1.1.0 changes:
//   • `&br=` (boot reason) now sent in EVERY packet, not just type=4 boot
//     packets. Pre-V1.1.0 the field only flew once per sender boot, so if HA
//     was offline at boot time (or the retained MQTT value was wiped by an
//     entity rename), HA showed `mailbox_sender_boot_reason = Unknown` until
//     the next sender restart — which on this device might be weeks away.
//     With br= on every packet HA always has the current value cached.
//     Cost: +6 bytes airtime per packet, well below the noise floor.
//   • `bootReasonStr()` labels expanded for HA-display readability and the
//     "MCUSR == 0" default changed from `"unknown"` (which HA renders as the
//     Unknown state) to `"normal"`. The 32u4's Caterina bootloader clears
//     MCUSR before `.init3` can read it on many resets, so the default fires
//     often in practice. New mapping:
//        PORF  → "power-on"      (was "cold")
//        EXTRF → "external reset"(was "external")
//        WDRF  → "watchdog"      (was "wdt")
//        BORF  → "brown-out"     (was "brownout")
//        none  → "normal"        (was "unknown")
//   • Companion receiver change: V1.2.1 translates the numeric `type=` field
//     into a text label ("mail" / "heartbeat" / "heartbeat (low batt)" /
//     "boot") before publishing to `mailbox/sender/last_packet_type`. No
//     wire-format change — the on-air `type=` is still 1/2/3/4.
//
// V1.0.9 changes:
//   • Energy option B — WDT tick stretched from 8 s → 32 s. Chip wakes 4× less
//     often, drops the per-tick MCU activity from ~10800 wakes/day to ~2700.
//     Implemented by calling LowPower.powerDown(SLEEP_8S, …) four times per
//     tick in a small `sleep32s()` helper (32u4's WDT prescaler caps at 8 s,
//     so we can't ask the hardware for a single 32 s sleep). Reed events
//     still wake immediately on INT2 LOW — `sleep32s()` bails out of the
//     inner loop as soon as `reedFlag` is set, so reed-trigger latency is
//     unchanged (≤ 8 s worst case, same as before).
//     Rescaled tick-count constants:
//        HB_TICKS_NORMAL    21600 → 5400  (still 48 h)
//        HB_TICKS_LOW_BATT  2700  → 675   (still 6 h)
//        LOCKOUT_TICKS      8     → 2     (still ≈ 64 s)
//     Long-term saving ≈ 0.1 mAh/day — modest but free.
//   • Sender now includes its FW string in every LoRa packet (new `&fw=`
//     field). The receiver V1.0.8 publishes this to `mailbox/sender_fw` so
//     the running sender version shows up as an entity on the Mailbox sensor
//     device card in HA. Packet grows by ~9 bytes (negligible airtime).
//
// V1.0.8 changes:
//   • Normal heartbeat cadence doubled: 24 h → 48 h (HB_TICKS_NORMAL 10800
//     → 21600). Halves the long-term heartbeat TX count, slightly extends
//     battery life. Low-battery heartbeat unchanged at 6 h — that's the
//     urgency cadence and shouldn't scale with the normal one.
//   • Receiver V1.0.7 stretches the sender_alive timeout to 98 h in lockstep
//     so the "sender dead?" alert still tolerates one missed heartbeat.
//
// V1.0.7 changes:
//   • Documentation only — antenna upgraded from the original 8.2 cm wire-stub
//     (~0 dBi) to an external 868 MHz stubby (~+2 dBi) attached via u.FL
//     pigtail. Antenna can now be routed outside the metal mailbox enclosure
//     for ~10–15 dB better RSSI/SNR than the previous in-box wire stub.
//     TX power intentionally LEFT at +20 dBm (see comment at LORA_TX_POWER).
//     No code/runtime change in this version — just the FW_VERSION stamp.
//
// V1.0.6 changes:
//   • Cosmetic — sketch and parent folder renamed from
//     `adafruit_Lora_32u4_Mailbox_Sender_V3` → `mailbox_sender_V3`.
//     Shorter, easier to type, mirrors `mailbox_receiver_V3` naming.
//     No behavioural change.
//
// V1.0.5 changes:
//   • Switched to PRODUCTION toggles. DEBUG_NOSLEEP=0, DEBUG_LED=0,
//     ENABLE_SERIAL=0. Field-tested 2026-05-03: RSSI ~ -58 dBm, SNR ~ 11 dB
//     from inside the mailbox to the Heltec on the window — plenty of margin.
//   • New `BOOT_UPLOAD_WINDOW_MS` (default 10 000 ms). After setup() finishes
//     (init + boot packet TX), the sketch holds the chip awake for 10 s
//     before dropping into the deep-sleep loop. During that window USB-CDC
//     stays enumerated, so a normal Arduino IDE upload (1200bps-touch auto-
//     reset) works without the Caterina double-tap dance.
//     Workflow: press Reset on the Feather, count to 3, click Upload.
//     Window only applies in field mode (DEBUG_NOSLEEP=0); DEBUG mode never
//     sleeps so it's not needed.
//
// V1.0.4 changes:
//   • REVERT V1.0.3 type=5 (lid-close TX) per user feedback. Sender goes back
//     to the V1.0.2 single-edge behaviour: TX only on lid-OPEN edge (mail
//     arrived). The V1.0.3 lid-close detection added complexity for a feature
//     the user found useless. Removed:
//       - Packet type 0x05
//       - needLidCloseTx state flag
//       - The lid-CLOSE polling branch in field-mode loop
//       - DEBUG_NOSLEEP edge-detection on falling edge (back to rising-only)
//     DEBUG_NOSLEEP debounce restored to 5 s (was briefly 1 s in V1.0.3).
//
// V1.0.3 changes (REVERTED — see V1.0.4 above):
//   • New packet type 0x05 = "lid closed".
//
// V1.0.2 changes:
//   • New sub-toggle DEBUG_HEARTBEAT_30S (default 0 = OFF). When DEBUG_NOSLEEP
//     is on, the loop no longer auto-fires a heartbeat every 30 s — only the
//     boot packet at startup + reed events while testing. Flip to 1 to get
//     the periodic heartbeat back for radio-link health checks.
//   • All visible version strings (boot SLOG banner) now derive from a single
//     FW_VERSION macro so they can't drift apart from the file header again.
//
// V1.0.1 changes:
//   • New build toggle DEBUG_NOSLEEP (default 1 for bench debugging). When on,
//     the chip stays awake forever, so USB-CDC Serial doesn't drop out under
//     PWR_DOWN. Set to 0 for field deployment to restore the 48 h sleep loop.
//   • In DEBUG_NOSLEEP mode, prints sensor readings once per second in
//     Arduino Serial Plotter format (label:value pairs) so the Plotter view
//     graphs vbat / temp / humid / pres / reed_open simultaneously.
//   • DEBUG_NOSLEEP loop uses a 5 s reed lockout (vs 60 s) for faster bench
//     iteration.
//   • Boot-time `while (!Serial && millis() < 3000)` so USB-CDC enumerates
//     before the first SLOG, otherwise the boot log was getting lost.
//   • Header expanded with explicit "[snd]" boot banner so it's obvious the
//     sketch came up.
//
// V1.0.0 was the initial release — see notes below.

// Single source of truth for the firmware version string. Used by the boot
// SLOG banner. Update this when bumping the version stamp at the top of the
// file so the runtime log matches the header without manual sync.
#define FW_VERSION "V2.1.1"
//
// Battery-powered mailbox sensor:
//   • Sleeps in AVR PWR_DOWN (~10 µA + LiPo charger leakage) almost continuously.
//   • Wakes on the reed switch (lid opens → magnet aligned → reed closes → INT2 LOW).
//   • Reads the BME280, builds a key=value LoRa packet, transmits @ +20 dBm, sleeps.
//   • 60 s lockout after each reed event so a bouncy lid doesn't double-notify.
//   • Heartbeat every 48 h normally, every 6 h when vbat < 3.6 V (early dead-batt warning).
//   • Boot packet on every cold start, including the AVR reset reason for diagnostics.
//
// Hardware reference: SENDER_HARDWARE.md in this folder. Pin map matches that doc exactly.
// Receiver pair: mailbox_receiver_V3.ino in the same folder.
//
// Versioning scheme (also applies to other coding projects):
//   V1.0.x = bug fix, comment, typo, cosmetic-only edit
//   V1.x   = feature change, behaviour tweak
//   V2     = major hardware/architecture change

////////////////////////////////////////////////////////////////////////////////
// Build-time toggles — flip BEFORE compiling for each deployment scenario.
////////////////////////////////////////////////////////////////////////////////
// 1 = blink the blue LED (D5) once per LoRa TX as visual confirmation.
// 0 = production. No pinMode/digitalWrite ever touches D5; ~3 mA awake-current saved.
//     Wire stays installed in the field, no de-soldering needed.
#define DEBUG_LED       0

// 1 = Serial.println debug output (requires USB).
// 0 = production. No Serial.* call compiles in. Saves a few mA from the USB block.
#define ENABLE_SERIAL   0

// 1 = bench-debug mode: NEVER deep-sleep. Stays awake forever, prints sensor
//     readings once a second in Serial Plotter format, treats reed events
//     with a 5 s lockout (instead of 60 s).
//     Use this when the sender "appears dead" — the 32u4's USB-CDC drops out
//     during PWR_DOWN sleep so you'll never see Serial output in the field
//     mode unless you're lucky with USB re-enumeration timing.
// 0 = field mode: deep sleep + 48 h heartbeat as designed.
#define DEBUG_NOSLEEP   0

// Sub-toggle inside DEBUG_NOSLEEP: 1 = also fire a heartbeat TX every 30 s
// for radio-link health checks. 0 = no automatic TX, only the boot packet at
// startup and reed events when you wave a magnet at the switch.
// Default 0 — Marko's preference while prototyping is reed-triggered only.
#define DEBUG_HEARTBEAT_30S 0

// V1.0.5: how long to hold the chip awake at the end of setup() before
// dropping into the deep-sleep loop. Only used in field mode (DEBUG_NOSLEEP=0).
// During this window USB-CDC stays enumerated → Arduino IDE auto-reset upload
// works normally → no Caterina double-tap dance for routine reflashes.
// Cost: 10 s × ~25 mA = 250 mAs once per cold boot. With ~1 boot/year in
// production this is utterly negligible vs the 2000 mAh battery.
#define BOOT_UPLOAD_WINDOW_MS  10000UL

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////
#include <SPI.h>
#include <LoRa.h>                  // Sandeep Mistry — RFM95 (SX1276) driver
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <LowPower.h>              // rocketscream — wraps PWR_DOWN + WDT cleanly
#include <EEPROM.h>
#include <avr/wdt.h>

////////////////////////////////////////////////////////////////////////////////
// Pin assignments — match SENDER_HARDWARE.md verbatim. Don't rewire just one
// of these without updating the other places.
////////////////////////////////////////////////////////////////////////////////
#define PIN_REED        0      // INT2 — wakes from PWR_DOWN on LOW level (reed closed = lid open)
#define PIN_BME_SDA     2      // I2C SDA (also INT1 — unused)
#define PIN_BME_SCL     3      // I2C SCL (also INT0 — unused; was previously the reed pin)
#define PIN_LED         5      // Blue debug LED — fired once per TX when DEBUG_LED=1
#define PIN_VBAT        A9     // On-board ÷2 LiPo divider

// LoRa pins (hardwired on the Feather 32u4 LoRa PCB — do not change)
#define LORA_CS         8
#define LORA_RST        4
#define LORA_IRQ        7

////////////////////////////////////////////////////////////////////////////////
// LoRa link parameters — MUST match the receiver exactly. Mismatch is the
// #1 reason for "the radios are alive but I see nothing on the OLED."
////////////////////////////////////////////////////////////////////////////////
#define LORA_FREQ       866E6      // EU 868 MHz ISM band
#define LORA_BW         250E3      // Hz
#define LORA_SF         9          // spreading factor 6..12
#define LORA_TX_POWER    20        // dBm PA_BOOST — reed (type=1) and boot (type=4) packets.
                                   // Keeps full power for the mail-arrived path where
                                   // reliability matters most. Measured RSSI: −70…−84 dBm.
#define LORA_HB_TX_POWER 14        // dBm RFO pin — heartbeats (type=2/3) only.
                                   // 6 dB less than LORA_TX_POWER → worst-case ~−90 dBm,
                                   // still 35 dB above SF9 sensitivity (~−125 dBm).
                                   // TX current: ~29 mA vs ~120 mA at +20 dBm (4× saving).

////////////////////////////////////////////////////////////////////////////////
// Heartbeat scheduling — counted in 32 s "ticks" (V1.0.9 energy option B).
// One tick = 4× LowPower.powerDown(SLEEP_8S, ...) calls back-to-back, because
// the AVR WDT prescaler caps at 8 s. The `sleep32s()` helper bails out early
// if `reedFlag` fires inside one of the four sub-sleeps, so reed-event
// latency stays at ≤ 8 s worst case.
//
// Real-world WDT tolerance: ±10 %, so 48 h heartbeats can land ±5 h either
// side of nominal. Fine for a mailbox.
////////////////////////////////////////////////////////////////////////////////
#define HB_TICKS_NORMAL    5400UL     // 48 h ÷ 32 s  (V1.0.9: was 21600 = 48 h ÷ 8 s)
#define HB_TICKS_LOW_BATT  675UL      // 6 h  ÷ 32 s  (V1.0.9: was 2700  = 6 h  ÷ 8 s)
#define LOW_BATT_MV        3600       // mV at which we boost heartbeat rate
#define LOCKOUT_TICKS      2U         // 2 × 32 s = 64 s ≈ "60 s" reed lockout (V1.0.9: was 8)

////////////////////////////////////////////////////////////////////////////////
// EEPROM map — 32u4 has 1024 bytes, rated ≥ 100 k erase/write cycles.
// Magic byte distinguishes "fresh chip" (0xFF everywhere) from initialised state
// so we don't have to treat 65535 as a sentinel.
////////////////////////////////////////////////////////////////////////////////
#define EE_MAGIC_ADDR      0
#define EE_BOOT_COUNT_ADDR 1     // uint16
#define EE_REED_COUNT_ADDR 3     // uint16
#define EE_MAGIC_VALUE     0xA5

////////////////////////////////////////////////////////////////////////////////
// Boot reason capture — runs BEFORE setup() via the .init3 section, copies
// MCUSR into a .noinit RAM byte so it survives the bootloader's clearing.
//
// Caterina (the Feather 32u4 bootloader) has been observed to leave WDRF/BORF
// intact through reset. The .init3 hook copies them out before any user code
// runs.
////////////////////////////////////////////////////////////////////////////////
uint8_t mcusrCopy __attribute__((section(".noinit")));
void getMCUSR(void) __attribute__((naked, used, section(".init3")));
void getMCUSR(void) {
  mcusrCopy = MCUSR;
  MCUSR = 0;
  wdt_disable();          // belt-and-braces — prevent WDT carrying over from a hung previous run
}

////////////////////////////////////////////////////////////////////////////////
// State
////////////////////////////////////////////////////////////////////////////////
volatile bool reedFlag      = false;       // set by ISR, cleared by main loop
uint8_t       seq           = 0;           // sequence counter, RAM-only, wraps every 256
                                           //   resets to 0 on every boot — receiver detects
                                           //   that via the type=4 boot packet and resets
                                           //   its dup-tracking accordingly.
uint16_t      bootCount     = 0;           // EEPROM-backed; ++ each cold boot
uint16_t      reedEventCount = 0;          // EEPROM-backed; ++ each reed TX

uint16_t      wdtCount       = 0;          // 32 s logical ticks since last heartbeat (V1.0.9)
uint16_t      heartbeatTicks = HB_TICKS_NORMAL;
uint8_t       lockoutTicksRemaining = 0;   // 32 s logical ticks of reed-ignore window (V1.0.9)
                                           // (V1.0.4: needLidCloseTx + type=5
                                           // logic removed — sender TXs only
                                           // on lid-open edge again.)

// Sensor cache — BME280 last-known values + most recent vbat reading.
// All fields updated before every sendPacket(); buildPacket() reads from here
// so there is exactly one ADC conversion per TX event (no double-read).
struct {
  float    tempC       = 21.0;             // sensible defaults for first boot
  uint8_t  humidPct    = 50;
  float    pressureHpa = 1013.0;
  bool     ok          = false;
  uint16_t vbatMv      = 3700;            // updated by readVbatMv()
} cache;

Adafruit_BME280 bme;

////////////////////////////////////////////////////////////////////////////////
// Logging — Serial only when ENABLE_SERIAL=1, otherwise compiles out entirely.
// Strings stored in PROGMEM via F() to save RAM (32u4 has only 2.5 KB).
////////////////////////////////////////////////////////////////////////////////
#if ENABLE_SERIAL
  #define SLOG(s)             Serial.println(F(s))
  // SLOGF prints "[snd] <label><value>" on one line. Always exactly 2 args:
  //   SLOGF(F("vbat_mv="), v)
  // The variadic version we tried first expanded to Serial.print(label, value)
  // which doesn't compile — Print::print is single-argument (or value+base).
  #define SLOGF(label, val)   do { Serial.print(F("[snd] ")); Serial.print(label); Serial.print(val); Serial.println(); } while (0)
#else
  #define SLOG(s)             do {} while (0)
  #define SLOGF(label, val)   do {} while (0)
#endif

////////////////////////////////////////////////////////////////////////////////
// Reed wake ISR — set flag, detach self (re-attached at top of loop only when
// it's safe to do so — see loop()). Must be tiny: no Serial, no I2C, no SPI.
////////////////////////////////////////////////////////////////////////////////
void reedWakeISR() {
  reedFlag = true;
  detachInterrupt(digitalPinToInterrupt(PIN_REED));
}

////////////////////////////////////////////////////////////////////////////////
// Forward declarations
////////////////////////////////////////////////////////////////////////////////
const char* bootReasonStr();
void initEeprom();
void initBme();
void initLora();
void readSensors();
uint16_t readVbatMv();
String buildPacket(uint8_t pktType);
void sendPacket(uint8_t pktType);
void blinkLed();
bool sleep32s();

////////////////////////////////////////////////////////////////////////////////
// Setup — runs exactly once on every cold/wake boot. Sends a type=4 boot
// packet so HA knows the sender just restarted (and why — boot_reason field).
////////////////////////////////////////////////////////////////////////////////
void setup() {
#if ENABLE_SERIAL
  Serial.begin(115200);
  // Wait up to 3 s for USB-CDC enumeration so the boot log isn't lost. Won't
  // block forever — booting without USB is a normal field condition. The 32u4
  // takes ~500–1500 ms to enumerate after reset on most hosts.
  while (!Serial && millis() < 3000) { /* spin */ }
  delay(50);
#endif
  SLOG("");
  SLOG("============================================");
  SLOG("[snd] Mailbox sender " FW_VERSION " boot");
#if DEBUG_NOSLEEP
  SLOG("[snd] DEBUG_NOSLEEP=1  (chip will NOT sleep)");
  #if DEBUG_HEARTBEAT_30S
    SLOG("[snd] DEBUG_HEARTBEAT_30S=1  (TX every 30 s)");
  #else
    SLOG("[snd] DEBUG_HEARTBEAT_30S=0  (reed-only TX)");
  #endif
#endif
  SLOG("============================================");
  SLOGF(F("MCUSR copy: 0x"), String(mcusrCopy, HEX));

  // Pins
  pinMode(PIN_REED, INPUT_PULLUP);  // inverted-mounted NO reed: HIGH when lid closed
#if DEBUG_LED
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
#endif

  initEeprom();
  bootCount++;
  EEPROM.put(EE_BOOT_COUNT_ADDR, bootCount);
  SLOGF(F("boot_count="), bootCount);
  SLOGF(F("boot_reason="), bootReasonStr());

  initBme();
  readSensors();        // first read for the boot packet
  initLora();
  uint16_t v = readVbatMv();   // populate cache.vbatMv before boot TX; reuse for hb schedule
  sendPacket(4);        // type 4 = boot

  // Initial heartbeat schedule: based on current battery.
  heartbeatTicks = (v < LOW_BATT_MV) ? HB_TICKS_LOW_BATT : HB_TICKS_NORMAL;
  SLOGF(F("vbat_mv="), v);
  SLOGF(F("hb_ticks="), heartbeatTicks);

#if !DEBUG_NOSLEEP
  // V1.0.5 upload window — hold USB-CDC alive so the IDE's normal auto-reset
  // upload trick (1200bps touch) can grab the bootloader without forcing the
  // user to do a manual Caterina double-tap. delay() blocks the main loop but
  // does NOT disable USB; the host stays enumerated for the full 10 s.
  // After the window we drop into the deep-sleep loop and USB-CDC dies until
  // the next reset.
  SLOG("[snd] upload window 10s — Reset+Upload now to flash easily");
  delay(BOOT_UPLOAD_WINDOW_MS);
  SLOG("[snd] entering sleep loop");
#endif
}

////////////////////////////////////////////////////////////////////////////////
// Loop — sleep, wake, decide, sleep again. Never returns.
//
// Sleep strategy (field mode, DEBUG_NOSLEEP=0):
//   - If lid is closed (REED pin reads HIGH) AND we're not in lockout:
//       attach reed wake interrupt → can wake either on reed OR on WDT.
//   - If lid is held open (pin LOW) OR we're inside the post-event lockout:
//       no reed wake → WDT-only sleep, lid-stuck-open or postman-takes-ages
//       can't double-trigger the alert.
//
// Bench-debug mode (DEBUG_NOSLEEP=1):
//   - Stays awake forever. delay()s in 1 s steps so USB Serial doesn't drop.
//   - Prints all sensor + state values once per second in Arduino Serial
//     Plotter format (label:value space-separated).
//   - TX a heartbeat every 30 s so the receiver sees regular activity.
//   - Reed events: 5 s lockout (instead of 60 s) for faster bench iteration.
////////////////////////////////////////////////////////////////////////////////
#if DEBUG_NOSLEEP
void loop() {
  static unsigned long lastPlotMs    = 0;
  static unsigned long lastHbTxMs    = 0;
  static unsigned long lastReedTxMs  = 0;
  static uint8_t       lastReedRead  = 0;

  // ---- Sensor / state plot once per second ----
  if (millis() - lastPlotMs >= 1000) {
    lastPlotMs = millis();
    readSensors();
    uint16_t vbat   = readVbatMv();
    uint8_t  reedHi = (digitalRead(PIN_REED) == LOW) ? 1 : 0;   // 1 = lid OPEN

    // Arduino Serial Plotter format: "label:value label:value …" on one line.
    // The Plotter auto-creates one trace per label.
    Serial.print(F("vbat_mV:"));    Serial.print(vbat);
    Serial.print(F(" temp_C:"));    Serial.print(cache.tempC, 2);
    Serial.print(F(" humid_pct:")); Serial.print(cache.humidPct);
    Serial.print(F(" pres_hPa:"));  Serial.print(cache.pressureHpa, 1);
    Serial.print(F(" reed_open:")); Serial.print(reedHi);
    Serial.print(F(" sensor_ok:")); Serial.println(cache.ok ? 1 : 0);
  }

  // ---- Reed event detection (V1.0.4: rising edge only — lid-OPEN event).
  //      5 s debounce window so a single bouncy lid event doesn't double-fire.
  uint8_t reedNow = (digitalRead(PIN_REED) == LOW) ? 1 : 0;
  if (reedNow && !lastReedRead && (millis() - lastReedTxMs > 5000)) {
    SLOG("[snd] DEBUG: reed event → TX type=1");
    readSensors();
    readVbatMv();
    sendPacket(1);
    reedEventCount++;
    EEPROM.put(EE_REED_COUNT_ADDR, reedEventCount);
    lastReedTxMs = millis();
  }
  lastReedRead = reedNow;

#if DEBUG_HEARTBEAT_30S
  // ---- Heartbeat every 30 s for visible TX activity ----
  // Off by default in V1.0.2 — flip DEBUG_HEARTBEAT_30S to 1 to re-enable when
  // you want a periodic packet to verify the radio link without poking the reed.
  if (millis() - lastHbTxMs >= 30000) {
    SLOG("[snd] DEBUG: 30s heartbeat → TX type=2");
    readSensors();
    uint16_t v = readVbatMv();
    uint8_t  hbType = (v < LOW_BATT_MV) ? 3 : 2;
    sendPacket(hbType);
    lastHbTxMs = millis();
  }
#else
  (void) lastHbTxMs;   // silence "unused variable" warning when sub-toggle is off
#endif

  delay(50);   // pace the loop; reed-edge polling latency stays well under 100 ms
}

#else   // ============== FIELD MODE — original sleep-based loop ==============

void loop() {
  // -------- Arm sleep mode --------
  bool reedAttached = false;
  if (lockoutTicksRemaining == 0 && digitalRead(PIN_REED) == HIGH) {
    // Lid closed, no lockout — fine to wake on next reed event.
    attachInterrupt(digitalPinToInterrupt(PIN_REED), reedWakeISR, LOW);
    reedAttached = true;
  }

  LoRa.sleep();                                              // RFM95 ~0.1 µA
  sleep32s();                                                // V1.0.9: 4× SLEEP_8S, bails on reedFlag

  if (reedAttached) {
    detachInterrupt(digitalPinToInterrupt(PIN_REED));        // ISR may have already done this; safe either way
  }

  // -------- Account for time elapsed --------
  // Approximate: we slept at most 32 s. If reed interrupted earlier, the elapsed
  // time is shorter — but we don't try to measure (no RTC, would cost battery
  // to track). Rounding to 32 s ticks introduces ±5 % drift on the heartbeat
  // schedule, which is fine.
  if (lockoutTicksRemaining > 0) lockoutTicksRemaining--;
  wdtCount++;

  // -------- Reed event handling (V1.0.4: lid-OPEN edge only) --------
  if (reedFlag) {
    reedFlag = false;
    SLOG("[snd] reed wake");
    readSensors();
    readVbatMv();                           // populate cache.vbatMv for buildPacket()
    sendPacket(1);                          // type 1 = reed event (mail arrived)
    reedEventCount++;
    EEPROM.put(EE_REED_COUNT_ADDR, reedEventCount);
    lockoutTicksRemaining = LOCKOUT_TICKS;  // start the 60 s ignore window
  }

  // -------- Heartbeat --------
  if (wdtCount >= heartbeatTicks) {
    SLOG("[snd] heartbeat wake");
    readSensors();
    uint16_t v = readVbatMv();
    uint8_t  hbType = (v < LOW_BATT_MV) ? 3 : 2;
    sendPacket(hbType);
    wdtCount = 0;
    // Re-evaluate cadence after each heartbeat so a freshly-charged battery
    // returns us to 48 h cadence after charging recovers vbat above 3.6 V.
    heartbeatTicks = (v < LOW_BATT_MV) ? HB_TICKS_LOW_BATT : HB_TICKS_NORMAL;
  }
}
#endif   // DEBUG_NOSLEEP

////////////////////////////////////////////////////////////////////////////////
// Boot reason — translates the saved MCUSR bits into a human-readable string
// that's published verbatim to HA. Order matters: WDT supersedes brown-out
// supersedes external supersedes power-on (those bits can be set together
// after a chained reset, and the more "interesting" cause wins).
//
// V1.1.0: default changed from "unknown" → "normal". The 32u4's Caterina
// bootloader clears MCUSR (at least WDRF, often more) before `.init3` runs,
// so the no-bits path fires on most real-world resets. "normal" reflects
// "we restarted cleanly, no diagnostic bit survived" — way more useful than
// "unknown" in HA.
////////////////////////////////////////////////////////////////////////////////
const char* bootReasonStr() {
  if (mcusrCopy & (1 << WDRF))  return "watchdog";
  if (mcusrCopy & (1 << BORF))  return "brown-out";
  if (mcusrCopy & (1 << EXTRF)) return "external reset";
  if (mcusrCopy & (1 << PORF))  return "power-on";
  return "normal";
}

////////////////////////////////////////////////////////////////////////////////
// EEPROM — first-boot detection via magic byte. Without the magic, we'd treat
// a freshly-flashed AVR (0xFF everywhere) as having boot_count = 65535, which
// would survive but look bizarre.
////////////////////////////////////////////////////////////////////////////////
void initEeprom() {
  uint8_t magic = EEPROM.read(EE_MAGIC_ADDR);
  if (magic != EE_MAGIC_VALUE) {
    // First boot on this chip — initialise.
    EEPROM.write(EE_MAGIC_ADDR, EE_MAGIC_VALUE);
    bootCount     = 0;
    reedEventCount = 0;
    EEPROM.put(EE_BOOT_COUNT_ADDR, bootCount);
    EEPROM.put(EE_REED_COUNT_ADDR, reedEventCount);
    SLOG("[snd] EEPROM initialised");
  } else {
    EEPROM.get(EE_BOOT_COUNT_ADDR, bootCount);
    EEPROM.get(EE_REED_COUNT_ADDR, reedEventCount);
  }
}

////////////////////////////////////////////////////////////////////////////////
// BME280 — forced-mode setup. Forced mode means "wake → take one measurement
// → sleep". Continuous-mode would draw ~0.4 mA continuously; forced-mode is
// the right choice for a battery sensor that only reads ~3 times/day.
////////////////////////////////////////////////////////////////////////////////
void initBme() {
  Wire.begin();
  bool ok = bme.begin(0x76);              // SDO tied to GND on the 6-pin module
  if (!ok) {
    SLOG("[snd] BME280 init FAILED — sensor_ok will be 0");
    cache.ok = false;
    return;
  }
  // Forced mode + reasonable oversampling. 1× on each axis is adequate for
  // mailbox-grade accuracy and minimises measurement time (~10 ms).
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,    // temp
                  Adafruit_BME280::SAMPLING_X1,    // pressure
                  Adafruit_BME280::SAMPLING_X1,    // humidity
                  Adafruit_BME280::FILTER_OFF);
  cache.ok = true;
  SLOG("[snd] BME280 OK");
}

void readSensors() {
  if (!cache.ok) {
    // Try to recover — maybe the bus glitched at boot. Re-init silently.
    cache.ok = bme.begin(0x76);
    if (!cache.ok) return;                        // give up; receiver gets sensor_ok=0
  }
  // Forced-mode read: tell the chip to take ONE measurement, then sleep.
  if (!bme.takeForcedMeasurement()) {
    SLOG("[snd] BME280 forced-mode timeout — using cached values");
    cache.ok = false;
    return;
  }
  cache.tempC       = bme.readTemperature();
  cache.humidPct    = (uint8_t) constrain((int) round(bme.readHumidity()), 0, 100);
  cache.pressureHpa = bme.readPressure() / 100.0f;
  cache.ok          = true;
}

////////////////////////////////////////////////////////////////////////////////
// LoRa init — called once in setup, then loop() just toggles sleep/idle.
////////////////////////////////////////////////////////////////////////////////
void initLora() {
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    if (LoRa.begin(LORA_FREQ)) {
      LoRa.setSpreadingFactor(LORA_SF);
      LoRa.setSignalBandwidth(LORA_BW);
      LoRa.setTxPower(LORA_TX_POWER);
      LoRa.enableCrc();                  // hardware-CRC: receiver gets bit-perfect packets or nothing
      LoRa.sleep();
      SLOG("[snd] LoRa OK");
      return;
    }
    SLOGF(F("LoRa init failed, retry "), attempt);
    delay(200);
  }
  SLOG("[snd] LoRa init FAILED after 3 retries — entering sleep loop anyway");
  // We continue running; loop() will keep retrying via initLora() if we ever
  // re-call it. For now, don't TX. WDT will eventually reset us if hangs.
}

////////////////////////////////////////////////////////////////////////////////
// Battery voltage — Feather has a built-in ÷2 divider on A9 (= D9 = PB5).
// Reference is the chip's 3.3 V rail, ADC is 10-bit (0..1023).
//   raw * 2 (undo divider) * 3300 mV / 1024 counts = mV
// Average 8 samples to suppress ADC noise.
////////////////////////////////////////////////////////////////////////////////
uint16_t readVbatMv() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) {
    sum += analogRead(PIN_VBAT);  // analogRead blocks until conversion done; no delay needed
  }
  uint32_t avg = sum / 4;
  cache.vbatMv = (uint16_t)((avg * 2UL * 3300UL) / 1024UL);
  return cache.vbatMv;
}

////////////////////////////////////////////////////////////////////////////////
// Build packet — key=value, ASCII. ~70 bytes worst case.
//
// Field key meanings (kept short to save airtime):
//   id   = device ID, always "AA"
//   type = 1 reed (mail arrived) / 2 heartbeat / 3 low-batt heartbeat / 4 boot
//   seq  = packet sequence (0..255 wraps; resets to 0 on boot)
//   t    = temperature °C
//   h    = humidity %
//   p    = pressure hPa
//   v    = battery voltage mV
//   r    = reed state at TX time (0 = closed/lid-shut, 1 = open/lid-open)
//   sok  = BME280 sensor_ok flag
//   boot = boot count (uint16)
//   up   = uptime in minutes since this boot
//   br   = boot reason ("power-on" / "external reset" / "watchdog" / "brown-out" / "normal")
//          — V1.1.0+: in every packet, not just type=4 (so HA always has the current value)
//   fw   = firmware version string (V1.0.9: new — receiver publishes to HA)
////////////////////////////////////////////////////////////////////////////////
String buildPacket(uint8_t pktType) {
  uint16_t vbat   = cache.vbatMv;   // populated by readVbatMv() before every sendPacket()
  uint8_t  reedHi = (digitalRead(PIN_REED) == LOW) ? 1 : 0;   // LOW pin = reed CLOSED = lid OPEN

  String pkt;
  pkt.reserve(128);   // V1.0.9: was 80; new &fw= field pushes type=4 boot
                      // packets to ~107 chars worst case, give a margin.
  pkt  = F("id=AA");
  pkt += F("&type="); pkt += pktType;
  pkt += F("&seq=");  pkt += seq;
  pkt += F("&t=");    pkt += String(cache.tempC,       2);
  pkt += F("&h=");    pkt += cache.humidPct;
  pkt += F("&p=");    pkt += String(cache.pressureHpa, 1);
  pkt += F("&v=");    pkt += vbat;
  pkt += F("&r=");    pkt += reedHi;
  pkt += F("&sok=");  pkt += (cache.ok ? 1 : 0);
  pkt += F("&boot="); pkt += bootCount;
  pkt += F("&up=");   pkt += (uint32_t) (millis() / 60000UL);
  // V1.1.0: `&br=` in every packet (was type=4 only). +6 bytes airtime per
  // packet, but HA always has the current boot reason cached — survives MQTT
  // outages, entity renames, and broker restarts without showing "Unknown".
  pkt += F("&br=");   pkt += bootReasonStr();
  // V1.0.9: append sender firmware version. Receiver V1.1.0+ publishes this
  // to `mailbox/sender/version` (V1.0.8–V1.0.9 receivers used the pre-rename
  // `mailbox/sender_fw` topic) → HA entity, so the sender's running version
  // is visible on the device card without having to read the OLED.
  pkt += F("&fw=");   pkt += FW_VERSION;
  return pkt;
}

////////////////////////////////////////////////////////////////////////////////
// Send packet — wakes the radio, transmits, sleeps the radio, blinks LED.
////////////////////////////////////////////////////////////////////////////////
void sendPacket(uint8_t pktType) {
  String pkt = buildPacket(pktType);
  SLOGF(F("TX: "), pkt);

  LoRa.idle();                          // standby — needed before TX
  // Heartbeats use RFO pin at reduced power; reed/boot keep full PA_BOOST.
  if (pktType == 2 || pktType == 3) {
    LoRa.setTxPower(LORA_HB_TX_POWER, PA_OUTPUT_RFO_PIN);
  } else {
    LoRa.setTxPower(LORA_TX_POWER);
  }
  LoRa.beginPacket();
  LoRa.print(pkt);
  LoRa.endPacket();                     // blocks until TX done (~190 ms @ SF9/250k for 70 B)
  LoRa.sleep();                         // back to ~0.1 µA

  blinkLed();
  seq++;                                // wraps via uint8_t arithmetic
}

////////////////////////////////////////////////////////////////////////////////
// LED blink — single 100 ms flash, only when DEBUG_LED is enabled at compile.
// In production builds this function is empty.
////////////////////////////////////////////////////////////////////////////////
void blinkLed() {
#if DEBUG_LED
  digitalWrite(PIN_LED, HIGH);
  delay(100);
  digitalWrite(PIN_LED, LOW);
#endif
}

////////////////////////////////////////////////////////////////////////////////
// sleep32s — one logical "tick" of the energy-budget clock (V1.0.9).
//
// The 32u4's WDT prescaler caps at 8 s, so LowPower.powerDown(SLEEP_8S, …) is
// the longest sleep the hardware can do in a single call. We chain four of
// them to get 32 s with one tick of bookkeeping.
//
// If the reed interrupt fires during one of the four sub-sleeps, the ISR sets
// reedFlag and we bail out immediately — reed-event latency stays at ≤ 8 s
// worst case, same as the V1.0.8 behaviour.
//
// Returns true if reedFlag is set on exit (reed wake), false otherwise (full
// 32 s WDT-only sleep). The caller doesn't currently use the return value but
// it's there for future explicit-wake-source tracking.
////////////////////////////////////////////////////////////////////////////////
bool sleep32s() {
  for (uint8_t i = 0; i < 4; i++) {
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);   // ATmega32u4 ~5 µA
    if (reedFlag) return true;                        // reed wake — let loop() handle it
  }
  return false;
}
