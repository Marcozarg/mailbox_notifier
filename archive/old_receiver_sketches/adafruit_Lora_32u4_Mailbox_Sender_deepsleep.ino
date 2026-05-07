// LORA
#include <LoRa.h>
// DHT
#include <DHT.h>

//Sleep
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>

// Define the pins used by the LoRa module
const int csPin = 8;      // LoRa radio chip select
const int resetPin = 4;   // LoRa radio reset
const int irqPin = 7;     // Must be a hardware interrupt pin

// Reed switch and led
const byte ledPin = 5;
const byte interruptPin = 3;

// DHT settings
#define DHTPIN 11
#define DHTTYPE    DHT22     // DHT 22 (AM2302)
DHT dht(DHTPIN, DHTTYPE);

// Lipo Battery Settings
#define VBATPIN A9

// Outgoing Message counter
String message;
byte msgCount = 0;

void setup() {
  Serial.begin(115200);
//  while(!Serial); // wait for Arduino Serial Monitor (native USB boards)

  dht.begin();
  pinMode(interruptPin, INPUT_PULLUP);
//  pinMode(interruptPin, INPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // Debugging LED is on while board is not sleeping
  delay(2000);

  // Setup LoRa module
  LoRa.setPins(csPin, resetPin, irqPin);

  // Start LoRa module at local frequency
  // 433E6 for Asia
  // 866E6 for Europe
  // 915E6 for North America

  if (!LoRa.begin(866E6)) {
    Serial.println("Starting LoRa failed!");
    while (1)
      ;
  }
  else {
    Serial.println("Starting LoRa OK!");
  }
  // Lora settings
  LoRa.setSpreadingFactor(9);           // ranges from 6-12,default 7 see API docs
  LoRa.setTxPower(20);                  // txPower - TX power in dB, defaults to 17
  LoRa.setSignalBandwidth(250E3);      // signalBandwidth - signal bandwidth in Hz, defaults to 125E3. Supported values are 7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3, and 500E3.
}

void ReedInterrupt(void) {
  // Wake up section
  detachInterrupt(digitalPinToInterrupt(interruptPin));
}

void enterSleep(void){
  /*
    attachInterrupt(digitalPinToInterrupt(pin), ISR, mode) (recommended)
    attachInterrupt(interrupt, ISR, mode) (not recommended)
    attachInterrupt(pin, ISR, mode) (Not recommended. Additionally, this syntax only works on Arduino SAMD Boards, Uno WiFi Rev2, Due, and 101.)
    
    interrupt: the number of the interrupt. Allowed data types: int.
    pin: the Arduino pin number.
    ISR: the ISR to call when the interrupt occurs; this function must take no parameters and return nothing. This function is sometimes referred to as an interrupt service routine.
    mode: defines when the interrupt should be triggered. Four constants are predefined as valid values:

    LOW to trigger the interrupt whenever the pin is low,
    CHANGE to trigger the interrupt whenever the pin changes value
    RISING to trigger when the pin goes from low to high,
    FALLING for when the pin goes from high to low.
    The Due, Zero and MKR1000 boards allow also:
    HIGH to trigger the interrupt whenever the pin is high.
  */
  attachInterrupt(digitalPinToInterrupt(interruptPin), ReedInterrupt, HIGH);
  // Do not use LOW
  delay(100);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  LoRa.sleep(); // put the radio to sleep
  digitalWrite(ledPin, LOW); // Debugging LED is on while board is not sleeping
  delay(2000);
  sleep_enable();

//if (digitalRead(interruptPin)){
//  sleep_mode();
//}

  // Function continues from here after sleep
  sleep_disable();
  digitalWrite(ledPin, HIGH); // Debugging LED is on while board is not sleeping
}

void loop() {

  
  if (!LoRa.begin(866E6)) {
    Serial.println("Starting LoRa failed!");
    while (1)
    ;
  }
  else {
    Serial.println("Starting LoRa OK!");
  }

  // Lora settings
  LoRa.setSpreadingFactor(9);           // ranges from 6-12,default 7 see API docs
  LoRa.setTxPower(20);                  // txPower - TX power in dB, defaults to 17
  LoRa.setSignalBandwidth(250E3);      // signalBandwidth - signal bandwidth in Hz, defaults to 125E3. Supported values are 7.8E3, 10.4E3, 15.6E3, 20.8E3, 31.25E3, 41.7E3, 62.5E3, 125E3, 250E3, and 500E3.


  // Read DHT values
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  Serial.print("t: " );
  Serial.print(t);
  Serial.print(" h: " );
  Serial.print(h);

  // Read battery voltage
  float measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage
  Serial.print(" VBat: " );
  Serial.println(measuredvbat);

  // Combine message
  message = String(t) + ";" + String(h) + ";" + String(measuredvbat) + ";" + msgCount;

  // Send packet
  LoRa.beginPacket();
  LoRa.print(message);
  LoRa.endPacket();

  msgCount++;
  delay(5000);
  enterSleep();
}