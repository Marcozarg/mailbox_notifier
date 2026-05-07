
// Basic
#include <ArduinoMqttClient.h>
#include <ESP8266WiFi.h>
#include "arduino_secrets.h"

// OLED
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Lora
#include <LoRa.h>

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
char mqttuser[] = MQTTUSER;
char mqttpass[] = MQTTPASS;

const char broker[] = MQTTBROKER;
int        port     = 1883;
const char clientId[]  = "MailboxSensor";
const char topic[]  = "arduino/mailbox/switch";
unsigned long currenttime=0,buttonpresstime=0;

// Define the pins used by the LoRa module
const int csPin = 5;      // LoRa radio chip select
const int resetPin = 14;  // LoRa radio reset
const int irqPin = 2;     // Must be a hardware interrupt pin

// LED connection
const int ledPin = 0;     // D3 pin

// Pushbutton variables
int buttonPin = 12;       // D6 pin
int ButtonCount = 0;
int sendButtonState;

// Source and sensorAddress1 addresses
byte localAddress = 0x01;    // Address of this device (Controller = 0x01)
byte sensorAddress1 = 0xAA;  // Address of Sensor 1

// OLED parameters
#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D  // Change if required

// Define display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

void setup() {
  Serial.begin(9600);
  while (!Serial) {
  ; // wait for serial port to connect. Needed for native USB port only
  }

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  // Set LED as output
  pinMode(ledPin, OUTPUT);

  // Set pushbutton as input
  pinMode(buttonPin, INPUT_PULLUP);
  
  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(WHITE);        // Draw white text
  display.setCursor(0,0);             // Start at top-left corner

  // Show the display buffer on the screen. You MUST call display() after
  // drawing commands to make them visible on screen!
  display.display();

  // We start by connecting to a WiFi network
  delay(2000);
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  display.println(F("Connecting "));
  display.println(ssid);
  display.display();
  Serial.println(ssid);
  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.setHostname("arduinomailman");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  
  long rssi = WiFi.RSSI();

  display.print(F("WiFi connected "));
  display.println(rssi);
  display.println(F("IP address:"));
  display.println(WiFi.localIP());
  display.display();
  
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI:");
  Serial.println(rssi);
  
  display.println(F("Connecting broker.."));
  display.display();
   
  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(broker);

  mqttClient.setUsernamePassword(mqttuser, mqttpass);
  mqttClient.setId(clientId);
  if (!mqttClient.connect(broker, port)) {
    display.println(F("MQTT connection failed!"));
    display.println(mqttClient.connectError());
    display.display();
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  Serial.println();
  display.println(F("MQTT connection OK"));
  display.display();
//  client.publish("homeassistant/switch/ArduinoMailBox/config",buffer,true);
}

void loop() {
  // call poll() regularly to allow the library to send MQTT keep alives which
  // avoids being disconnected by the broker
  mqttClient.poll();
  currenttime=millis();
  // Get pushbutton state
  sendButtonState = digitalRead(buttonPin);

  // Send packet if button pressed
  if (sendButtonState == LOW) {
    // Send packet
    buttonpresstime=millis();
    ButtonCount++;
    Serial.println("Sending message to topic: ");
    Serial.println(topic);
    Serial.print(" ON");
    // Clear the buffer
    display.clearDisplay();
    display.setTextColor(WHITE);        // Draw white text
    display.setCursor(0,0);             // Start at top-left corner
    display.setTextSize(1);             // Normal 1:1 pixel scale
    display.println(F("Nappia painettu "));
    display.setTextSize(3);             // Normal 1:1 pixel scale
    display.setCursor(50,30); 
    display.println(ButtonCount);
    display.display();

    mqttClient.beginMessage(topic);
    mqttClient.print("ON");
    mqttClient.endMessage();
    Serial.println();

    Serial.println("Sending packet: ");
    digitalWrite(ledPin, HIGH);
    Serial.println("led on");
    delay(1000);
    digitalWrite(ledPin, LOW);
    Serial.println("led off");
  }
  // Clear screen, if time is more than 2min from last button push
//  if (currenttime - buttonpresstime > 120000){
  if (currenttime - buttonpresstime > 10000){
    display.clearDisplay();
    display.display();
  }

}
