// V1.0.0 — 2026-06-27 — Feather M0 RFM95 port of mailbox sender
//
// V1.0.0 changes:
//   • New file — port of mailbox_sender.ino (32u4 V2.3.1) to Adafruit Feather M0
//     with RFM95 LoRa Radio (ATSAMD21G18 + RFM95 SX1276). Functional parity:
//     AES-128-CTR encryption, 48 h heartbeat, reed wake, BME280, battery voltage.
//     All LoRa parameters and packet format unchanged — receiver compatible.
//   • SAMD21 platform differences from 32u4:
//       LORA_IRQ: 7 → 3          (DIO0 wired to pin 3 on Feather M0 RFM95 PCB)
//       PIN_LED: 5 → 13          (built-in red LED on Feather M0)
//       Sleep: LowPower → Adafruit_SleepyDog (SAMD21 WDT standby mode)
//       Storage: EEPROM → FlashStorage (emulated in SAMD21 Flash; no hardware EEPROM)
//       Boot reason: MCUSR + .init3 hook → PM->RCAUSE.reg (SAMD21 Power Manager)
//       ADC: analogReadResolution(10) set in setup() — keeps vbat formula (÷1024) unchanged
//       AES S-box: PROGMEM/pgm_read_byte removed (ARM: Flash/RAM share address space)
//       No disableUsb() — SAMD21 standby handles USB power naturally
//       No ACSR = (1<<ACD) — AVR-specific analog comparator register
//
// Note: copy arduino_secrets.h from mailbox_sender/ — same LORA_AES_KEY required.

#define FW_VERSION "V1.0.0"
//
// Battery-powered mailbox sensor (Feather M0 variant):
//   • Sleeps in SAMD21 STANDBY (~5 µA) almost continuously.
//   • Wakes on the reed switch (lid opens → magnet aligned → reed closes → INT fires).
//   • Reads the BME280, builds a key=value LoRa packet, transmits, sleeps.
//   • 60 s lockout after each reed event.
//   • Heartbeat every 48 h normally, every 6 h when vbat < 3.6 V.
//   • Boot packet on every cold start including reset reason.

////////////////////////////////////////////////////////////////////////////////
// Build-time toggles
////////////////////////////////////////////////////////////////////////////////
#define DEBUG_LED           0
#define ENABLE_SERIAL       0
#define DEBUG_NOSLEEP       0
#define DEBUG_HEARTBEAT_30S 0
#define BOOT_UPLOAD_WINDOW_MS  20000UL

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////
#include <SPI.h>
#include <LoRa.h>                   // Sandeep Mistry — RFM95 (SX1276) driver
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SleepyDog.h>     // SAMD21 WDT standby sleep (replaces LowPower)
#include <FlashStorage.h>           // SAMD21 Flash-backed persistent storage (replaces EEPROM)
#include "arduino_secrets.h"        // LORA_AES_KEY (gitignored)

////////////////////////////////////////////////////////////////////////////////
// Pin assignments — see HARDWARE.md for wiring. Feather M0 header is pin-
// compatible with Feather 32u4 except LORA_IRQ (DIO0).
////////////////////////////////////////////////////////////////////////////////
#define PIN_REED        0      // Reed switch, INPUT_PULLUP, LOW = reed closed = lid open
#define PIN_BME_SDA     20     // Hardware SDA on Feather M0 (same header position as D2 on 32u4)
#define PIN_BME_SCL     21     // Hardware SCL on Feather M0 (same header position as D3 on 32u4)
#define PIN_LED         13     // Built-in red LED on Feather M0 (was D5 on 32u4)
#define PIN_VBAT        A9     // On-board ÷2 LiPo divider — same as 32u4

// LoRa pins (hardwired on Feather M0 RFM95 PCB)
#define LORA_CS         8
#define LORA_RST        4
#define LORA_IRQ        3      // DIO0 on M0 — different from 32u4 (was 7)

////////////////////////////////////////////////////////////////////////////////
// LoRa link parameters — identical to 32u4 sender, must match receiver exactly.
////////////////////////////////////////////////////////////////////////////////
#define LORA_FREQ        866E6
#define LORA_BW          250E3
#define LORA_SF          9
#define LORA_TX_POWER    20        // dBm PA_BOOST — reed (type=1) and boot (type=4)
#define LORA_HB_TX_POWER 14        // dBm RFO pin  — heartbeats (type=2/3); 4× TX current saving

////////////////////////////////////////////////////////////////////////////////
// Heartbeat scheduling — 32 s ticks; identical to 32u4 sender.
// One tick = Watchdog.sleep(8000) × 4 (SAMD21 WDT max ≈ 16 s, we use 8 s).
////////////////////////////////////////////////////////////////////////////////
#define HB_TICKS_NORMAL    5400UL  // 48 h ÷ 32 s
#define HB_TICKS_LOW_BATT  675UL   //  6 h ÷ 32 s
#define LOW_BATT_MV        3600
#define LOCKOUT_TICKS      2U      // 2 × 32 s ≈ 64 s reed lockout

////////////////////////////////////////////////////////////////////////////////
// Flash storage (SAMD21 has no hardware EEPROM — use last Flash pages instead)
////////////////////////////////////////////////////////////////////////////////
typedef struct {
  uint8_t  magic;
  uint16_t bootCount;
  uint16_t reedCount;
} EEData;
FlashStorage(eeStorage, EEData);
#define EE_MAGIC_VALUE  0xA5

////////////////////////////////////////////////////////////////////////////////
// State
////////////////////////////////////////////////////////////////////////////////
volatile bool reedFlag        = false;
uint8_t       seq             = 0;
uint16_t      bootCount       = 0;
uint16_t      reedEventCount  = 0;
uint16_t      wdtCount        = 0;
uint16_t      heartbeatTicks  = HB_TICKS_NORMAL;
uint8_t       lockoutTicksRemaining = 0;

struct {
  float    tempC        = 21.0;
  uint8_t  humidPct     = 50;
  float    pressureHpa  = 1013.0;
  bool     ok           = false;
  uint16_t vbatMv       = 3700;
} cache;

Adafruit_BME280 bme;

////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////
#if ENABLE_SERIAL
  #define SLOG(s)           Serial.println(F(s))
  #define SLOGF(label, val) do { Serial.print(F("[snd] ")); Serial.print(label); Serial.print(val); Serial.println(); } while (0)
#else
  #define SLOG(s)           do {} while (0)
  #define SLOGF(label, val) do {} while (0)
#endif

////////////////////////////////////////////////////////////////////////////////
// Reed wake ISR
////////////////////////////////////////////////////////////////////////////////
void reedWakeISR() {
  reedFlag = true;
  detachInterrupt(digitalPinToInterrupt(PIN_REED));
}

////////////////////////////////////////////////////////////////////////////////
// Forward declarations
////////////////////////////////////////////////////////////////////////////////
const char* bootReasonStr();
void initFlash();
void saveFlash();
void initBme();
void initLora();
void readSensors();
uint16_t readVbatMv();
String buildPacket(uint8_t pktType);
void sendPacket(uint8_t pktType);
void blinkLed();
bool sleep32s();

////////////////////////////////////////////////////////////////////////////////
// Setup
////////////////////////////////////////////////////////////////////////////////
void setup() {
  analogReadResolution(10);   // match 32u4 10-bit ADC; keeps vbat formula (÷1024) unchanged

#if ENABLE_SERIAL
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  delay(50);
#endif
  SLOG("");
  SLOG("============================================");
  SLOG("[snd] Mailbox sender M0 " FW_VERSION " boot");
#if DEBUG_NOSLEEP
  SLOG("[snd] DEBUG_NOSLEEP=1  (chip will NOT sleep)");
  #if DEBUG_HEARTBEAT_30S
    SLOG("[snd] DEBUG_HEARTBEAT_30S=1  (TX every 30 s)");
  #else
    SLOG("[snd] DEBUG_HEARTBEAT_30S=0  (reed-only TX)");
  #endif
#endif
  SLOG("============================================");

  pinMode(PIN_REED, INPUT_PULLUP);
#if DEBUG_LED
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
#endif

  initFlash();
  bootCount++;
  saveFlash();
  SLOGF(F("boot_count="), bootCount);
  SLOGF(F("boot_reason="), bootReasonStr());

  initBme();
  readSensors();
  initLora();
  uint16_t v = readVbatMv();
  sendPacket(4);

  heartbeatTicks = (v < LOW_BATT_MV) ? HB_TICKS_LOW_BATT : HB_TICKS_NORMAL;
  SLOGF(F("vbat_mv="), v);
  SLOGF(F("hb_ticks="), heartbeatTicks);

#if !DEBUG_NOSLEEP
  SLOG("[snd] upload window 20s — reset + upload now to flash easily");
  delay(BOOT_UPLOAD_WINDOW_MS);
  SLOG("[snd] entering sleep loop");
  USBDevice.detach();   // disconnect D+; PHY drops to ~0 mA. To reflash: double-tap reset.
#endif
}

////////////////////////////////////////////////////////////////////////////////
// Loop
////////////////////////////////////////////////////////////////////////////////
#if DEBUG_NOSLEEP
void loop() {
  static unsigned long lastPlotMs   = 0;
  static unsigned long lastHbTxMs   = 0;
  static unsigned long lastReedTxMs = 0;
  static uint8_t       lastReedRead = 0;

  if (millis() - lastPlotMs >= 1000) {
    lastPlotMs = millis();
    readSensors();
    uint16_t vbat  = readVbatMv();
    uint8_t reedHi = (digitalRead(PIN_REED) == LOW) ? 1 : 0;
    Serial.print(F("vbat_mV:"));    Serial.print(vbat);
    Serial.print(F(" temp_C:"));    Serial.print(cache.tempC, 2);
    Serial.print(F(" humid_pct:")); Serial.print(cache.humidPct);
    Serial.print(F(" pres_hPa:"));  Serial.print(cache.pressureHpa, 1);
    Serial.print(F(" reed_open:")); Serial.print(reedHi);
    Serial.print(F(" sensor_ok:")); Serial.println(cache.ok ? 1 : 0);
  }

  uint8_t reedNow = (digitalRead(PIN_REED) == LOW) ? 1 : 0;
  if (reedNow && !lastReedRead && (millis() - lastReedTxMs > 5000)) {
    SLOG("[snd] DEBUG: reed event → TX type=1");
    readSensors();
    readVbatMv();
    sendPacket(1);
    reedEventCount++;
    saveFlash();
    lastReedTxMs = millis();
  }
  lastReedRead = reedNow;

#if DEBUG_HEARTBEAT_30S
  if (millis() - lastHbTxMs >= 30000) {
    SLOG("[snd] DEBUG: 30s heartbeat → TX type=2");
    readSensors();
    uint16_t v   = readVbatMv();
    uint8_t hbType = (v < LOW_BATT_MV) ? 3 : 2;
    sendPacket(hbType);
    lastHbTxMs = millis();
  }
#else
  (void) lastHbTxMs;
#endif

  delay(50);
}

#else   // ====================== FIELD MODE ======================

void loop() {
  bool reedAttached = false;
  if (lockoutTicksRemaining == 0 && digitalRead(PIN_REED) == HIGH) {
    attachInterrupt(digitalPinToInterrupt(PIN_REED), reedWakeISR, FALLING);
    reedAttached = true;
  }

  LoRa.sleep();
  sleep32s();   // 4 × Watchdog.sleep(8000); bails on reedFlag

  if (reedAttached) {
    detachInterrupt(digitalPinToInterrupt(PIN_REED));
  }

  if (lockoutTicksRemaining > 0) lockoutTicksRemaining--;
  wdtCount++;

  if (reedFlag) {
    reedFlag = false;
    SLOG("[snd] reed wake");
    readSensors();
    readVbatMv();
    sendPacket(1);
    reedEventCount++;
    saveFlash();
    lockoutTicksRemaining = LOCKOUT_TICKS;
  }

  if (wdtCount >= heartbeatTicks) {
    SLOG("[snd] heartbeat wake");
    readSensors();
    uint16_t v   = readVbatMv();
    uint8_t hbType = (v < LOW_BATT_MV) ? 3 : 2;
    sendPacket(hbType);
    wdtCount = 0;
    heartbeatTicks = (v < LOW_BATT_MV) ? HB_TICKS_LOW_BATT : HB_TICKS_NORMAL;
  }
}
#endif   // DEBUG_NOSLEEP

////////////////////////////////////////////////////////////////////////////////
// Boot reason — SAMD21 Power Manager RCAUSE register (replaces AVR MCUSR).
// Available at any time; no .init3 hook needed on ARM.
////////////////////////////////////////////////////////////////////////////////
const char* bootReasonStr() {
  uint8_t rc = PM->RCAUSE.reg;
  if (rc & PM_RCAUSE_WDT)   return "watchdog";
  if (rc & PM_RCAUSE_BOD33) return "brown-out";
  if (rc & PM_RCAUSE_BOD12) return "brown-out";
  if (rc & PM_RCAUSE_EXT)   return "external reset";
  if (rc & PM_RCAUSE_POR)   return "power-on";
  return "normal";
}

////////////////////////////////////////////////////////////////////////////////
// Flash-backed persistent storage (replaces EEPROM on SAMD21)
////////////////////////////////////////////////////////////////////////////////
void initFlash() {
  EEData d = eeStorage.read();
  if (d.magic != EE_MAGIC_VALUE) {
    d.magic     = EE_MAGIC_VALUE;
    d.bootCount = 0;
    d.reedCount = 0;
    eeStorage.write(d);
    SLOG("[snd] Flash storage initialised");
  }
  bootCount      = d.bootCount;
  reedEventCount = d.reedCount;
}

void saveFlash() {
  EEData d;
  d.magic     = EE_MAGIC_VALUE;
  d.bootCount = bootCount;
  d.reedCount = reedEventCount;
  eeStorage.write(d);
}

////////////////////////////////////////////////////////////////////////////////
// BME280 — forced-mode setup (same as 32u4 version)
////////////////////////////////////////////////////////////////////////////////
void initBme() {
  Wire.begin();
  bool ok = bme.begin(0x76);
  if (!ok) {
    SLOG("[snd] BME280 init FAILED — sensor_ok will be 0");
    cache.ok = false;
    return;
  }
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::FILTER_OFF);
  cache.ok = true;
  SLOG("[snd] BME280 OK");
}

void readSensors() {
  if (!cache.ok) {
    cache.ok = bme.begin(0x76);
    if (!cache.ok) return;
  }
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
// LoRa init
////////////////////////////////////////////////////////////////////////////////
void initLora() {
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    if (LoRa.begin(LORA_FREQ)) {
      LoRa.setSpreadingFactor(LORA_SF);
      LoRa.setSignalBandwidth(LORA_BW);
      LoRa.setTxPower(LORA_TX_POWER);
      LoRa.enableCrc();
      LoRa.sleep();
      SLOG("[snd] LoRa OK");
      return;
    }
    SLOGF(F("LoRa init failed, retry "), attempt);
    delay(200);
  }
  SLOG("[snd] LoRa init FAILED after 3 retries — entering sleep loop anyway");
}

////////////////////////////////////////////////////////////////////////////////
// Battery voltage — same ÷2 divider and 3.3 V reference as 32u4.
// analogReadResolution(10) in setup() keeps the ÷1024 formula unchanged.
////////////////////////////////////////////////////////////////////////////////
uint16_t readVbatMv() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) {
    sum += analogRead(PIN_VBAT);
  }
  uint32_t avg = sum / 4;
  cache.vbatMv = (uint16_t)((avg * 2UL * 3300UL) / 1024UL);
  return cache.vbatMv;
}

////////////////////////////////////////////////////////////////////////////////
// AES-128-CTR — identical algorithm to 32u4 version.
// PROGMEM/pgm_read_byte removed: on ARM, const arrays already reside in Flash.
////////////////////////////////////////////////////////////////////////////////
static const uint8_t AES_SBOX[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static inline uint8_t sb(uint8_t x)     { return AES_SBOX[x]; }
static inline uint8_t xtime(uint8_t a)  { return (a << 1) ^ (a & 0x80 ? 0x1b : 0); }

static void aesExpandKey(const uint8_t* key, uint8_t rk[176]) {
  static const uint8_t RC[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
  memcpy(rk, key, 16);
  for (uint8_t i = 1; i <= 10; i++) {
    uint8_t* p = rk + i * 16;
    uint8_t* q = p - 16;
    p[0] = q[0] ^ sb(q[13]) ^ RC[i-1];
    p[1] = q[1] ^ sb(q[14]);
    p[2] = q[2] ^ sb(q[15]);
    p[3] = q[3] ^ sb(q[12]);
    for (uint8_t j = 4; j < 16; j++) p[j] = q[j] ^ p[j-4];
  }
}

static void aesBlock(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16], t;
  for (uint8_t i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
  for (uint8_t r = 1; r <= 10; r++) {
    for (uint8_t i = 0; i < 16; i++) s[i] = sb(s[i]);
    t=s[1];  s[1]=s[5];  s[5]=s[9];  s[9]=s[13]; s[13]=t;
    t=s[2];  s[2]=s[10]; s[10]=t;    t=s[6]; s[6]=s[14]; s[14]=t;
    t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
    if (r < 10) {
      for (uint8_t c = 0; c < 4; c++) {
        uint8_t* p = s + c * 4;
        uint8_t a=p[0], b=p[1], c2=p[2], d=p[3];
        p[0] = xtime(a) ^ (xtime(b)^b) ^ c2 ^ d;
        p[1] = a ^ xtime(b) ^ (xtime(c2)^c2) ^ d;
        p[2] = a ^ b ^ xtime(c2) ^ (xtime(d)^d);
        p[3] = (xtime(a)^a) ^ b ^ c2 ^ xtime(d);
      }
    }
    for (uint8_t i = 0; i < 16; i++) s[i] ^= rk[r * 16 + i];
  }
  memcpy(out, s, 16);
}

static void aesCtr128(const uint8_t* key, const uint8_t* iv,
                      const uint8_t* in, uint8_t* out, size_t len) {
  static uint8_t rk[176];
  aesExpandKey(key, rk);
  uint8_t ctr[16], ks[16];
  memcpy(ctr, iv, 16);
  for (size_t i = 0; i < len; ) {
    aesBlock(rk, ctr, ks);
    for (int8_t j = 15; j >= 0; j--) { if (++ctr[j]) break; }
    size_t blk = (len - i < 16) ? len - i : 16;
    for (size_t k = 0; k < blk; k++) out[i+k] = in[i+k] ^ ks[k];
    i += blk;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Build packet — identical format to 32u4 sender; receiver is unaware of MCU.
////////////////////////////////////////////////////////////////////////////////
String buildPacket(uint8_t pktType) {
  uint16_t vbat   = cache.vbatMv;
  uint8_t  reedHi = (digitalRead(PIN_REED) == LOW) ? 1 : 0;

  String pkt;
  pkt.reserve(128);
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
  pkt += F("&br=");   pkt += bootReasonStr();
  pkt += F("&fw=");   pkt += FW_VERSION;
  return pkt;
}

////////////////////////////////////////////////////////////////////////////////
// Send packet — AES-128-CTR encrypt then transmit. Identical wire format to 32u4.
////////////////////////////////////////////////////////////////////////////////
void sendPacket(uint8_t pktType) {
  String pkt = buildPacket(pktType);
  SLOGF(F("TX: "), pkt);

  const uint8_t key[16] = LORA_AES_KEY;
  uint8_t iv[16];
  memset(iv, 0, sizeof(iv));
  iv[0] = seq;
  iv[1] = (uint8_t)(bootCount & 0xFF);
  iv[2] = (uint8_t)(bootCount >> 8);

  uint8_t cipherBuf[128];
  aesCtr128(key, iv, (const uint8_t*)pkt.c_str(), cipherBuf, pkt.length());

  LoRa.idle();
  if (pktType == 2 || pktType == 3) {
    LoRa.setTxPower(LORA_HB_TX_POWER, PA_OUTPUT_RFO_PIN);
  } else {
    LoRa.setTxPower(LORA_TX_POWER);
  }
  LoRa.beginPacket();
  LoRa.write(0xAE);
  LoRa.write(iv[0]);
  LoRa.write(iv[1]);
  LoRa.write(iv[2]);
  LoRa.write((uint8_t)0x00);
  LoRa.write(cipherBuf, pkt.length());
  LoRa.endPacket();
  LoRa.sleep();

  blinkLed();
  seq++;
}

////////////////////////////////////////////////////////////////////////////////
// LED blink
////////////////////////////////////////////////////////////////////////////////
void blinkLed() {
#if DEBUG_LED
  digitalWrite(PIN_LED, HIGH);
  delay(100);
  digitalWrite(PIN_LED, LOW);
#endif
}

////////////////////////////////////////////////////////////////////////////////
// sleep32s — one 32 s logical tick using SAMD21 WDT standby (replaces LowPower).
// Watchdog.sleep(8000) puts SAMD21 in STANDBY mode; both the WDT timeout and
// external interrupts (reed switch via EIC) can wake it.
// Returns true if reedFlag set on exit (reed wake), false otherwise.
////////////////////////////////////////////////////////////////////////////////
bool sleep32s() {
  for (uint8_t i = 0; i < 4; i++) {
    Watchdog.sleep(8000);   // SAMD21 WDT standby; actual duration ≈ 8192 ms
    if (reedFlag) return true;
  }
  return false;
}
