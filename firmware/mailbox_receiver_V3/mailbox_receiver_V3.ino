// V1.0.5 — 2026-05-05 — Mailbox receiver V3 (Heltec WiFi LoRa 32 V3, RadioLib via heltec_unofficial)
//
// V1.0.5 changes:
//   • BUG FIX — PRG short-press logged "wake OLED" but the screen stayed dark.
//     Cause: heltec_display_power(false) hard-cuts the OLED via the Vext pin,
//     and on power-on the chip needs full re-init that we weren't doing.
//     Switched to display.displayOff() / display.displayOn() — soft SSD1306
//     commands that toggle the panel without dropping its configuration.
//     Trivial extra OLED quiescent current; we're USB-powered anyway, and
//     the real reason for the timeout was avoiding panel burn-in.
//
// V1.0.4 changes:
//   • REVERT V1.0.3 lid-close work per user feedback ("lid status is useless").
//     Removed mailbox_lid entity from MQTT discovery and stopped publishing
//     to T_LID. The r= field is still parsed (for diagnostic logging) but
//     not exposed to HA. mailbox/state remains the headline entity.
//   • Sender V1.0.4 stops sending type=5 packets in lockstep — see that
//     file's notes.
//
// V1.0.3 changes (kept):
//   • BUG FIX — discovery for mailbox_msg_count and mailbox_boot_count was
//     silently rejected by HA because they declared state_class but no
//     unit_of_measurement (HA requires both or neither). Stripped state_class
//     from those two entities so they appear in HA properly.
//   • Active NTP sync wait (up to 10 s) at the end of setup() so the
//     mailbox_last_seen timestamp publishes correctly on the first received
//     packet. Was async-firing-and-hoping before.
//
// V1.0.2 changes:
//   • BUG FIX — increase MQTT TX payload buffer from default 256 → 1024 bytes
//     via mqttClient.setTxPayloadSize(1024). Each discovery JSON is ~300–400
//     bytes, so the default buffer was silently dropping every discovery
//     message. Symptom: "Mailbox sensor" device never appeared in HA.
//   • All visible version strings (boot SLOG, OLED splash, discovery
//     sw_version) now derive from a single FW_VERSION macro so they can't
//     drift apart.
//
// V1.0.1 changes:
//   • Backward-compat MQTT publishing — restores the V2_real topics that the
//     original HA dashboard YAML expects:
//       - "mailboxstatus/switch" → "ON" on each reed event (mail-arrived)
//       - "mailboxstatus/feather" → JSON blob with temp/humid/lipo/msgcount/rssi/snr
//     Without this, entities like sensor.adafruit_feather_mailboxtemp stop
//     updating after V3 flash. Will be removed in V3.1 once Marko verifies
//     the new mailbox/* entities cover everything.
//
// V1.0.0 was the initial release — see notes below.

// Single source of truth for the firmware version string.
// Used by: header banner above (manual), boot Serial log, OLED splash, and
// the "sw_version" field in every MQTT discovery payload.
#define FW_VERSION "V1.0.5"
//
// Listens for LoRa packets from the mailbox sender (Adafruit Feather 32u4 LoRa) on EU 868 MHz,
// parses the key=value payload, publishes each field to its own MQTT topic, and drives a sticky
// "mail arrived" state that Home Assistant displays as a single notification source.
//
// New in V3 (vs V2_real archived in Old Receiver sketches/):
//   • Real reconnect logic — no more `while(1)` halts on broker hiccups
//   • Hardware watchdog — recovers from any firmware hang within 30 s
//   • MQTT discovery — HA self-creates the device + 15 entities on every boot
//   • Last-Will-and-Testament — HA shows receiver as offline immediately when it dies
//   • Sticky mailbox/state retained, cleared by HA dashboard or PRG long-press
//   • Sender-alive heartbeat detector — flags the Feather if no packet in 50 h
//   • ArduinoOTA — flash from laptop without USB cable (no password — trust the LAN)
//   • OLED auto-off after 10 min idle, woken by PRG short-press
//   • NTP-stamped last_seen, fi.pool.ntp.org, Europe/Helsinki
//   • Drops the dead `<LoRa.h>` include from V2_real, removes the `while (!Serial)` blocker

// Versioning scheme (also applies to other coding projects):
//   V1.0.x = bug fix, comment, typo, cosmetic-only edit
//   V1.x   = feature change, behaviour tweak, display/UX polish
//   V2     = major hardware/architecture change

////////////////////////////////////////////////////////////////////////////////
// Build-time toggles
////////////////////////////////////////////////////////////////////////////////
// Set to 0 in deployed builds to silence Serial output (saves a little USB-block
// power and prevents log spam from a phantom serial monitor).
#define ENABLE_SERIAL 1

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////
#define HELTEC_POWER_BUTTON       // enables the PRG-button helper from the Heltec lib
#include <heltec_unofficial.h>    // wraps RadioLib (SX1262) + OLED + battery + button for V3

#include <WiFi.h>                 // ESP32 WiFi (NOT ESP8266WiFi — V2_real had the wrong header)
#include <WiFiUdp.h>              // needed for ArduinoOTA + NTP
#include <ArduinoMqttClient.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>         // ESP-IDF watchdog API exposed in Arduino-ESP32
#include <time.h>                 // POSIX time API for NTP-synced clock

#include "arduino_secrets.h"      // SECRET_SSID / SECRET_PASS / MQTTUSER / MQTTPASS / MQTTBROKER

////////////////////////////////////////////////////////////////////////////////
// LoRa link parameters — MUST match the sender exactly, or you'll see nothing.
////////////////////////////////////////////////////////////////////////////////
#define FREQUENCY        866.0   // MHz — EU 868 ISM band, matches sender
#define BANDWIDTH        250.0   // kHz — wider = shorter airtime, less sensitive
#define SPREADING_FACTOR 9       // 6..12 — higher = longer range, longer airtime
// TX power not set: this is RX-only.

////////////////////////////////////////////////////////////////////////////////
// MQTT — broker info from arduino_secrets.h, topics defined locally so the
// whole topic tree is grep-friendly in one place.
////////////////////////////////////////////////////////////////////////////////
const char  MQTT_BROKER[]    = MQTTBROKER;
const int   MQTT_PORT        = 1883;
const char  MQTT_CLIENT_ID[] = "MailboxReceiver";

// State + sensor topics (RX → HA)
const char T_STATE[]          = "mailbox/state";              // retained, QoS 1, "MAIL"/"EMPTY"
// T_LID is unused as of V1.0.4 (entity removed from discovery + publish path).
// Kept declared so re-enabling the feature later is a one-line change.
const char T_LID[]            = "mailbox/lid";                // currently unpublished
const char T_TEMP[]           = "mailbox/temp";
const char T_HUMID[]          = "mailbox/humidity";
const char T_PRESSURE[]       = "mailbox/pressure";
const char T_BATT_V[]         = "mailbox/battery_voltage";
const char T_BATT_PCT[]       = "mailbox/battery_percent";
const char T_MSG_COUNT[]      = "mailbox/msg_count";
const char T_RSSI[]           = "mailbox/rssi";
const char T_SNR[]            = "mailbox/snr";
const char T_LAST_SEEN[]      = "mailbox/last_seen";
const char T_SENSOR_OK[]      = "mailbox/sensor_ok";
const char T_BOOT_COUNT[]     = "mailbox/boot_count";
const char T_BOOT_REASON[]    = "mailbox/boot_reason";
const char T_PACKET_TYPE[]    = "mailbox/last_packet_type";
const char T_SENDER_ALIVE[]   = "mailbox/sender_alive";       // retained
const char T_RX_ONLINE[]      = "mailbox/receiver/online";    // LWT-retained
const char T_RX_WIFI[]        = "mailbox/receiver/wifi_rssi";
const char T_RX_UPTIME[]      = "mailbox/receiver/uptime";

// Subscribe topic (HA → RX) — the existing dashboard "Mailbox" button publishes here.
const char T_CLEAR[]          = "mailboxstatus/switch";       // we listen for "OFF"

////////////////////////////////////////////////////////////////////////////////
// Tunables
////////////////////////////////////////////////////////////////////////////////
// Watchdog: any blocking call must complete inside this window or the chip resets.
#define WDT_TIMEOUT_S            30

// Sender-alive timeout: if no packet within this many ms, flag the sender dead.
// 50 h = (24 h heartbeat × 2) + 2 h slack — covers one missed heartbeat.
#define SENDER_ALIVE_TIMEOUT_MS  (50UL * 3600UL * 1000UL)

// Sticky-state defensive guard: after publishing MAIL, ignore further reed events
// for this long. Belt-and-braces on top of the sender's 60 s lockout.
#define STATE_REPEAT_GUARD_MS    60000UL

// OLED auto-off after this many ms of idle — saves the panel from burn-in and
// looks tidier when nobody's looking.
#define OLED_TIMEOUT_MS          (10UL * 60UL * 1000UL)

// Button gestures — short = wake OLED, long = manually clear MAIL state.
#define BTN_LONG_PRESS_MS        1500

// MQTT reconnect backoff bounds.
#define MQTT_RECONNECT_MIN_MS    5000UL
#define MQTT_RECONNECT_MAX_MS    300000UL

// Discovery republish at boot only — HA dedups by unique_id, no need to spam.

////////////////////////////////////////////////////////////////////////////////
// State (everything the loop needs to remember)
////////////////////////////////////////////////////////////////////////////////
WiFiClient    wifiClient;
MqttClient    mqttClient(wifiClient);

// LoRa RX flag — set by the DIO1 ISR (must do nothing else; can't talk to SPI here).
volatile bool rxFlag = false;

// Last-seen state of the sender (used for dup-seq detection + sender_alive timeout).
int8_t        lastSeq          = -1;          // -1 = never seen
unsigned long lastPacketRxMs   = 0;           // millis of last valid RX
bool          senderAliveLast  = true;        // last value published, only republish on change

// Sticky mail state — recovered from broker at boot (subscribe to T_STATE retained).
bool          mailState        = false;       // false = EMPTY, true = MAIL
unsigned long lastMailTransitionMs = 0;       // for STATE_REPEAT_GUARD_MS

// Last decoded packet — kept so the OLED + diagnostic publishes can display them
// even between RX events.
struct {
  uint8_t  packetType  = 0;
  uint8_t  seq         = 0;
  float    tempC       = NAN;
  uint8_t  humidPct    = 0;
  float    pressureHpa = NAN;
  uint16_t vbatMv      = 0;
  uint8_t  reedOpen    = 0;
  uint8_t  sensorOk    = 1;
  uint16_t bootCount   = 0;
  uint32_t uptimeMin   = 0;
  String   bootReason  = "";
  float    rssi        = 0;
  float    snr         = 0;
} lastPkt;

// MQTT reconnect state.
unsigned long mqttNextAttemptMs = 0;
unsigned long mqttBackoffMs     = MQTT_RECONNECT_MIN_MS;

// OLED activity timestamp.
unsigned long lastDisplayActivityMs = 0;
bool          displayOn             = true;

// Diagnostics published periodically.
unsigned long lastDiagPublishMs = 0;
#define DIAG_PUBLISH_INTERVAL_MS  60000UL

// Button tracking (manual; heltec_unofficial's API lacks a configurable long-press).
bool          btnLastPressed     = false;
unsigned long btnPressStartMs    = 0;

////////////////////////////////////////////////////////////////////////////////
// Logging helper — prints to Serial (when enabled) AND OLED status line.
// Use `both.print/println/printf` from the Heltec library — same idea.
////////////////////////////////////////////////////////////////////////////////
#if ENABLE_SERIAL
  #define LOG(tag, ...)   do { Serial.print("["); Serial.print(tag); Serial.print("] "); Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
#else
  #define LOG(tag, ...)   do {} while (0)
#endif

////////////////////////////////////////////////////////////////////////////////
// Forward declarations — must precede onMqttMessage / setup / loop because
// some of those functions call helpers defined further down the file.
////////////////////////////////////////////////////////////////////////////////
void connectWifi();
void connectMqtt();
void publishDiscoveryAll();
void publishOnePacket();
void publishDiagnostics();
void parseAndDispatch(const String& payload, float rssi, float snr);
void renderOled();
void handleButton();
void clearMailState(const char* source);
String batteryPercentString(uint16_t mv);

////////////////////////////////////////////////////////////////////////////////
// LoRa ISR — set flag, do nothing else (SPI not safe in interrupt context).
////////////////////////////////////////////////////////////////////////////////
void onLoraRx() {
  rxFlag = true;
}

////////////////////////////////////////////////////////////////////////////////
// MQTT message handler — only one topic subscribed (T_CLEAR), but written
// extensibly so adding subscriptions later doesn't require a refactor.
////////////////////////////////////////////////////////////////////////////////
void onMqttMessage(int messageSize) {
  String topic   = mqttClient.messageTopic();
  String payload = "";
  while (mqttClient.available()) {
    payload += (char) mqttClient.read();
  }
  LOG("mqtt", "RX %s = %s", topic.c_str(), payload.c_str());

  // The HA dashboard "Mailbox" button publishes "OFF" here when tapped. Anything
  // that's not "ON" treated as a clear command — this matches what the existing
  // dashboard YAML does.
  if (topic == T_CLEAR) {
    if (payload != "ON") {
      clearMailState("ha-button");
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Setup
////////////////////////////////////////////////////////////////////////////////
void setup() {
#if ENABLE_SERIAL
  Serial.begin(115200);
  // No `while (!Serial)` — we boot fine without USB attached.
#endif
  heltec_setup();
  display.init();
  display.setFont(ArialMT_Plain_10);
  display.flipScreenVertically();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.cls();

  // Boot splash — give the user 2 s to see the version + IP placeholder.
  display.drawString(0,  0, "Mailbox RX " FW_VERSION);
  display.drawString(0, 14, "by Marko");
  display.drawString(0, 32, "Booting...");
  display.display();
  LOG("boot", "Mailbox RX " FW_VERSION " starting");

  // Start the watchdog early — once enabled we MUST kick it from loop().
  // ESP32 Arduino core 3.x uses a struct-based init (different from the 2.x
  // (timeout, panic) signature). idle_core_mask = 0 means we don't want to
  // also watchdog the IDLE tasks — overkill for this sketch.
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms     = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic  = true,
  };
  esp_task_wdt_init(&wdtConfig);
  esp_task_wdt_add(NULL);

  // WiFi + MQTT + LoRa, in that order. Each function blocks until success or
  // until WDT bites — the WDT is the safety net against truly broken networks.
  connectWifi();

  // NTP — Finnish pool, Helsinki TZ with DST. configTzTime handles all of this.
  // POSIX TZ string for Europe/Helsinki: EET-2EEST,M3.5.0/3,M10.5.0/4
  configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "fi.pool.ntp.org", "pool.ntp.org");
  LOG("ntp", "Sync requested, waiting up to 10 s...");

  // V1.0.3: actively wait for NTP. configTzTime is async — without this, the
  // first received packet would publish mailbox/last_seen as Unknown because
  // time(nullptr) returns 0 until the first NTP exchange completes (~1–3 s
  // typical, sometimes slower). 10 s ceiling means a misconfigured network
  // doesn't block boot forever — we proceed without NTP and last_seen just
  // stays Unknown until sync eventually succeeds.
  unsigned long ntpStart = millis();
  while (time(nullptr) < 1700000000 && millis() - ntpStart < 10000) {
    delay(500);
    esp_task_wdt_reset();
  }
  if (time(nullptr) > 1700000000) {
    time_t now = time(nullptr);
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    LOG("ntp", "Synced after %lu ms: %s", (unsigned long)(millis() - ntpStart), buf);
  } else {
    LOG("ntp", "FAILED to sync within 10 s — proceeding (will retry passively)");
  }

  // ArduinoOTA — no password (per project decision: trust the LAN).
  ArduinoOTA.setHostname("arduinomailman");
  ArduinoOTA.onStart([]() {
    LOG("ota", "OTA update starting — pausing main loop");
    display.cls();
    display.drawString(0, 0, "OTA UPDATE");
    display.drawString(0, 16, "do not power off");
    display.display();
  });
  ArduinoOTA.onEnd([]() {
    LOG("ota", "OTA complete, rebooting");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    LOG("ota", "OTA error %d", (int) e);
  });
  ArduinoOTA.begin();
  LOG("ota", "OTA ready @ %s", WiFi.localIP().toString().c_str());

  connectMqtt();           // includes LWT setup
  publishDiscoveryAll();   // 15 entities × discovery JSON; HA dedups by unique_id

  // Subscribe to the dashboard clear-button topic.
  mqttClient.subscribe(T_CLEAR);
  mqttClient.onMessage(onMqttMessage);

  // LoRa init — uses the Heltec library's pre-wired SX1262 pin map.
  RADIOLIB_OR_HALT(radio.begin());
  radio.setDio1Action(onLoraRx);
  RADIOLIB_OR_HALT(radio.setFrequency(FREQUENCY));
  RADIOLIB_OR_HALT(radio.setBandwidth(BANDWIDTH));
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(SPREADING_FACTOR));
  RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));
  LOG("lora", "RX armed @ %.2f MHz BW %.1f kHz SF%d", FREQUENCY, BANDWIDTH, SPREADING_FACTOR);

  // Restore sticky state from broker — the retained mailbox/state message will
  // arrive after subscription. Until it does, we display "—" (unknown).
  mqttClient.subscribe(T_STATE);

  lastDisplayActivityMs = millis();
  lastDiagPublishMs     = millis();
  esp_task_wdt_reset();
  delay(2000);                 // hold the splash for 2 s
  renderOled();
}

////////////////////////////////////////////////////////////////////////////////
// Loop
////////////////////////////////////////////////////////////////////////////////
void loop() {
  esp_task_wdt_reset();
  heltec_loop();             // services button + battery measurement state
  ArduinoOTA.handle();

  // WiFi — auto-reconnect if it dropped.
  if (WiFi.status() != WL_CONNECTED) {
    LOG("wifi", "Disconnected — reconnecting");
    connectWifi();
  }

  // MQTT — non-blocking poll, reconnect with exponential backoff if needed.
  if (mqttClient.connected()) {
    mqttClient.poll();
    mqttBackoffMs = MQTT_RECONNECT_MIN_MS;   // reset backoff on success
  } else if (millis() >= mqttNextAttemptMs) {
    LOG("mqtt", "Disconnected — reconnect attempt");
    connectMqtt();
    if (!mqttClient.connected()) {
      mqttNextAttemptMs = millis() + mqttBackoffMs;
      mqttBackoffMs     = min(mqttBackoffMs * 2, MQTT_RECONNECT_MAX_MS);
    }
  }

  // LoRa packet pickup.
  if (rxFlag) {
    rxFlag = false;
    String payload;
    radio.readData(payload);
    if (_radiolib_status == RADIOLIB_ERR_NONE) {
      float rssi = radio.getRSSI();
      float snr  = radio.getSNR();
      LOG("lora", "RX [%s] rssi=%.1f snr=%.1f", payload.c_str(), rssi, snr);
      parseAndDispatch(payload, rssi, snr);
    } else {
      LOG("lora", "RX error %d", _radiolib_status);
    }
    // Re-arm the receiver. SX126X RX_TIMEOUT_INF is "stay in RX forever."
    RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));
  }

  // Sender-alive watchdog — flag the sender dead if quiet too long.
  bool senderAliveNow = (lastPacketRxMs > 0) &&
                        (millis() - lastPacketRxMs < SENDER_ALIVE_TIMEOUT_MS);
  if (senderAliveNow != senderAliveLast) {
    if (mqttClient.connected()) {
      mqttClient.beginMessage(T_SENDER_ALIVE, true, 1);   // retained, QoS 1
      mqttClient.print(senderAliveNow ? "true" : "false");
      mqttClient.endMessage();
      senderAliveLast = senderAliveNow;
      LOG("watch", "sender_alive → %s", senderAliveNow ? "true" : "false");
    }
  }

  // Periodic diagnostics (uptime, WiFi RSSI).
  if (mqttClient.connected() && millis() - lastDiagPublishMs > DIAG_PUBLISH_INTERVAL_MS) {
    publishDiagnostics();
    lastDiagPublishMs = millis();
  }

  // Button + OLED.
  handleButton();
  if (displayOn && (millis() - lastDisplayActivityMs > OLED_TIMEOUT_MS)) {
    display.displayOff();          // V1.0.5: soft off so wake doesn't need re-init
    displayOn = false;
    LOG("oled", "auto-off");
  }

  // OLED refresh — once a second is enough; the display lambda is cheap.
  static unsigned long lastOledRefreshMs = 0;
  if (displayOn && millis() - lastOledRefreshMs > 1000) {
    renderOled();
    lastOledRefreshMs = millis();
  }
}

////////////////////////////////////////////////////////////////////////////////
// WiFi connect — blocks until joined or WDT bites.
////////////////////////////////////////////////////////////////////////////////
void connectWifi() {
  WiFi.setHostname("arduinomailman");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  LOG("wifi", "Connecting to %s", SECRET_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    esp_task_wdt_reset();         // keep the watchdog quiet during the join
  }
  LOG("wifi", "OK ip=%s rssi=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

////////////////////////////////////////////////////////////////////////////////
// MQTT connect — sets LWT before connecting so the broker auto-publishes
// "offline" if the receiver dies.
////////////////////////////////////////////////////////////////////////////////
void connectMqtt() {
  mqttClient.setUsernamePassword(MQTTUSER, MQTTPASS);
  mqttClient.setId(MQTT_CLIENT_ID);
  mqttClient.setKeepAliveInterval(30 * 1000);
  // V1.0.2 fix: ArduinoMqttClient's default TX payload buffer is 256 bytes.
  // Each HA discovery JSON is ~300–400 bytes, so the library was silently
  // dropping every discovery publish, which is why the "Mailbox sensor" device
  // never appeared in HA. 1024 is plenty for any single payload we send.
  mqttClient.setTxPayloadSize(1024);
  // LWT: retained, QoS 1, payload "false". The broker publishes this if we
  // disconnect ungracefully — HA will see receiver_online flip immediately.
  mqttClient.beginWill(T_RX_ONLINE, true, 1);
  mqttClient.print("false");
  mqttClient.endWill();

  if (!mqttClient.connect(MQTT_BROKER, MQTT_PORT)) {
    LOG("mqtt", "Connect failed err=%d (will retry)", mqttClient.connectError());
    return;
  }
  LOG("mqtt", "Connected to %s", MQTT_BROKER);

  // Mark ourselves online (counterpart to the LWT).
  mqttClient.beginMessage(T_RX_ONLINE, true, 1);
  mqttClient.print("true");
  mqttClient.endMessage();
}

////////////////////////////////////////////////////////////////////////////////
// MQTT discovery — one device with N entities.
//
// HA's MQTT discovery convention: publish a JSON config to
//   homeassistant/<platform>/<unique_id>/config
// with "device" identifiers shared across all entities to group them under
// one device card. We discover all entities once at boot — HA dedups by
// unique_id so this is idempotent.
//
// All discovery payloads are RETAINED so HA picks them up after a restart.
////////////////////////////////////////////////////////////////////////////////
// String-literal concatenation: the C preprocessor joins adjacent literals,
// so this picks up FW_VERSION at compile time.
const char DEV_BLOCK[] = ","
  "\"device\":{"
    "\"identifiers\":[\"mailbox_sensor_v3\"],"
    "\"name\":\"Mailbox sensor\","
    "\"manufacturer\":\"Marko\","
    "\"model\":\"Feather 32u4 LoRa + Heltec V3\","
    "\"sw_version\":\"" FW_VERSION "\""
  "}}";

void publishOneDiscovery(const char* platform, const char* uniqueId,
                         const char* name, const char* stateTopic,
                         const char* deviceClass, const char* unit,
                         const char* stateClass, const char* entityCategory,
                         const char* extra = "") {
  String topic = String("homeassistant/") + platform + "/" + uniqueId + "/config";
  String json  = "{";
  json += "\"name\":\"" + String(name) + "\",";
  json += "\"unique_id\":\"" + String(uniqueId) + "\",";
  json += "\"object_id\":\"" + String(uniqueId) + "\",";
  json += "\"state_topic\":\"" + String(stateTopic) + "\"";
  if (deviceClass    && *deviceClass)    json += ",\"device_class\":\"" + String(deviceClass) + "\"";
  if (unit           && *unit)           json += ",\"unit_of_measurement\":\"" + String(unit) + "\"";
  if (stateClass     && *stateClass)     json += ",\"state_class\":\"" + String(stateClass) + "\"";
  if (entityCategory && *entityCategory) json += ",\"entity_category\":\"" + String(entityCategory) + "\"";
  if (extra          && *extra)          json += String(",") + extra;
  json += DEV_BLOCK;

  mqttClient.beginMessage(topic.c_str(), true, 1);     // retained, QoS 1
  mqttClient.print(json);
  mqttClient.endMessage();
}

void publishDiscoveryAll() {
  LOG("disc", "Publishing 15 entity configs");
  // binary_sensor.mailbox_state — the headline entity. Sticky, payload MAIL/EMPTY.
  publishOneDiscovery("binary_sensor", "mailbox_state", "Mailbox state",
                      T_STATE, "occupancy", nullptr, nullptr, nullptr,
                      "\"payload_on\":\"MAIL\",\"payload_off\":\"EMPTY\"");

  // V1.0.4: mailbox_lid entity removed (was useless without lid-close TX from
  // sender, which was rolled back). T_LID topic no longer published to.

  // BME280 sensors.
  publishOneDiscovery("sensor", "mailbox_temp", "Mailbox temperature",
                      T_TEMP, "temperature", "°C", "measurement", nullptr);
  publishOneDiscovery("sensor", "mailbox_humidity", "Mailbox humidity",
                      T_HUMID, "humidity", "%", "measurement", nullptr);
  publishOneDiscovery("sensor", "mailbox_pressure", "Mailbox pressure",
                      T_PRESSURE, "pressure", "hPa", "measurement", nullptr);

  // Battery — both raw voltage and a percent-of-full.
  publishOneDiscovery("sensor", "mailbox_battery_voltage", "Mailbox battery voltage",
                      T_BATT_V, "voltage", "V", "measurement", "diagnostic");
  publishOneDiscovery("sensor", "mailbox_battery_percent", "Mailbox battery",
                      T_BATT_PCT, "battery", "%", "measurement", nullptr);

  // Diagnostics — packet bookkeeping and link quality.
  // V1.0.3: state_class dropped — HA rejects state_class without unit_of_measurement.
  // The packet sequence is just an integer counter, no unit makes sense for it.
  publishOneDiscovery("sensor", "mailbox_msg_count", "Mailbox packet seq",
                      T_MSG_COUNT, nullptr, nullptr, nullptr, "diagnostic");
  publishOneDiscovery("sensor", "mailbox_rssi", "Mailbox RSSI",
                      T_RSSI, "signal_strength", "dBm", "measurement", "diagnostic");
  publishOneDiscovery("sensor", "mailbox_snr", "Mailbox SNR",
                      T_SNR, nullptr, "dB", "measurement", "diagnostic");
  publishOneDiscovery("sensor", "mailbox_last_seen", "Mailbox last seen",
                      T_LAST_SEEN, "timestamp", nullptr, nullptr, "diagnostic");
  publishOneDiscovery("sensor", "mailbox_last_packet_type", "Mailbox last packet type",
                      T_PACKET_TYPE, nullptr, nullptr, nullptr, "diagnostic");
  // V1.0.3: state_class dropped — see msg_count comment above.
  publishOneDiscovery("sensor", "mailbox_boot_count", "Mailbox boot count",
                      T_BOOT_COUNT, nullptr, nullptr, nullptr, "diagnostic");
  publishOneDiscovery("sensor", "mailbox_boot_reason", "Mailbox boot reason",
                      T_BOOT_REASON, nullptr, nullptr, nullptr, "diagnostic");

  // Connectivity binary sensors.
  publishOneDiscovery("binary_sensor", "mailbox_sender_alive", "Mailbox sender alive",
                      T_SENDER_ALIVE, "connectivity", nullptr, nullptr, "diagnostic",
                      "\"payload_on\":\"true\",\"payload_off\":\"false\"");
  publishOneDiscovery("binary_sensor", "mailbox_receiver_online", "Mailbox receiver online",
                      T_RX_ONLINE, "connectivity", nullptr, nullptr, "diagnostic",
                      "\"payload_on\":\"true\",\"payload_off\":\"false\"");

  // Receiver self-diagnostics.
  publishOneDiscovery("sensor", "mailbox_receiver_wifi_rssi", "Mailbox receiver WiFi RSSI",
                      T_RX_WIFI, "signal_strength", "dBm", "measurement", "diagnostic");
  publishOneDiscovery("sensor", "mailbox_receiver_uptime", "Mailbox receiver uptime",
                      T_RX_UPTIME, "duration", "s", "total_increasing", "diagnostic");
}

////////////////////////////////////////////////////////////////////////////////
// Parser — key=value packet from sender.
//
// Expected keys (sender V3): id type seq t h p v r sok boot up br
// Unknown keys are silently ignored — that's the whole point of the format
// (forward-compatible: sender adds new keys without coordinated reflashes).
////////////////////////////////////////////////////////////////////////////////
String getKv(const String& payload, const String& key) {
  int from = 0;
  while (from < (int) payload.length()) {
    int eq  = payload.indexOf('=', from);
    int amp = payload.indexOf('&', eq + 1);
    if (eq < 0) break;
    if (amp < 0) amp = payload.length();
    if (payload.substring(from, eq) == key) {
      return payload.substring(eq + 1, amp);
    }
    from = amp + 1;
  }
  return "";   // not present
}

void parseAndDispatch(const String& payload, float rssi, float snr) {
  String idStr   = getKv(payload, "id");
  String typeStr = getKv(payload, "type");
  if (idStr != "AA" || typeStr.length() == 0) {
    LOG("parse", "rejecting non-mailbox packet (id=%s type=%s)", idStr.c_str(), typeStr.c_str());
    return;
  }

  uint8_t pktType = typeStr.toInt();
  uint8_t seq     = getKv(payload, "seq").toInt();

  // Duplicate-seq rejection — but a boot packet (type=4) legitimately resets seq to 0,
  // so bypass the dup check on type=4.
  if (pktType != 4 && lastSeq >= 0 && seq == (uint8_t) lastSeq) {
    LOG("dedup", "duplicate seq %u, dropping", seq);
    return;
  }

  // Decode all fields. Missing fields keep their previous values — useful when
  // a sensor read failed and the sender flagged sok=0.
  lastPkt.packetType  = pktType;
  lastPkt.seq         = seq;
  lastPkt.rssi        = rssi;
  lastPkt.snr         = snr;

  String s;
  s = getKv(payload, "t");    if (s.length()) lastPkt.tempC       = s.toFloat();
  s = getKv(payload, "h");    if (s.length()) lastPkt.humidPct    = s.toInt();
  s = getKv(payload, "p");    if (s.length()) lastPkt.pressureHpa = s.toFloat();
  s = getKv(payload, "v");    if (s.length()) lastPkt.vbatMv      = s.toInt();
  s = getKv(payload, "r");    if (s.length()) lastPkt.reedOpen    = s.toInt() ? 1 : 0;
  s = getKv(payload, "sok");  if (s.length()) lastPkt.sensorOk    = s.toInt() ? 1 : 0;
  s = getKv(payload, "boot"); if (s.length()) lastPkt.bootCount   = s.toInt();
  s = getKv(payload, "up");   if (s.length()) lastPkt.uptimeMin   = s.toInt();
  s = getKv(payload, "br");   if (s.length()) lastPkt.bootReason  = s;

  lastSeq          = (int8_t) seq;
  lastPacketRxMs   = millis();
  lastDisplayActivityMs = millis();         // wake OLED on packet
  if (!displayOn) { display.displayOn(); displayOn = true; }    // V1.0.5: soft wake

  if (mqttClient.connected()) {
    publishOnePacket();
  }

  // Sticky state transition — only on reed event packets, with the 60 s guard.
  if (pktType == 1 && lastPkt.reedOpen) {
    if (!mailState && (millis() - lastMailTransitionMs > STATE_REPEAT_GUARD_MS)) {
      mailState = true;
      lastMailTransitionMs = millis();
      if (mqttClient.connected()) {
        mqttClient.beginMessage(T_STATE, true, 1);   // retained, QoS 1
        mqttClient.print("MAIL");
        mqttClient.endMessage();
        // V2_real backward-compat (V1.0.1): also fire the old switch topic so
        // the original HA dashboard's switch.adafruit_feather_mailboxstatus
        // updates. Not retained — matches V2_real behaviour exactly.
        mqttClient.beginMessage(T_CLEAR);
        mqttClient.print("ON");
        mqttClient.endMessage();
        LOG("state", "MAIL → published (reed event) + legacy ON");
      }
    } else {
      LOG("state", "reed event ignored (already MAIL or guarded)");
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Publish a complete packet's worth of sensor topics — called from the
// dispatcher after a valid RX.
////////////////////////////////////////////////////////////////////////////////
void publishOne(const char* topic, const String& value, bool retained = false) {
  mqttClient.beginMessage(topic, retained, retained ? 1 : 0);
  mqttClient.print(value);
  mqttClient.endMessage();
}

void publishOnePacket() {
  // Only publish sensor values when sok=1 — sok=0 means the sender's BME280
  // failed and the values are stale. Publishing stale values would mislead HA.
  if (lastPkt.sensorOk) {
    publishOne(T_TEMP,     String(lastPkt.tempC,       2));
    publishOne(T_HUMID,    String(lastPkt.humidPct));
    publishOne(T_PRESSURE, String(lastPkt.pressureHpa, 1));
  }
  publishOne(T_BATT_V,   String(lastPkt.vbatMv / 1000.0, 2),                true);
  publishOne(T_BATT_PCT, batteryPercentString(lastPkt.vbatMv),              true);
  // V1.0.4: T_LID publish removed alongside the entity — see header notes.
  publishOne(T_MSG_COUNT, String(lastPkt.seq),                              true);
  publishOne(T_RSSI,      String(lastPkt.rssi, 1),                          true);
  publishOne(T_SNR,       String(lastPkt.snr,  1),                          true);
  publishOne(T_SENSOR_OK, lastPkt.sensorOk ? "true" : "false");
  publishOne(T_BOOT_COUNT, String(lastPkt.bootCount),                       true);
  if (lastPkt.bootReason.length()) publishOne(T_BOOT_REASON, lastPkt.bootReason, true);
  publishOne(T_PACKET_TYPE, String(lastPkt.packetType),                     true);

  // last_seen — ISO 8601 timestamp from NTP-synced clock. HA renders as "X minutes ago".
  time_t now = time(nullptr);
  if (now > 1700000000) {                       // sanity: NTP synced (>2023-11)
    struct tm* tmInfo = gmtime(&now);
    char isoBuf[32];
    strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", tmInfo);
    publishOne(T_LAST_SEEN, String(isoBuf), true);
  }

  // ---- V2_real backward-compat (V1.0.1) -------------------------------------
  // Publish the JSON blob the original HA configuration.yaml mqtt: block
  // expects under value_template: "{{ value_json.<key> }}". Field names match
  // V2_real exactly: temp / humid / lipo (volts, not mV) / msgcount / rssi / snr.
  // Not retained — matches V2_real behaviour. Drop in V3.1 after migration.
  String legacyJson;
  legacyJson.reserve(128);
  legacyJson  = "{\"temp\":";      legacyJson += String(lastPkt.tempC,            2);
  legacyJson += ",\"humid\":";     legacyJson += lastPkt.humidPct;
  legacyJson += ",\"lipo\":";      legacyJson += String(lastPkt.vbatMv / 1000.0f, 2);
  legacyJson += ",\"msgcount\":";  legacyJson += lastPkt.seq;
  legacyJson += ",\"rssi\":";      legacyJson += String(lastPkt.rssi,             1);
  legacyJson += ",\"snr\":";       legacyJson += String(lastPkt.snr,              1);
  legacyJson += "}";
  mqttClient.beginMessage("mailboxstatus/feather");
  mqttClient.print(legacyJson);
  mqttClient.endMessage();
}

void publishDiagnostics() {
  publishOne(T_RX_WIFI,   String(WiFi.RSSI()));
  publishOne(T_RX_UPTIME, String(millis() / 1000UL));
}

////////////////////////////////////////////////////////////////////////////////
// LiPo voltage → percent. Piecewise curve (4.20=100, 3.85=50, 3.60=20, 3.30=0).
// Refine after one month of real data.
////////////////////////////////////////////////////////////////////////////////
String batteryPercentString(uint16_t mv) {
  if (mv == 0) return "0";          // sender sent zero = unknown
  float v = mv / 1000.0;
  int pct;
  if      (v >= 4.20) pct = 100;
  else if (v >= 3.85) pct = 50  + (int) ((v - 3.85) / (4.20 - 3.85) * 50);
  else if (v >= 3.60) pct = 20  + (int) ((v - 3.60) / (3.85 - 3.60) * 30);
  else if (v >= 3.30) pct =       (int) ((v - 3.30) / (3.60 - 3.30) * 20);
  else                pct = 0;
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return String(pct);
}

////////////////////////////////////////////////////////////////////////////////
// OLED render — called once a second when display is on.
////////////////////////////////////////////////////////////////////////////////
void renderOled() {
  display.cls();
  display.setFont(ArialMT_Plain_10);

  // Top row: WiFi/MQTT status, time
  String topRow = (WiFi.status() == WL_CONNECTED) ? "WiFi " : "wifi? ";
  topRow += (mqttClient.connected())               ? "MQTT " : "mqtt? ";
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char hm[8];
    strftime(hm, sizeof(hm), "%H:%M", &tmInfo);
    topRow += hm;
  }
  display.drawString(0, 0, topRow);

  // Big middle: MAIL / —
  display.setFont(ArialMT_Plain_24);
  display.drawString(36, 18, mailState ? "MAIL" : "—");
  display.setFont(ArialMT_Plain_10);

  // Bottom rows: latest sensor + link
  if (lastSeq >= 0) {
    char line1[32], line2[32];
    if (lastPkt.sensorOk && !isnan(lastPkt.tempC)) {
      snprintf(line1, sizeof(line1), "%.1fC %d%% %.0fhPa",
               lastPkt.tempC, lastPkt.humidPct, lastPkt.pressureHpa);
    } else {
      snprintf(line1, sizeof(line1), "sensor !ok");
    }
    snprintf(line2, sizeof(line2), "%.2fV %s%% rssi%d",
             lastPkt.vbatMv / 1000.0,
             batteryPercentString(lastPkt.vbatMv).c_str(),
             (int) lastPkt.rssi);
    display.drawString(0, 44, line1);
    display.drawString(0, 54, line2);
  } else {
    display.drawString(0, 44, "waiting for packet");
  }

  display.display();
}

////////////////////////////////////////////////////////////////////////////////
// Button handling — short = wake OLED, long ≥1500 ms = clear MAIL state.
//
// We track press start manually because heltec_unofficial doesn't expose a
// configurable long-press threshold. PRG button is GPIO0, active low.
////////////////////////////////////////////////////////////////////////////////
void handleButton() {
  bool pressed = (digitalRead(0) == LOW);
  if (pressed && !btnLastPressed) {
    btnPressStartMs = millis();
  } else if (!pressed && btnLastPressed) {
    unsigned long held = millis() - btnPressStartMs;
    if (held >= BTN_LONG_PRESS_MS) {
      LOG("btn", "long press → clear state");
      clearMailState("button");
      // Brief OLED feedback
      display.displayOn();          // V1.0.5: soft wake
      displayOn = true;
      lastDisplayActivityMs = millis();
      display.cls();
      display.setFont(ArialMT_Plain_16);
      display.drawString(0, 24, "Cleared");
      display.display();
      delay(800);
    } else if (held >= 50) {
      LOG("btn", "short press → wake OLED");
      display.displayOn();          // V1.0.5: soft wake
      displayOn = true;
      lastDisplayActivityMs = millis();
    }
  }
  btnLastPressed = pressed;
}

////////////////////////////////////////////////////////////////////////////////
// Clear the sticky mail state — called from HA dashboard button (T_CLEAR
// payload = "OFF") or from the local PRG long-press.
////////////////////////////////////////////////////////////////////////////////
void clearMailState(const char* source) {
  mailState = false;
  if (mqttClient.connected()) {
    mqttClient.beginMessage(T_STATE, true, 1);   // retained, QoS 1
    mqttClient.print("EMPTY");
    mqttClient.endMessage();
  }
  LOG("state", "EMPTY ← cleared by %s", source);
}
