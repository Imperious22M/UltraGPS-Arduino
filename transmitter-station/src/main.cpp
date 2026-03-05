/*
 Receiver program for the UltraGPS system.
 The receiver is radio-based on the commands it receives.
*/

#include <Arduino.h>
#include <SPI.h>
#include "radio.h"
#include "debug_macros.h"
#include "led_control.h"
#include "timers.h"

//! Serial baud rate for debugging
constexpr unsigned int BAUD_RATE  = 38400;

//! Instantiate all classes
constexpr uint8_t radioIRQPin = 2;  //! Must be an interrupt-capable pin (2 or 3 on Uno)
// Radio initialization with CE=9, CSN=10
RadioController radio(9, 10);
// LED controller initialization
LedControl leds(3, 4, 6, 7, 8);      //! STAT_1, STAT_2, STAT_3, PULSE_TRIG, RADIO_STATUS

// ---------------------------------------------------------------
// onRadioIRQ()
//
// Fires on the falling edge of the radio IRQ pin (RX_DR only).
// Reads the packet directly via SPI (polled — safe in ISR), updates
// TCNT1 to match the transmitter, then arms the one-shot trigger.
// ---------------------------------------------------------------
void onRadioIRQ() {
    bool tx_ok, tx_fail, rx_ready;
    uint16_t timer_val, tick_delay;
    radio.clearIRQFlags(tx_ok, tx_fail, rx_ready);

    if (!rx_ready) return;
    if(!radio.receiveTrigger(timer_val, tick_delay)) return;

    // Interrupts are already disabled inside an ISR — TCNT1 write is safe
    // Update the current count of the timer to match the transmitter's value, 
    // then schedule the trigger
    TCNT1        = timer_val;
    timer_offset = 0;

    schedule_trigger(timer_val, tick_delay);
}

void setup()
{
  //! Serial setup
  Serial.begin(BAUD_RATE);
  while (!Serial && millis() < 3000);
  DEBUG_PRINTLN("=== Receiver Starting ===");

  //! Timer setup
  timer1_init();
  //! Pin setup
  pinMode(triggerPin, OUTPUT);
  pinMode(radioIRQPin, INPUT);
  //! Initialiaze LEDs
  leds.init(0);
  // Initialize radio and synchronize transmitters clocks
  if (!radio.init(true)) {
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
  
  leds.setRadioStatus(true);

}

void loop()
{
  if (trigger_fired) {
      trigger_fired = false;
      DEBUG_INFO_PRINTLN("[INFO] Trigger fired.");
      // Flash the pulse LED to indicate the trigger fired
      leds.setPulseTrig(true);
  }
}

