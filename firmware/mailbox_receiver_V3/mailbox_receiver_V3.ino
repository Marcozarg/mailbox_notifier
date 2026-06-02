// V2.0.0 — 2026-06-02 — Version milestone: project documentation overhaul
//
// V2.0.0 changes:
//   • Version milestone bump to align with V2.0.0 sender. No firmware changes.
//     Documentation restructured: README.md is now the canonical project reference,
//     HARDWARE.md (was docs/SENDER_HARDWARE.md) moved to repo root, planning docs removed.
//
// V1.3.0 — 2026-06-02 — Features: HA reboot command, packet loss counter, freq error sensor
//
// V1.3.0 changes:
//   • NEW — HA reboot command: receiver subscribes to `mailbox/cmd/reboot`; any
//     incoming message triggers ESP.restart(). A `button` entity (device_class restart)
//     is published via MQTT discovery so HA exposes it as a one-tap button on the
//     Mailbox device card. Useful for remote recovery without physical access.
//   • NEW — Packet loss counter: after the dedup check in parseAndDispatch(), gaps
//     in the sender's seq number (uint8 wrapping arithmetic) are counted and
//     accumulated in `packetLossCount`. Published to `mailbox/receiver/packet_loss`
//     (retained, total_increasing) every time a packet is processed. Helps detect
//     LoRa link degradation before it causes complete delivery failures. type=4 boot
//     packets are excluded because the sender legitimately resets seq to 0 on boot.
//   • NEW — Frequency error sensor: after each RX, `radio.getFrequencyError()` is
//     published to `mailbox/receiver/freq_error` (Hz, measurement). Allows HA to
//     plot sender crystal drift vs. temperature over time.
//   • INTERNAL — `publishOneDiscovery()` now treats `stateTopic` as optional: if
//     null or empty the `state_topic` field is omitted from the JSON. This is needed
//     for the new `button` platform (which uses `command_topic` instead). Trailing
//     comma moved from `object_id` line to each consumer field so the JSON stays
//     valid when state_topic is absent. Entity count: 18 → 21.
//
// V1.2.6 — 2026-06-02 — BUG FIX: do not gate state on r= field for type=1 packets
//
// V1.2.6 changes:
//   • BUG FIX — State never published when the sender's r= field was 0 in a
//     type=1 (reed/mail) packet. Root cause: the sender reads digitalRead(PIN_REED)
//     when building the packet. The sender sleeps up to 8 s before waking on the
//     reed ISR, then spends ~10 ms reading the BME280. If the lid was opened briefly
//     (quick user test, fast mailman, or bad timing vs the 8 s sleep cycle), the lid
//     may already be closed again before buildPacket() runs — so r=0 even though the
//     reed event was real. The receiver checked `lastPkt.reedOpen` as a gate:
//       `if (pktType == 1 && lastPkt.reedOpen)`
//     With r=0 that condition was always false, so state was silently never published.
//     Fix: remove the `lastPkt.reedOpen` gate. pktType == 1 IS the mail-arrived
//     signal — the sender's reed ISR already confirmed the event. Whether the reed
//     happens to still be open at packet-build time is irrelevant. The r= field is
//     kept in the packet for diagnostics but must not gate the state transition.
//
// V1.2.5 — 2026-06-02 — BUG FIX: publish deferred reed event on MQTT reconnect
//
// V1.2.5 changes:
//   • BUG FIX — Mail arriving while the receiver's MQTT was in exponential-backoff
//     (i.e. HA had finished rebooting but the receiver hadn't reconnected yet) was
//     silently lost. Root cause: when a type=1 reed packet arrived with MQTT down,
//     parseAndDispatch() correctly skipped publishOnePacket() and logged "MAIL
//     pending — MQTT disconnected", but it did NOT set any flag. When MQTT later
//     reconnected, nothing triggered the deferred publish — the mail event was gone
//     forever. The retained sensor values already in Mosquitto (from the previous
//     delivery) made it look like data "came through fine", masking the miss.
//     Fix: add a `pendingMailState` bool. When a type=1 reed event is received
//     while MQTT is down, set the flag. In connectMqtt(), publish MAIL *before*
//     subscribing to T_STATE — this updates the broker's retained value first, so
//     the subscribe-triggered retained-message delivery in onMqttMessage picks up
//     MAIL and mailState converges correctly on the first poll().
//
// V1.2.4 — 2026-05-25 — BUG FIX: re-subscribe to T_STATE on every MQTT reconnect
//
// V1.2.4 changes:
//   • BUG FIX — After any Mosquitto restart (e.g. HA upgrade), the receiver
//     lost its subscription to `mailbox/state` and never got it back. Root
//     cause: MQTT cleanSession=true means the broker discards all
//     subscriptions for this client ID on disconnect. connectMqtt() was
//     called on every reconnect but only published T_R_ONLINE="true" —
//     it never re-subscribed. The onMqttMessage handler was therefore
//     never called after a reconnect, so the local `mailState` flag
//     drifted out of sync with the broker:
//       1) HA dashboard "clear" (publishes retained EMPTY to mailbox/state)
//          was silently ignored — receiver still believed state was MAIL.
//       2) Next real reed event hit `!mailState == false`, was logged as
//          "reed event ignored (already MAIL or guarded)", and never
//          published to HA. Symptom: OLED didn't show MAIL, state stayed
//          stuck.
//     Fix: move `mqttClient.subscribe(T_STATE)` into connectMqtt() so it
//     fires on every connect, not just at boot. The broker immediately
//     replays the retained mailbox/state value, which onMqttMessage() picks
//     up on the first poll() call and syncs mailState correctly.
//   • Cleanup: removed the two now-redundant subscribe(T_STATE) calls from
//     setup() — connectMqtt() already handles it. onMessage registration
//     stays in setup() (it persists across reconnects, only needed once).
//
// V1.2.3 — 2026-05-22 — Mailbox receiver V3 (Heltec WiFi LoRa 32 V3, RadioLib via heltec_unofficial)
//
// V1.2.3 changes:
//   • ArduinoOTA hostname brought in line with the WiFi hostname. Both now
//     use the same host part `mailbox`, sourced from a single `WIFI_HOSTNAME`
//     macro near the top of the file.
//        WiFi DHCP hostname: `mailbox.<SECRET_DOMAINNAME>` (e.g. `mailbox.homenet.io`)
//        ArduinoOTA / mDNS:  `mailbox` (advertised as `mailbox.local`)
//     Arduino IDE network port list now reads `mailbox at <IP>` instead of
//     `arduinomailman at <IP>`. After OTA-flashing V1.2.3 the IDE will
//     repopulate the port list — pick the new `mailbox` entry from then on.
//
// V1.2.2 changes:
//   • WiFi DHCP hostname changed from `arduinomailman` to
//     `mailbox.<SECRET_DOMAINNAME>` (composed at compile time via C
//     string-literal concatenation). With `SECRET_DOMAINNAME "homenet.io"`
//     the receiver now shows up in the router's DHCP lease table as
//     `mailbox.homenet.io`. The new secret is documented in
//     `arduino_secrets.h.example`.
//
// V1.2.1 changes:
//   • `mailbox_sender_last_packet_type` now publishes a human-readable label
//     ("mail" / "heartbeat" / "heartbeat (low batt)" / "boot") instead of the
//     raw integer 1/2/3/4. Implemented in publishOnePacket via a small
//     packetTypeLabel() helper. No wire-format change — the on-air `type=`
//     field is still numeric; this is purely a display improvement.
//   • Companion sender change: V1.1.0 now includes `&br=` (boot reason) in
//     every packet, not just boot packets. Receiver code unchanged for that —
//     the existing `getKv(payload, "br")` + retained publish path picks the
//     value up the moment any V1.1.0 packet arrives. Net effect: HA stops
//     showing "Unknown" for boot reason after the first V1.1.0 sender packet.
//
// V1.2.0 changes (BREAKING — entity_id slugs cleaned up):
//   • Fixed the "Mailbox sender battery" entity_id ending up as
//     `sensor.mailbox_mailbox_sender_battery` (doubled "mailbox_") and the
//     same doubling on all 17 sister entities. Root cause: modern HA composes
//     entity_id as `<device_slug>_<object_id_slug>` and the friendly name as
//     `<device_name> <entity_name>`. Our V1.1.0 discovery passed
//     name="Mailbox sender battery" + object_id="mailbox_sender_battery" +
//     device="Mailbox", so HA stripped "Mailbox" for display but still
//     prepended the device slug for the entity_id, producing the doubled form.
//   • Fix: drop the "Mailbox" prefix from every entity's `name` field AND
//     the "mailbox_" prefix from every entity's `unique_id` / `object_id`.
//     HA now composes:
//        device "Mailbox" + name "Sender battery"        → display "Mailbox > Sender battery"
//        device-slug "mailbox" + object_id "sender_battery" → entity_id `sensor.mailbox_sender_battery`
//     Clean on both ends, no doubling.
//   • Because unique_id changed for all 18 entities, the V1.1.0/V1.1.1
//     entities will go "Unavailable" in HA after this flash. One more bulk
//     delete pass under Settings → Devices & services → MQTT → entities
//     (filter "unavailable", search "mailbox_mailbox") cleans them up.
//   • V1.x bump (UX polish, per project versioning rules — see V1.0.0 notes).
//
// V1.1.1 changes:
//   • BUG FIX — ArduinoOTA upload was dying at ~50 % with WinError 10054
//     ("connection forcibly closed by remote host") on first attempt. Root
//     cause: ArduinoOTA.handle() blocks the main loop() while streaming the
//     binary and erasing 4 KB flash sectors, which means the 30 s task
//     watchdog (kicked from loop()) trips mid-upload and hardware-resets
//     the ESP32. Fix: register `onStart` / `onProgress` / `onEnd` / `onError`
//     callbacks that call `esp_task_wdt_reset()`, so the WDT stays armed at
//     30 s for real hangs but gets steadily kicked during the upload.
//   • Adjacent fix — `clearDio1Action()` on OTA start so RadioLib's DIO1
//     ISR can't fire while flash is mid-erase (SPI race risk). On
//     `onError` we re-arm the LoRa receive so a failed upload doesn't leave
//     the receiver LoRa-deaf until reboot. Successful upload reboots the
//     chip anyway, so no restore needed on the happy path.
//   • UX — OLED now shows live `OTA xx%` during the upload (replaces the
//     V1.1.0 static "OTA UPDATE / do not power off" splash), so progress is
//     visible on the device side too.
//   • Doc — README clarified: leave the Arduino-IDE password field blank on
//     network upload. The receiver has no OTA password (per locked
//     decision); whatever you type is ignored, but typing a space caused
//     confusion the first time around.
//
// V1.1.0 changes (BREAKING — entity_ids + MQTT topics restructured):
//   • Device card name: "Mailbox sensor" → "Mailbox" (identifier "mailbox_sensor_v3"
//     kept stable so HA keeps the device card grouping under one card).
//   • All entity_ids reorganised into a strict sender_/receiver_ scheme so the
//     origin of each value is obvious at a glance in HA:
//       Sender data (BME280, LiPo, packet bookkeeping)
//         mailbox_temp                  → mailbox_sender_temperature
//         mailbox_humidity              → mailbox_sender_humidity
//         mailbox_pressure              → mailbox_sender_pressure
//         mailbox_battery_voltage       → mailbox_sender_battery_voltage
//         mailbox_battery_percent       → mailbox_sender_battery
//         mailbox_msg_count             → mailbox_sender_packet_seq
//         mailbox_last_packet_type      → mailbox_sender_last_packet_type
//         mailbox_boot_count            → mailbox_sender_boot_count
//         mailbox_boot_reason           → mailbox_sender_boot_reason
//         mailbox_sender_alive          unchanged
//         mailbox_sender_version        unchanged
//       Receiver-measured (link quality, receiver self-diagnostics)
//         mailbox_rssi                  → mailbox_receiver_rssi
//         mailbox_snr                   → mailbox_receiver_snr
//         mailbox_last_seen             → mailbox_receiver_last_seen
//         mailbox_receiver_online       unchanged
//         mailbox_receiver_wifi_rssi    unchanged
//         mailbox_receiver_uptime       unchanged
//       Headline (device-level)
//         mailbox_state                 unchanged
//     Friendly names follow the entity_ids ("Mailbox sender temperature" etc.).
//   • Underlying MQTT topics also restructured for symmetry — all sender-derived
//     values now under `mailbox/sender/...`, all receiver-measured under
//     `mailbox/receiver/...`, with `mailbox/state` as the headline. The old
//     `mailbox/temp`, `mailbox/rssi`, `mailbox/sender_fw`, etc. topics are GONE
//     (no aliasing kept — clean break per the Q3 decision).
//   • Legacy V2_real compatibility publishes REMOVED (per Q3-b decision):
//       - No more `mailboxstatus/switch=ON` on reed events
//       - No more `mailboxstatus/feather=<JSON>` blob
//       - No more subscription to `mailboxstatus/switch` for the clear command.
//     **HA dashboard impact:** the "Mailbox" tap-action must now publish
//     `EMPTY` (retained) directly to `mailbox/state` to clear the sticky
//     state. The receiver's existing T_STATE subscription handles this via the
//     V1.0.6 Fix B path (broker-driven sync), so behaviour is identical from
//     the user's POV.
//   • V1.x major bump (per project versioning convention) because this is a
//     behaviour/UX change, not a bug fix. Old entities will become orphaned in
//     HA — delete manually from Settings → Devices & services → MQTT (Q2-b).
//
// V1.0.8 changes:
//   • `mailbox_receiver_uptime` switched from seconds to days (2 decimals).
//     Discovery unit "s" → "d"; publishDiagnostics now publishes
//     `millis() / 86400000.0` formatted with two decimal places. HA card now
//     reads e.g. "1.34 d" instead of "115320 s". `mailbox_last_seen` left
//     untouched per user request.
//   • Receiver firmware version moved off the boot splash and onto the main
//     OLED screen, right-justified on the top status row. Always visible at
//     a glance now, instead of only flashing past for 2 s at boot.
//   • New entity `mailbox_sender_version` — shows the sender's running
//     firmware string under the Mailbox sensor device card in HA. Receiver
//     parses the new `&fw=` field added by sender V1.0.9 (gracefully ignores
//     packets without it, so a stale sender doesn't break parsing). Bumps
//     discovery entity count from 15 → 16.
//
// V1.0.7 changes:
//   • Sender_alive timeout doubled: 50 h → 98 h, in lockstep with the sender
//     V1.0.8 heartbeat cadence change (24 h → 48 h). Scaling factor preserved
//     (heartbeat × 2 + 2 h slack), so the "sender dead?" alert still tolerates
//     exactly one missed heartbeat before flagging the link as broken.
//
// V1.0.6 changes:
//   • BUG FIX A — `mailState` was set to true before the MQTT publish, even if
//     the broker was disconnected. Symptom: after HA reboot, reed events that
//     fired while MQTT was reconnecting silently flipped the local flag
//     without reaching the broker. The receiver then thought state was
//     already MAIL on subsequent events and never re-published, leaving HA
//     stuck on EMPTY/Unknown until manually cleared. Fix: only mutate
//     mailState (and lastMailTransitionMs) after the publish has actually
//     been sent. Same pattern applied in clearMailState. Added a log line
//     for the disconnected-publish path so future regressions are visible.
//
//   • BUG FIX B — onMqttMessage subscribed to T_STATE but ignored incoming
//     messages on that topic, so the comment in setup() about "Restore sticky
//     state from broker" never actually worked. Now T_STATE = "MAIL" / "EMPTY"
//     updates mailState locally. This recovers the receiver's view from the
//     broker on reboot AND keeps it in sync if any other client (e.g. HA
//     dashboard) publishes to mailbox/state directly.
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
#define FW_VERSION "V2.0.0"

// Single source of truth for the device's host part. Combined with
// SECRET_DOMAINNAME to form the WiFi DHCP FQDN ("mailbox.homenet.io") and
// used standalone as the ArduinoOTA mDNS name ("mailbox.local"). Change
// here once and both stay in sync.
#define WIFI_HOSTNAME "mailbox"
//
// Listens for LoRa packets from the mailbox sender (Adafruit Feather 32u4 LoRa) on EU 868 MHz,
// parses the key=value payload, publishes each field to its own MQTT topic, and drives a sticky
// "mail arrived" state that Home Assistant displays as a single notification source.
//
// New in V3 (vs V2_real archived in Old Receiver sketches/):
//   • Real reconnect logic — no more `while(1)` halts on broker hiccups
//   • Hardware watchdog — recovers from any firmware hang within 30 s
//   • MQTT discovery — HA self-creates the "Mailbox" device + 18 entities on every boot
//   • Last-Will-and-Testament — HA shows receiver as offline immediately when it dies
//   • Sticky mailbox/state retained, cleared by HA dashboard or PRG long-press
//   • Sender-alive heartbeat detector — flags the Feather if no packet in 98 h
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

// Topic tree — V1.1.0 restructure. Every topic is namespaced under either
// `mailbox/sender/...` (data the FEATHER produces) or `mailbox/receiver/...`
// (data this RECEIVER measures), with `mailbox/state` as the device-level
// headline. Pre-V1.1.0 topics (`mailbox/temp`, `mailbox/rssi`,
// `mailbox/sender_fw`, etc.) and the legacy `mailboxstatus/*` compat tree
// are gone — no aliases kept (Q3-b clean break).

// Headline (bidirectional — receiver publishes MAIL/EMPTY, HA may publish
// EMPTY to clear).
const char T_STATE[]          = "mailbox/state";                    // retained, QoS 1, "MAIL"/"EMPTY"

// Sender-derived (RX → HA), all retained on publish.
const char T_S_TEMPERATURE[]      = "mailbox/sender/temperature";
const char T_S_HUMIDITY[]         = "mailbox/sender/humidity";
const char T_S_PRESSURE[]         = "mailbox/sender/pressure";
const char T_S_BATTERY_VOLTAGE[]  = "mailbox/sender/battery_voltage";
const char T_S_BATTERY_PERCENT[]  = "mailbox/sender/battery_percent";
const char T_S_PACKET_SEQ[]       = "mailbox/sender/packet_seq";
const char T_S_LAST_PACKET_TYPE[] = "mailbox/sender/last_packet_type";
const char T_S_BOOT_COUNT[]       = "mailbox/sender/boot_count";
const char T_S_BOOT_REASON[]      = "mailbox/sender/boot_reason";
const char T_S_SENSOR_OK[]        = "mailbox/sender/sensor_ok";     // side channel, no HA entity
const char T_S_ALIVE[]            = "mailbox/sender/alive";         // retained, "true"/"false"
const char T_S_VERSION[]          = "mailbox/sender/version";       // sender FW string

// Receiver-measured (RX → HA).
const char T_R_RSSI[]         = "mailbox/receiver/rssi";
const char T_R_SNR[]          = "mailbox/receiver/snr";
const char T_R_LAST_SEEN[]    = "mailbox/receiver/last_seen";
const char T_R_ONLINE[]       = "mailbox/receiver/online";          // LWT-retained
const char T_R_WIFI_RSSI[]    = "mailbox/receiver/wifi_rssi";
const char T_R_UPTIME[]       = "mailbox/receiver/uptime";
const char T_R_PACKET_LOSS[]  = "mailbox/receiver/packet_loss";     // V1.3.0: cumulative missed packets
const char T_R_FREQ_ERROR[]   = "mailbox/receiver/freq_error";      // V1.3.0: sender crystal drift, Hz

// HA → receiver commands (V1.3.0).
const char T_CMD_REBOOT[]     = "mailbox/cmd/reboot";               // any payload → ESP.restart()

// No HA→RX clear-topic anymore. HA dashboard publishes "EMPTY" (retained)
// directly to `mailbox/state`; the existing T_STATE subscription + V1.0.6
// Fix B handler picks it up and updates `mailState` locally.

////////////////////////////////////////////////////////////////////////////////
// Tunables
////////////////////////////////////////////////////////////////////////////////
// Watchdog: any blocking call must complete inside this window or the chip resets.
#define WDT_TIMEOUT_S            30

// Sender-alive timeout: if no packet within this many ms, flag the sender dead.
// 98 h = (48 h heartbeat × 2) + 2 h slack — covers one missed heartbeat.
// V1.0.7: was 50UL (matched the old 24 h sender heartbeat).
#define SENDER_ALIVE_TIMEOUT_MS  (98UL * 3600UL * 1000UL)

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
// V1.2.5: set when a type=1 reed event arrives while MQTT is disconnected.
// Cleared (and MAIL published) inside connectMqtt() on the next successful connect.
bool          pendingMailState = false;

// V1.3.0: cumulative count of packets inferred lost (gaps in sender seq number).
// Resets to 0 on receiver reboot only — not on sender boot (type=4 is excluded
// from the gap check since the sender legitimately resets seq to 0 on every boot).
uint32_t      packetLossCount  = 0;

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
  String   fw          = "";      // V1.0.8 — sender firmware string from &fw= field
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
// MQTT message handler — only T_STATE is subscribed (V1.1.0 dropped the
// legacy T_CLEAR/mailboxstatus subscription). HA clears the sticky state by
// publishing "EMPTY" (retained) directly to mailbox/state, which lands here.
////////////////////////////////////////////////////////////////////////////////
void onMqttMessage(int messageSize) {
  String topic   = mqttClient.messageTopic();
  String payload = "";
  while (mqttClient.available()) {
    payload += (char) mqttClient.read();
  }
  LOG("mqtt", "RX %s = %s", topic.c_str(), payload.c_str());

  // V1.3.0: remote reboot command — any payload triggers restart.
  if (topic == T_CMD_REBOOT) {
    LOG("cmd", "Reboot command received — restarting in 100 ms");
    delay(100);   // let the serial log flush before reset
    ESP.restart();
    return;
  }

  if (topic == T_STATE) {
    // V1.0.6 fix B: keep mailState in sync with the broker's retained value.
    //
    // Three cases this handles:
    //   1) Receiver boot — the subscribe to T_STATE near the end of setup()
    //      triggers the broker to replay its retained MAIL/EMPTY here, so
    //      our local mailState reflects reality from the first millisecond
    //      instead of starting at false and potentially mis-firing.
    //   2) State drift while disconnected — if HA cleared the state via the
    //      dashboard while our MQTT was down, the retained EMPTY arrives on
    //      reconnect and we adopt it (otherwise we'd think it was still MAIL
    //      and refuse the next legitimate reed-event publish).
    //   3) HA dashboard clear — V1.1.0+: tapping the "Mailbox" tile publishes
    //      "EMPTY" (retained) directly to mailbox/state. We simply adopt it.
    //
    // Self-publish echoes are no-ops (payload matches mailState already).
    bool newState = (payload == "MAIL");
    if (newState != mailState) {
      mailState = newState;
      LOG("state", "sync from broker → %s", newState ? "MAIL" : "EMPTY");
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

  // Boot splash — give the user 2 s to see who/what is booting.
  // V1.0.8: FW_VERSION removed from this screen and moved to the top-right
  // of the main renderOled() status row so it's visible all the time, not
  // just at boot. The Serial log below still prints it for flash debugging.
  display.drawString(0,  0, "Mailbox RX");
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
  //
  // V1.1.1: WDT-kick from the OTA callbacks. ArduinoOTA.handle() blocks the
  // main loop() during the entire upload, so the normal `esp_task_wdt_reset()`
  // call at the top of loop() never runs — without this the 30 s WDT trips
  // mid-upload and the chip resets (manifests as WinError 10054 in Arduino
  // IDE at ~50 %). Kicking the WDT from onProgress (fires every few KB) keeps
  // it happy for the full upload while still catching real firmware hangs.
  ArduinoOTA.setHostname(WIFI_HOSTNAME);   // V1.2.3 — was "arduinomailman"
  ArduinoOTA.onStart([]() {
    LOG("ota", "OTA update starting — pausing main loop");
    // V1.1.1: silence the LoRa interrupt during OTA. RadioLib's DIO1 ISR
    // pokes SPI when a packet arrives, and a flash-erase landing on the same
    // SPI bus is a recipe for corruption. On a successful OTA the chip
    // reboots and re-initialises radio cleanly; the onError path re-arms RX
    // so a failed upload doesn't leave us LoRa-deaf until manual reboot.
    radio.clearDio1Action();
    esp_task_wdt_reset();
    if (!displayOn) { display.displayOn(); displayOn = true; }
    display.cls();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0,  "OTA UPDATE");
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 22, "do not power off");
    display.display();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    esp_task_wdt_reset();   // V1.1.1 — the actual reason this whole block exists
    // Update the OLED progress bar every ~5 % to avoid spamming the SSD1306
    // I2C bus and slowing the upload itself.
    static unsigned int lastShownPct = 101;
    unsigned int pct = total ? (progress * 100U) / total : 0;
    if (pct != lastShownPct && pct % 5 == 0) {
      lastShownPct = pct;
      display.cls();
      display.setFont(ArialMT_Plain_16);
      display.drawString(0, 0,  "OTA UPDATE");
      display.setFont(ArialMT_Plain_10);
      char line[24];
      snprintf(line, sizeof(line), "%u%%   %u / %u", pct, progress, total);
      display.drawString(0, 22, line);
      // Simple progress bar across the bottom row.
      int barW = (int) ((128 * (uint32_t) progress) / (total ? total : 1));
      display.drawRect(0, 50, 128, 10);
      display.fillRect(0, 50, barW, 10);
      display.display();
    }
  });
  ArduinoOTA.onEnd([]() {
    esp_task_wdt_reset();
    LOG("ota", "OTA complete, rebooting");
    display.cls();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 16, "OTA OK");
    display.drawString(0, 36, "rebooting");
    display.display();
  });
  ArduinoOTA.onError([](ota_error_t e) {
    esp_task_wdt_reset();
    LOG("ota", "OTA error %d — restoring LoRa RX", (int) e);
    // Re-arm the LoRa interrupt and receiver so a failed upload doesn't
    // leave us deaf. The interrupt was cleared in onStart; restore it.
    radio.setDio1Action(onLoraRx);
    radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF);
    display.cls();
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0,  "OTA FAILED");
    display.setFont(ArialMT_Plain_10);
    char line[24];
    snprintf(line, sizeof(line), "err %d", (int) e);
    display.drawString(0, 22, line);
    display.drawString(0, 36, "LoRa restored");
    display.display();
  });
  ArduinoOTA.begin();
  LOG("ota", "OTA ready @ %s", WiFi.localIP().toString().c_str());

  connectMqtt();           // includes LWT setup + T_STATE subscribe (V1.2.4)
  publishDiscoveryAll();   // 18 entities × discovery JSON; HA dedups by unique_id

  // Register the MQTT message handler once — it is stored in the client object
  // and persists across reconnects, so this only needs to happen at boot.
  // T_STATE subscription lives in connectMqtt() (V1.2.4) so it is re-registered
  // on every reconnect. The broker replays the retained mailbox/state value on
  // the first poll() in loop(), syncing mailState before any packet arrives.
  mqttClient.onMessage(onMqttMessage);

  // LoRa init — uses the Heltec library's pre-wired SX1262 pin map.
  RADIOLIB_OR_HALT(radio.begin());
  radio.setDio1Action(onLoraRx);
  RADIOLIB_OR_HALT(radio.setFrequency(FREQUENCY));
  RADIOLIB_OR_HALT(radio.setBandwidth(BANDWIDTH));
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(SPREADING_FACTOR));
  RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));
  LOG("lora", "RX armed @ %.2f MHz BW %.1f kHz SF%d", FREQUENCY, BANDWIDTH, SPREADING_FACTOR);

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
      mqttClient.beginMessage(T_S_ALIVE, true, 1);   // retained, QoS 1
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
  // V1.2.2: hostname composed at compile time from WIFI_HOSTNAME + the user's
  // LAN domain in secrets. Result e.g. "mailbox.homenet.io". Whether the
  // router actually registers this into local DNS depends on the DHCP server
  // config, but at minimum the FQDN shows up in lease tables. V1.2.3 keeps
  // the OTA mDNS name (`WIFI_HOSTNAME` alone, i.e. "mailbox.local") aligned
  // so the IDE port list reads `mailbox at <IP>`.
  WiFi.setHostname(WIFI_HOSTNAME "." SECRET_DOMAINNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  LOG("wifi", "Connecting to %s (hostname %s.%s)", SECRET_SSID, WIFI_HOSTNAME, SECRET_DOMAINNAME);
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
  mqttClient.beginWill(T_R_ONLINE, true, 1);
  mqttClient.print("false");
  mqttClient.endWill();

  if (!mqttClient.connect(MQTT_BROKER, MQTT_PORT)) {
    LOG("mqtt", "Connect failed err=%d (will retry)", mqttClient.connectError());
    return;
  }
  LOG("mqtt", "Connected to %s", MQTT_BROKER);

  // Mark ourselves online (counterpart to the LWT).
  mqttClient.beginMessage(T_R_ONLINE, true, 1);
  mqttClient.print("true");
  mqttClient.endMessage();

  // V1.2.5 BUG FIX: if a reed event was received while MQTT was down, publish
  // MAIL *before* subscribing to T_STATE. Publishing first updates the broker's
  // retained value to MAIL; the subscribe below then causes the broker to
  // replay MAIL back to us via onMqttMessage(), so mailState converges to true
  // on the first poll(). If we subscribed first and published second, the broker
  // would replay the old retained EMPTY before our MAIL arrived, then replay
  // MAIL — harmless but noisy; publishing first avoids that double-flip.
  if (pendingMailState) {
    mqttClient.beginMessage(T_STATE, true, 1);   // retained, QoS 1
    mqttClient.print("MAIL");
    mqttClient.endMessage();
    mailState            = true;
    lastMailTransitionMs = millis();
    pendingMailState     = false;
    LOG("state", "MAIL → published (deferred from MQTT-disconnected period)");
  }

  // V1.2.4 BUG FIX: re-subscribe to T_STATE on every (re)connect.
  // MQTT cleanSession=true causes the broker to discard all subscriptions for
  // this client ID on disconnect. Without re-subscribing here the receiver
  // goes deaf to mailbox/state after any Mosquitto restart: it stops receiving
  // the HA dashboard EMPTY clear, mailState drifts out of sync, and the next
  // legitimate reed event is silently dropped ("reed event ignored — already
  // MAIL or guarded"). The broker replays the retained mailbox/state value
  // immediately on subscribe; onMqttMessage() picks it up on the next poll()
  // and syncs mailState — so the receiver always knows the true state.
  mqttClient.subscribe(T_STATE);
  LOG("mqtt", "Subscribed to %s", T_STATE);

  // V1.3.0: subscribe to the remote-reboot command topic.
  mqttClient.subscribe(T_CMD_REBOOT);
  LOG("mqtt", "Subscribed to %s", T_CMD_REBOOT);
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
    "\"identifiers\":[\"mailbox_sensor_v3\"],"       // kept stable — see V1.1.0 notes
    "\"name\":\"Mailbox\","                          // V1.1.0: was "Mailbox sensor"
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
  json += "\"object_id\":\"" + String(uniqueId) + "\"";
  // V1.3.0: state_topic is optional — button entities use command_topic instead.
  if (stateTopic && *stateTopic) json += ",\"state_topic\":\"" + String(stateTopic) + "\"";
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
  // V1.2.0: 18 entities. Names + unique_ids no longer carry the "mailbox"
  // prefix — HA prepends the device name "Mailbox" automatically. This avoids
  // the doubled `sensor.mailbox_mailbox_*` entity_id that modern HA produced
  // for the V1.1.0/V1.1.1 discovery payloads.
  LOG("disc", "Publishing 21 entity configs (V1.3.0: +reboot button, +packet_loss, +freq_error)");

  // ---- Headline ------------------------------------------------------------
  // binary_sensor.mailbox_state — sticky, payload MAIL/EMPTY.
  // Composed entity_id: device-slug "mailbox" + object_id "state".
  publishOneDiscovery("binary_sensor", "state", "State",
                      T_STATE, "occupancy", nullptr, nullptr, nullptr,
                      "\"payload_on\":\"MAIL\",\"payload_off\":\"EMPTY\"");

  // ---- Sender-derived (BME280, LiPo, packet bookkeeping) ------------------
  publishOneDiscovery("sensor", "sender_temperature", "Sender temperature",
                      T_S_TEMPERATURE, "temperature", "°C", "measurement", nullptr);
  publishOneDiscovery("sensor", "sender_humidity", "Sender humidity",
                      T_S_HUMIDITY, "humidity", "%", "measurement", nullptr);
  publishOneDiscovery("sensor", "sender_pressure", "Sender pressure",
                      T_S_PRESSURE, "pressure", "hPa", "measurement", nullptr);

  publishOneDiscovery("sensor", "sender_battery_voltage", "Sender battery voltage",
                      T_S_BATTERY_VOLTAGE, "voltage", "V", "measurement", "diagnostic");
  publishOneDiscovery("sensor", "sender_battery", "Sender battery",
                      T_S_BATTERY_PERCENT, "battery", "%", "measurement", nullptr);

  // Packet bookkeeping. state_class intentionally absent — HA rejects state_class
  // without unit_of_measurement, and a sequence counter has no natural unit.
  publishOneDiscovery("sensor", "sender_packet_seq", "Sender packet seq",
                      T_S_PACKET_SEQ, nullptr, nullptr, nullptr, "diagnostic");
  publishOneDiscovery("sensor", "sender_last_packet_type", "Sender last packet type",
                      T_S_LAST_PACKET_TYPE, nullptr, nullptr, nullptr, "diagnostic");
  publishOneDiscovery("sensor", "sender_boot_count", "Sender boot count",
                      T_S_BOOT_COUNT, nullptr, nullptr, nullptr, "diagnostic");
  publishOneDiscovery("sensor", "sender_boot_reason", "Sender boot reason",
                      T_S_BOOT_REASON, nullptr, nullptr, nullptr, "diagnostic");

  publishOneDiscovery("binary_sensor", "sender_alive", "Sender alive",
                      T_S_ALIVE, "connectivity", nullptr, nullptr, "diagnostic",
                      "\"payload_on\":\"true\",\"payload_off\":\"false\"");

  // Sender firmware version — populated by sender V1.0.9+ via the &fw= packet
  // field. Plain text sensor, no unit/class. Icon picked to look "version-y".
  publishOneDiscovery("sensor", "sender_version", "Sender version",
                      T_S_VERSION, nullptr, nullptr, nullptr, "diagnostic",
                      "\"icon\":\"mdi:tag-text\"");

  // ---- Receiver-measured (link quality + receiver self-diagnostics) -------
  publishOneDiscovery("sensor", "receiver_rssi", "Receiver RSSI",
                      T_R_RSSI, "signal_strength", "dBm", "measurement", "diagnostic");
  publishOneDiscovery("sensor", "receiver_snr", "Receiver SNR",
                      T_R_SNR, nullptr, "dB", "measurement", "diagnostic");
  publishOneDiscovery("sensor", "receiver_last_seen", "Receiver last seen",
                      T_R_LAST_SEEN, "timestamp", nullptr, nullptr, "diagnostic");

  publishOneDiscovery("binary_sensor", "receiver_online", "Receiver online",
                      T_R_ONLINE, "connectivity", nullptr, nullptr, "diagnostic",
                      "\"payload_on\":\"true\",\"payload_off\":\"false\"");
  publishOneDiscovery("sensor", "receiver_wifi_rssi", "Receiver WiFi RSSI",
                      T_R_WIFI_RSSI, "signal_strength", "dBm", "measurement", "diagnostic");
  publishOneDiscovery("sensor", "receiver_uptime", "Receiver uptime",
                      T_R_UPTIME, "duration", "d", "total_increasing", "diagnostic");

  // ---- V1.3.0 additions ---------------------------------------------------
  // Reboot button — HA sends any payload to T_CMD_REBOOT; receiver calls ESP.restart().
  publishOneDiscovery("button", "receiver_reboot", "Reboot receiver",
                      nullptr, "restart", nullptr, nullptr, "config",
                      "\"command_topic\":\"mailbox/cmd/reboot\"");

  publishOneDiscovery("sensor", "receiver_packet_loss", "Receiver packet loss",
                      T_R_PACKET_LOSS, nullptr, nullptr, "total_increasing", "diagnostic");

  publishOneDiscovery("sensor", "receiver_freq_error", "Receiver freq error",
                      T_R_FREQ_ERROR, nullptr, "Hz", "measurement", "diagnostic");
}

////////////////////////////////////////////////////////////////////////////////
// Parser — key=value packet from sender.
//
// Expected keys (sender V3): id type seq t h p v r sok boot up br fw
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

  // V1.3.0: packet loss counter — count gaps in sender seq (uint8 wrapping arithmetic).
  // Excluded for type=4 (sender boot resets seq to 0 legitimately). lastSeq == -1
  // (first packet ever) is also excluded: no gap to measure.
  if (pktType != 4 && lastSeq >= 0) {
    uint8_t expected = (uint8_t)((uint8_t) lastSeq + 1);
    if (seq != expected) {
      uint8_t lost = (uint8_t)(seq - expected);   // wrapping subtraction handles rollover
      packetLossCount += lost;
      LOG("loss", "%u packet(s) lost (expected seq %u got %u, cumulative=%lu)",
          lost, expected, seq, packetLossCount);
    }
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
  s = getKv(payload, "fw");   if (s.length()) lastPkt.fw          = s;   // V1.0.8

  lastSeq          = (int8_t) seq;
  lastPacketRxMs   = millis();
  lastDisplayActivityMs = millis();         // wake OLED on packet
  if (!displayOn) { display.displayOn(); displayOn = true; }    // V1.0.5: soft wake

  if (mqttClient.connected()) {
    publishOnePacket();
  }

  // Sticky state transition — only on reed event packets, with the 60 s guard.
  // V1.0.6 fix A: mutate mailState ONLY after a successful publish. Previously
  // the flag was set unconditionally, so a reed event during HA reboot would
  // leave the receiver thinking it had published when it hadn't.
  //
  // V1.2.6: do NOT gate on lastPkt.reedOpen. The sender reads digitalRead(PIN_REED)
  // at packet-build time, which can be 0 even for a real lid-open event if the lid
  // closed before the sender finished sleeping + reading the BME280 (up to ~8 s).
  // pktType == 1 is already the authoritative mail-arrived signal from the ISR.
  if (pktType == 1) {
    if (!mailState && (millis() - lastMailTransitionMs > STATE_REPEAT_GUARD_MS)) {
      if (mqttClient.connected()) {
        mqttClient.beginMessage(T_STATE, true, 1);   // retained, QoS 1
        mqttClient.print("MAIL");
        mqttClient.endMessage();
        // V1.1.0 (Q3-b): legacy `mailboxstatus/switch=ON` publish removed.
        mailState = true;                            // moved (V1.0.6 fix A)
        lastMailTransitionMs = millis();             // moved (V1.0.6 fix A)
        LOG("state", "MAIL → published (reed event)");
      } else {
        // V1.2.5: queue for publish on next MQTT reconnect (see connectMqtt()).
        pendingMailState = true;
        LOG("state", "MAIL queued — MQTT disconnected, will publish on reconnect");
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

// V1.2.1: number → label for the sender_last_packet_type entity. The wire
// format still uses numbers (smaller airtime), but HA gets a readable string.
// Unknown types fall back to "type N" so a future sender adding a type isn't
// silently masked as gibberish here.
const char* packetTypeLabel(uint8_t pktType) {
  switch (pktType) {
    case 1: return "mail";
    case 2: return "heartbeat";
    case 3: return "heartbeat (low batt)";
    case 4: return "boot";
    default: return "unknown";
  }
}

void publishOnePacket() {
  // Only publish sensor values when sok=1 — sok=0 means the sender's BME280
  // failed and the values are stale. Publishing stale values would mislead HA.
  if (lastPkt.sensorOk) {
    publishOne(T_S_TEMPERATURE, String(lastPkt.tempC,       2));
    publishOne(T_S_HUMIDITY,    String(lastPkt.humidPct));
    publishOne(T_S_PRESSURE,    String(lastPkt.pressureHpa, 1));
  }
  publishOne(T_S_BATTERY_VOLTAGE,  String(lastPkt.vbatMv / 1000.0, 2),         true);
  publishOne(T_S_BATTERY_PERCENT,  batteryPercentString(lastPkt.vbatMv),       true);
  publishOne(T_S_PACKET_SEQ,       String(lastPkt.seq),                        true);
  publishOne(T_R_RSSI,             String(lastPkt.rssi, 1),                    true);
  publishOne(T_R_SNR,              String(lastPkt.snr,  1),                    true);
  publishOne(T_S_SENSOR_OK,        lastPkt.sensorOk ? "true" : "false");
  publishOne(T_S_BOOT_COUNT,       String(lastPkt.bootCount),                  true);
  if (lastPkt.bootReason.length()) publishOne(T_S_BOOT_REASON, lastPkt.bootReason, true);
  if (lastPkt.fw.length())         publishOne(T_S_VERSION,     lastPkt.fw,         true);
  publishOne(T_S_LAST_PACKET_TYPE, String(packetTypeLabel(lastPkt.packetType)), true);

  // last_seen — ISO 8601 timestamp from NTP-synced clock. HA renders as "X minutes ago".
  time_t now = time(nullptr);
  if (now > 1700000000) {                       // sanity: NTP synced (>2023-11)
    struct tm* tmInfo = gmtime(&now);
    char isoBuf[32];
    strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", tmInfo);
    publishOne(T_R_LAST_SEEN, String(isoBuf), true);
  }

  // V1.3.0: packet loss counter + frequency error.
  publishOne(T_R_PACKET_LOSS, String(packetLossCount), true);
  publishOne(T_R_FREQ_ERROR,  String(radio.getFrequencyError(), 1));

  // V1.1.0 (Q3-b): the V2_real `mailboxstatus/feather` JSON-blob publish has
  // been removed. All values are now first-class HA entities on their own
  // `mailbox/sender/*` and `mailbox/receiver/*` topics, so the blob has no
  // remaining consumers in the project.
}

void publishDiagnostics() {
  publishOne(T_R_WIFI_RSSI, String(WiFi.RSSI()));
  // V1.0.8: uptime in days, 2 decimals (was seconds in V1.0.7).
  //   86 400 000 ms = 1 day. Float division so fractional days render correctly.
  //   Discovery unit_of_measurement was changed from "s" → "d" in lockstep.
  publishOne(T_R_UPTIME, String(millis() / 86400000.0, 2));
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

  // Top row: WiFi/MQTT status + clock on the left, FW version on the right.
  // V1.0.8: version moved here from the boot-only splash so it's always visible.
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
  // Right-justified FW version. Reset alignment afterwards so the rest of
  // renderOled() keeps drawing left-aligned as it always has.
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(128, 0, FW_VERSION);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

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
// Clear the sticky mail state — called from the local PRG long-press.
// (V1.1.0: HA dashboard no longer routes through here; the dashboard
// publishes "EMPTY" directly to mailbox/state and onMqttMessage adopts it.)
////////////////////////////////////////////////////////////////////////////////
void clearMailState(const char* source) {
  // V1.0.6 fix A (same pattern as the MAIL transition): only mutate mailState
  // after the publish lands on the broker. Otherwise an offline long-press
  // would silently flip the local flag while the broker still showed MAIL,
  // and the receiver would refuse to re-publish "MAIL" on the next real
  // reed event (already EMPTY locally, broker out of sync).
  if (mqttClient.connected()) {
    mqttClient.beginMessage(T_STATE, true, 1);   // retained, QoS 1
    mqttClient.print("EMPTY");
    mqttClient.endMessage();
    mailState = false;                           // moved (V1.0.6 fix A)
    LOG("state", "EMPTY ← cleared by %s", source);
  } else {
    LOG("state", "EMPTY clear PENDING (source=%s) — MQTT disconnected; retry later", source);
  }
}
