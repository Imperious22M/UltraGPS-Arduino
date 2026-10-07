/*
 Transmitter-station program for the UltraGPS system.
 This board is the radio RECEIVER: it listens for packets from the base
 station, keeps its Timer1 phase-locked to the base station, and pulses
 the ultrasonic transmitter's trigger pin. A reset packet from the base
 station reboots this board.
*/

#include <Arduino.h>
#include <SPI.h>
#include "radio.h"
#include "debug_macros.h"
#include "led_control.h"
#include "timers.h"
#include "sysreset.h"

//! Serial baud rate for debugging
constexpr unsigned int BAUD_RATE  = 38400;

//! Instantiate all classes
constexpr uint8_t radioIRQPin = 2;  //! Must be an interrupt-capable pin (2 or 3 on Uno)
// Radio initialization with CE=9, CSN=10
RadioController radio(9, 10);
// LED controller initialization
LedControl leds(3, 4, 6, 7, 8);      //! STAT_1, STAT_2, STAT_3, PULSE_TRIG, RADIO_STATUS

//! Set by the radio ISR when a reset packet arrives; acted on in loop()
volatile bool reset_requested = false;

// ---------------------------------------------------------------
// onRadioIRQ()
//
// Fires on the falling edge of the radio IRQ pin (RX_DR only).
// Drains the RX FIFO and dispatches on packet type:
//   PKT_TRIGGER - read the base station's current TCNT1 and apply a
//                 PLL correction to keep the local timer in phase.
//   PKT_RESET   - flag a reboot, performed from loop().
// ---------------------------------------------------------------
void onRadioIRQ() {
    bool tx_ok, tx_fail, rx_ready;
    radio.clearIRQFlags(tx_ok, tx_fail, rx_ready);

    if (!rx_ready) return;

    CommsPacket pkt;
    while (radio.receivePacket(pkt)) {
        switch (pkt.type) {
            case PKT_TRIGGER:
                //! tick_delay is declared externally (timers.h)
                tick_delay = pkt.tick_delay;
                // Correct the local timer phase to match the base station
                pllCorrect((int16_t)(pkt.timestamp - TCNT1));
                break;

            case PKT_RESET:
                if (RadioController::isResetPacket(pkt)) {
                    reset_requested = true;
                }
                break;

            default:
                break;
        }
    }
}

void setup()
{
  //! Disarm the watchdog left running by a software reset (see sysreset.h)
  systemResetGuard();

  //! Serial setup
  Serial.begin(BAUD_RATE);
  //while (!Serial && millis() < 3000);
  DEBUG_PRINTLN("=== Transmitter station starting ===");

  //! Timer setup
  timer1_init_10Hz();

  //! Pin setup
  pinMode(triggerPin, OUTPUT);
  pinMode(radioIRQPin, INPUT);
  //! Initialiaze LEDs
  leds.init(0);
  // Initialize radio as the link slave (listens to the base station)
  if (!radio.init(RADIO_ROLE_TRANSMITTER)) {
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
  //! Reset packet received from the base station: reboot this board
  if (reset_requested) {
      DEBUG_INFO_PRINTLN("[INFO] Reset packet received. Resetting.");
      Serial.flush();
      systemReset();   //! does not return
  }

  //! Activation of trigger pin is found in ISR interrupt
  //! in timers.cpp
  if (trigger_fired) {
      trigger_fired = false;
      DEBUG_INFO_PRINTLN("[INFO] Trigger fired.");
      // Flash the pulse LED to indicate the trigger fired
      leds.setPulseTrig(true);
  }
}

