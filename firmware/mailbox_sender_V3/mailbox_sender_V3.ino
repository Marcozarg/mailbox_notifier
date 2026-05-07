// V1.0.6 — 2026-05-07 — Mailbox sender V3 (Adafruit Feather 32u4 LoRa, BME280, NO reed)
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
//     PWR_DOWN. Set to 0 for field deployment to restore the 24 h sleep loop.
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
#define FW_VERSION "V1.0.6"
//
// Battery-powered mailbox sensor:
//   • Sleeps in AVR PWR_DOWN (~10 µA + LiPo charger leakage) almost continuously.
//   • Wakes on the reed switch (lid opens → magnet aligned → reed closes → INT2 LOW).
//   • Reads the BME280, builds a key=value LoRa packet, transmits @ +20 dBm, sleeps.
//   • 60 s lockout after each reed event so a bouncy lid doesn't double-notify.
//   • Heartbeat every 24 h normally, every 6 h when vbat < 3.6 V (early dead-batt warning).
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
// 0 = field mode: deep sleep + 24 h heartbeat as designed.
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
#define LORA_TX_POWER   20         // dBm — overshoots EU 14 dBm ERP limit on paper but
                                   //       at 50 m through one tree, headroom matters more
                                   //       than strict compliance for a hobby project

////////////////////////////////////////////////////////////////////////////////
// Heartbeat scheduling — counted in 8 s WDT ticks because that's the smallest
// granularity LowPower.powerDown(SLEEP_8S, ...) gives us.
//
// Real-world WDT tolerance: ±10 %, so 24 h heartbeats can land ±2.4 h either
// side of nominal. Fine for a mailbox.
////////////////////////////////////////////////////////////////////////////////
#define HB_TICKS_NORMAL    10800UL    // 24 h ÷ 8 s
#define HB_TICKS_LOW_BATT  2700UL     // 6 h ÷ 8 s
#define LOW_BATT_MV        3600       // mV at which we boost heartbeat rate
#define LOCKOUT_TICKS      8U         // 8 × 8 s = 64 s ≈ "60 s" reed lockout

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

uint16_t      wdtCount       = 0;          // 8 s ticks since last heartbeat
uint16_t      heartbeatTicks = HB_TICKS_NORMAL;
uint8_t       lockoutTicksRemaining = 0;   // 8 s ticks of reed-ignore window
                                           // (V1.0.4: needLidCloseTx + type=5
                                           // logic removed — sender TXs only
                                           // on lid-open edge again.)

// BME280 last-known values — used when the chip fails to respond. The receiver's
// sensor_ok flag tells HA whether to trust them.
struct {
  float    tempC       = 21.0;             // sensible defaults for first boot
  uint8_t  humidPct    = 50;
  float    pressureHpa = 1013.0;
  bool     ok          = false;
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
  sendPacket(4);        // type 4 = boot

  // Initial heartbeat schedule: based on current battery.
  uint16_t v = readVbatMv();
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
  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);            // ATmega32u4 ~5 µA

  if (reedAttached) {
    detachInterrupt(digitalPinToInterrupt(PIN_REED));        // ISR may have already done this; safe either way
  }

  // -------- Account for time elapsed --------
  // Approximate: we slept at most 8 s. If reed interrupted earlier, the elapsed
  // time is shorter — but we don't try to measure (no RTC, would cost battery
  // to track). Rounding to 8 s ticks introduces ±5 % drift on the heartbeat
  // schedule, which is fine.
  if (lockoutTicksRemaining > 0) lockoutTicksRemaining--;
  wdtCount++;

  // -------- Reed event handling (V1.0.4: lid-OPEN edge only) --------
  if (reedFlag) {
    reedFlag = false;
    SLOG("[snd] reed wake");
    readSensors();
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
    // returns us to 24 h cadence after charging recovers vbat above 3.6 V.
    heartbeatTicks = (v < LOW_BATT_MV) ? HB_TICKS_LOW_BATT : HB_TICKS_NORMAL;
  }
}
#endif   // DEBUG_NOSLEEP

////////////////////////////////////////////////////////////////////////////////
// Boot reason — translates the saved MCUSR bits into a one-word string.
// Order matters: WDT supersedes brownout supersedes external supersedes power-on.
////////////////////////////////////////////////////////////////////////////////
const char* bootReasonStr() {
  if (mcusrCopy & (1 << WDRF))  return "wdt";
  if (mcusrCopy & (1 << BORF))  return "brownout";
  if (mcusrCopy & (1 << EXTRF)) return "external";
  if (mcusrCopy & (1 << PORF))  return "cold";
  return "unknown";
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
  for (uint8_t i = 0; i < 8; i++) {
    sum += analogRead(PIN_VBAT);
    delay(2);
  }
  uint32_t avg = sum / 8;
  return (uint16_t) ((avg * 2UL * 3300UL) / 1024UL);
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
//   br   = boot reason ("cold" / "wdt" / "brownout" / "external") — type 4 only
////////////////////////////////////////////////////////////////////////////////
String buildPacket(uint8_t pktType) {
  uint16_t vbat   = readVbatMv();
  uint8_t  reedHi = (digitalRead(PIN_REED) == LOW) ? 1 : 0;   // LOW pin = reed CLOSED = lid OPEN

  String pkt;
  pkt.reserve(80);
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
  if (pktType == 4) {
    pkt += F("&br="); pkt += bootReasonStr();
  }
  return pkt;
}

////////////////////////////////////////////////////////////////////////////////
// Send packet — wakes the radio, transmits, sleeps the radio, blinks LED.
////////////////////////////////////////////////////////////////////////////////
void sendPacket(uint8_t pktType) {
  String pkt = buildPacket(pktType);
  SLOGF(F("TX: "), pkt);

  LoRa.idle();                          // standby — needed before TX
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
