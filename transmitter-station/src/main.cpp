/*
 Receiver program for the UltraGPS system.
 The receiver is radio-based on the commands it receives.
*/

#include <Arduino.h>
#include <SPI.h>
#include "radio.h"
#include "debug_macros.h"
#include "led_control.h"

//! Base Station Parameters
constexpr unsigned int TRIGGER_HIGH_DURATION = 300;     //! How long to hold the high state of the trigger pin (microseconds)

//! Serial baud rate for debugging
constexpr unsigned int BAUD_RATE  = 38400;

//! Instantiate all classes
constexpr uint8_t radioIRQPin = 2;  //! Must be an interrupt-capable pin (2 or 3 on Uno)
RadioController radioCtrl(9, 10, 5);  //! CE, CSN, Trigger pins
LedControl leds(3, 4, 6, 7, 8);      //! STAT_1, STAT_2, STAT_3, PULSE_TRIG, RADIO_STATUS

void setup()
{
  //! Serial setup
  Serial.begin(BAUD_RATE);
  while (!Serial && millis() < 3000);
  DEBUG_PRINTLN("=== Receiver Starting ===");

  //! Initialiaze LEDs
  leds.init(0);

  // Initialize radio and synchronize transmitters clocks
  if (!radioCtrl.init()) {
    DEBUG_INFO_PRINTLN("[ERROR] Radio init failed. Halting.");
    while (true) {
      DEBUG_INFO_PRINTLN("[ERROR] Still halted.");
      leds.setRadioStatus(true);
      delay(1000);
      leds.setRadioStatus(false);
      delay(1000);
    }
  }else{
    attachInterrupt(digitalPinToInterrupt(radioIRQPin), onRadioIRQ, FALLING);
  }

  if (!radioCtrl.syncReceiver(0)) {
    DEBUG_INFO_PRINTLN("[ERROR] Sync failed. Halting.");
    while (true) {
      DEBUG_INFO_PRINTLN("[ERROR] Still halted.");
      leds.setRadioStatus(true);
      delay(1000);
      leds.setRadioStatus(false);
      delay(1000);
    }
  }

  //! Radio initialized successfully - keep RADIO_STATUS LED on
  leds.setRadioStatus(true);

}

void loop()
{
  radioCtrl.handleRadioPacket();
}

