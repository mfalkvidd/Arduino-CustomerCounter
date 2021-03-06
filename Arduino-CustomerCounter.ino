#include "settings.h"
/**
   The MySensors Arduino library handles the wireless radio link and protocol
   between your home built sensors/actuators and HA controller of choice.
   The sensors forms a self healing radio network with optional repeaters. Each
   repeater and gateway builds a routing tables in EEPROM which keeps track of the
   network topology allowing messages to be routed to nodes.

   Created by Henrik Ekblad <henrik.ekblad@mysensors.org>
   Copyright (C) 2013-2015 Sensnology AB
   Full contributor list: https://github.com/mysensors/Arduino/graphs/contributors

   Documentation: http://www.mysensors.org
   Support Forum: http://forum.mysensors.org

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   version 2 as published by the Free Software Foundation.

 *******************************

   REVISION HISTORY
   Version 1.0 - Henrik EKblad
   Contribution by tekka,
   Contribution by a-lurker and Anticimex,
   Contribution by Norbert Truchsess <norbert.truchsess@t-online.de>
   Contribution by Ivo Pullens (ESP8266 support)

   DESCRIPTION
   The EthernetGateway sends data received from sensors to the WiFi link.
   The gateway also accepts input on ethernet interface, which is then sent out to the radio network.

   VERA CONFIGURATION:
   Enter "ip-number:port" in the ip-field of the Arduino GW device. This will temporarily override any serial configuration for the Vera plugin.
   E.g. If you want to use the defualt values in this sketch enter: 192.168.178.66:5003

   LED purposes:
   - To use the feature, uncomment WITH_LEDS_BLINKING in MyConfig.h
   - RX (green) - blink fast on radio message recieved. In inclusion mode will blink fast only on presentation recieved
   - TX (yellow) - blink fast on radio message transmitted. In inclusion mode will blink slowly
   - ERR (red) - fast blink on error during transmission error or recieve crc error

   See http://www.mysensors.org/build/esp8266_gateway for wiring instructions.
   nRF24L01+  ESP8266
   VCC        VCC
   CE         GPIO4
   CSN/CS     GPIO15
   SCK        GPIO14
   MISO       GPIO12
   MOSI       GPIO13
   GND        GND

   Not all ESP8266 modules have all pins available on their external interface.
   This code has been tested on an ESP-12 module.
   The ESP8266 requires a certain pin configuration to download code, and another one to run code:
   - Connect REST (reset) via 10K pullup resistor to VCC, and via switch to GND ('reset switch')
   - Connect GPIO15 via 10K pulldown resistor to GND
   - Connect CH_PD via 10K resistor to VCC
   - Connect GPIO2 via 10K resistor to VCC
   - Connect GPIO0 via 10K resistor to VCC, and via switch to GND ('bootload switch')

    Inclusion mode button:
   - Connect GPIO5 via switch to GND ('inclusion switch')

   Hardware SHA204 signing is currently not supported!

   Make sure to fill in your ssid and WiFi password below for ssid & pass.
*/

#include <ArduinoOTA.h>

// Enable debug prints to serial monitor
#define MY_DEBUG

// Use a bit lower baudrate for serial prints on ESP8266 than default in MyConfig.h
#define MY_BAUD_RATE 9600

// Enables and select radio type (if attached)
//#define MY_RADIO_NRF24
//#define MY_RADIO_RFM69

#define MY_GATEWAY_ESP8266

#define MY_ESP8266_SSID "ParfymeriExklusiv.se"
//#define MY_ESP8266_SSID "Acee"

// Set the hostname for the WiFi Client. This is the hostname
// it will pass to the DHCP server if not static.
#define MY_ESP8266_HOSTNAME "customer-counter"

// Enable UDP communication
//#define MY_USE_UDP

// Enable MY_IP_ADDRESS here if you want a static ip address (no DHCP)
#define MY_IP_ADDRESS 192,168,1,115

// If using static ip you need to define Gateway and Subnet address as well
#define MY_IP_GATEWAY_ADDRESS 192,168,1,1
#define MY_IP_SUBNET_ADDRESS 255,255,255,0

// The port to keep open on node server mode
#define MY_PORT 5003

// How many clients should be able to connect to this gateway (default 1)
#define MY_GATEWAY_MAX_CLIENTS 2

// Controller ip address. Enables client mode (default is "server" mode).
// Also enable this if MY_USE_UDP is used and you want sensor data sent somewhere.
//#define MY_CONTROLLER_IP_ADDRESS 192, 168, 178, 68

// Enable inclusion mode
//#define MY_INCLUSION_MODE_FEATURE

// Enable Inclusion mode button on gateway
// #define MY_INCLUSION_BUTTON_FEATURE
// Set inclusion mode duration (in seconds)
//#define MY_INCLUSION_MODE_DURATION 60
// Digital pin used for inclusion mode button
//#define MY_INCLUSION_MODE_BUTTON_PIN  3


// Set blinking period
// #define MY_DEFAULT_LED_BLINK_PERIOD 300

// Flash leds on rx/tx/err
// Led pins used if blinking feature is enabled above
#define MY_DEFAULT_ERR_LED_PIN 16  // Error led pin
#define MY_DEFAULT_RX_LED_PIN  16  // Receive led pin
#define MY_DEFAULT_TX_LED_PIN  16  // the PCB, on board LED

#define WIFI_POWER_DBM 10 // 20.5 is default

#if defined(MY_USE_UDP)
#include <WiFiUDP.h>
#else
#include <ESP8266WiFi.h>
#endif

#include <MySensors.h>

#define CHILD_ID_TRIPPED 0
#define CHILD_ID_CUSTOMERS 2
#define DIGITAL_INPUT_SENSOR D2
const unsigned long MIN_UPDATE_PERIOD = 1 * 60 * 1000;
MyMessage msg(CHILD_ID_TRIPPED, V_TRIPPED);
MyMessage wattMsg(CHILD_ID_CUSTOMERS, V_WATT);
MyMessage kwhMsg(CHILD_ID_CUSTOMERS, V_KWH);
MyMessage pcMsg(CHILD_ID_CUSTOMERS, V_VAR1);
double wh = 0;
float pulseCount = 0;
bool pcReceived = false;


#include <ESP8266WiFi.h>
extern "C" { // For ESP sleep
#include <user_interface.h>
}

void setup()
{
  WiFi.setOutputPower(WIFI_POWER_DBM); // Lower power to avoid interfering with the PIR
  request(CHILD_ID_CUSTOMERS, V_VAR1);
  pinMode(DIGITAL_INPUT_SENSOR, INPUT);
  ArduinoOTA.setHostname(MY_ESP8266_HOSTNAME);
  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nArduinoOTA end");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.setPassword((const char *)OTA_PASSWORD);
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  // Fetch last known pulse count value from gw
}

void presentation()
{
  sendSketchInfo("Motion Sensor", "1.1");
  present(CHILD_ID_TRIPPED, S_MOTION);
  wait(100);
  present(CHILD_ID_CUSTOMERS, S_POWER);
}


void loop()
{
  static bool lastTripped = true;
  static unsigned long lastTrippedTime = 0;
  bool tripped = digitalRead(DIGITAL_INPUT_SENSOR) == HIGH;
  if (tripped != lastTripped || millis() - lastTrippedTime > MIN_UPDATE_PERIOD)
  {
    lastTripped = tripped;
    Serial.print(millis());
    Serial.print(" ");
    Serial.print((millis() - lastTrippedTime) / 1000.0);
    Serial.print(" ");
    Serial.println(tripped ? "Yes" : "No");
    send(msg.set(tripped ? "1" : "0")); // Send tripped value to gw
    lastTrippedTime = millis();
    wifi_set_sleep_type(LIGHT_SLEEP_T); // This + the delay lets the esp go to low power mode
    if (tripped) {
      pulseCount += 0.5;
      if (pcReceived) {
        send(kwhMsg.set(pulseCount, 1));
      } else {
        // No count received. Try requesting it again
        request(CHILD_ID_CUSTOMERS, V_VAR1);
      }
    }
  }
  ArduinoOTA.handle();
  delay(100);
}

void receive(const MyMessage & message)
{
  if (message.type == V_VAR1) {
    float gwPulseCount = message.getFloat();
    Serial.print("Received last pulse count from gw: ");
    Serial.println(gwPulseCount);
    Serial.print("Local pulse count was: ");
    Serial.println(pulseCount);
    pulseCount += gwPulseCount;
    pcReceived = true;
  }
}

