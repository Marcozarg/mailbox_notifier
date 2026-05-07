#define HELTEC_POWER_BUTTON
#include <heltec_unofficial.h>

// Wifi
#include <WiFi.h>
#include "arduino_secrets.h"

// MQTT
#include <ArduinoMqttClient.h>

// Lora
#include <LoRa.h>

// JSON
#include <ArduinoJson.h>

// Pause between transmited packets in seconds.
// Set to zero to only transmit a packet when pressing the user button
// Will not exceed 1% duty cycle, even if you set a lower value.
#define PAUSE               300

// Frequency in MHz. Keep the decimal point to designate float.
// Check your own rules and regulations to see what is legal where you are.
#define FREQUENCY           866.0       // for Europe
// #define FREQUENCY           905.2       // for US

// LoRa bandwidth. Keep the decimal point to designate float.
// Allowed values are 7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125.0, 250.0 and 500.0 kHz.
#define BANDWIDTH           250.0

// Number from 5 to 12. Higher means slower but higher "processor gain",
// meaning (in nutshell) longer range and more robust against interference. 
#define SPREADING_FACTOR    9

// Transmit power in dBm. 0 dBm = 1 mW, enough for tabletop-testing. This value can be
// set anywhere between -9 dBm (0.125 mW) to 22 dBm (158 mW). Note that the maximum ERP
// (which is what your antenna maximally radiates) on the EU ISM band is 25 mW, and that
// transmissting without an antenna can damage your hardware.
#define TRANSMIT_POWER      0

// Wifi settings
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
char mqttuser[] = MQTTUSER;
char mqttpass[] = MQTTPASS;

// MQTT Broker settings
const char broker[] = MQTTBROKER;
int        port     = 1883;
const char clientId[]  = "MailboxReceiver";
const char topicswitch[]  = "mailboxstatus/switch";
const char topicfeather[]  = "mailboxstatus/feather";
unsigned long currenttime=0,buttonpresstime=0;

// Lora settings
String rxdata;
String msg;
volatile bool rxFlag = false;
long counter = 0;
uint64_t last_tx = 0;
uint64_t tx_time;
uint64_t minimum_pause;

// JSON
StaticJsonDocument<200> doc;
char buffer[128];

// Wifi client settings
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
  ; // wait for serial port to connect. Needed for native USB port only
  }
  heltec_setup();
  delay(1000);

  // Initialising the UI will init the display too.
  display.init();
  display.setFont(ArialMT_Plain_10);
  display.flipScreenVertically();
  
  // The coordinates define the left starting point of the text
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  // We start by connecting to a WiFi network
  delay(2000);
  both.print("Connecting to ");
  both.println(ssid);

  WiFi.setHostname("arduinomailman");
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  
  long rssi = WiFi.RSSI();

  both.print("WiFi connected ");
  both.println(rssi);
  both.print("IP:");
  both.println(WiFi.localIP());

  both.println("Connecting broker..");
  Serial.print("Attempting to connect to the MQTT broker: ");
  both.println(broker);

  mqttClient.setUsernamePassword(mqttuser, mqttpass);
  mqttClient.setId(clientId);
  if (!mqttClient.connect(broker, port)) {
    both.println("MQTT connection failed!");
    both.println(mqttClient.connectError());
    while (1);
  }
  both.println("MQTT connection OK");
  delay(5000);
  display.cls();
  
  // LORA INIT
    both.println("Lora init");
  RADIOLIB_OR_HALT(radio.begin());
  // Set the callback function for received packets
  radio.setDio1Action(rx);
  // Set radio parameters
  both.printf("Frequency: %.2f MHz\n", FREQUENCY);
  RADIOLIB_OR_HALT(radio.setFrequency(FREQUENCY));
  both.printf("Bandwidth: %.1f kHz\n", BANDWIDTH);
  RADIOLIB_OR_HALT(radio.setBandwidth(BANDWIDTH));
  both.printf("Spreading Factor: %i\n", SPREADING_FACTOR);
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(SPREADING_FACTOR));
  //both.printf("TX power: %i dBm\n", TRANSMIT_POWER);
  //RADIOLIB_OR_HALT(radio.setOutputPower(TRANSMIT_POWER));
  // Start receiving
  RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));

}

void loop() {
  heltec_loop();
  mqttClient.poll();

  if (button.isSingleClick()){
      heltec_display_power(true);
  }
  if (button.isDoubleClick()){
      heltec_setup();
      both.println("Display on");
  }

  // If a packet was received, display it and the RSSI and SNR
  if (rxFlag) {
    rxFlag = false;
    radio.readData(rxdata);
    if (_radiolib_status == RADIOLIB_ERR_NONE) {
      both.printf("RX[%s]\n", rxdata.c_str());
      both.printf("RSSI: %.2f dBm\n", radio.getRSSI());
      both.printf("SNR: %.2f dB\n", radio.getSNR());
      // Parse message and send to MQTT
      
      msg = rxdata.c_str();
//      both.println(msg);

      int index1 = msg.indexOf(';');
      String temp = msg.substring(0, index1);

      int index2 = msg.indexOf(';', index1 +1);
      String humid = msg.substring(index1+1, index2);

      int index3 = msg.indexOf(';', index2 +1);
      String lipo = msg.substring(index2+1, index3);

      int index4 = msg.indexOf(';', index3 +1);
      String msgcount = msg.substring(index3+1, index4);

      // Create JSON matrix
      doc["temp"] = temp;
      doc["humid"] = humid;
      doc["lipo"] = lipo;
      doc["msgcount"] = msgcount;
      doc["rssi"] = radio.getRSSI();
      doc["snr"] = radio.getSNR();
      serializeJson(doc, buffer);
//      Serial.println();
//      serializeJsonPretty(doc, Serial);

      // Send switch state (ON)
      mqttClient.beginMessage(topicswitch);
      mqttClient.print("ON");
      mqttClient.endMessage();

      // Send DHT,Battery etc
      mqttClient.beginMessage(topicfeather);
      mqttClient.print(buffer);
      mqttClient.endMessage();

    }
    RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));
  }
}

// Can't do Serial or display things here, takes too much time for the interrupt
void rx() {
  rxFlag = true;
}
