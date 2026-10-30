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
#include <EEPROM.h>

//! @name Stored trigger rate
//! The Timer1 TOP set by the base station's 'F' command (a PKT_RATE packet),
//! kept in EEPROM so this board comes back up at the same rate after a reset
//! or power cycle. Record at address 0: magic, TOP, ~TOP.
//! @{
constexpr int      RATE_EEPROM_ADDR = 0;
constexpr uint16_t RATE_MAGIC       = 0x7A7E;
struct RateRecord { uint16_t magic; uint16_t top; uint16_t check; };
//! @}

//! @brief Timer1 TOP stored in EEPROM, or the default rate's TOP if none is valid.
uint16_t loadTimer1Top() {
    RateRecord rec;
    EEPROM.get(RATE_EEPROM_ADDR, rec);
    if (rec.magic == RATE_MAGIC && rec.check == (uint16_t)~rec.top && timer1TopIsValid(rec.top)) {
        return rec.top;
    }
    return triggerRateToTop(TRIGGER_RATE_HZ);
}

//! @brief Store a Timer1 TOP in EEPROM (only changed bytes are written).
void saveTimer1Top(uint16_t top) {
    const RateRecord rec = { RATE_MAGIC, top, (uint16_t)~top };
    EEPROM.put(RATE_EEPROM_ADDR, rec);
}

//! New Timer1 TOP received in a PKT_RATE packet; 0 = none. Handled in loop().
volatile uint16_t new_timer1_top = 0;

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
//   PKT_SYNC    - read the base station's current TCNT1 and apply a
//                 PLL correction to keep the local timer in phase.
//                 Leaves tick_delay untouched.
//   PKT_TRIGGER - same PLL correction, plus store the burst delay used
//                 by the Timer1 ISR. A trigger with a zero delay is
//                 dropped entirely so it can never clear the delay.
//   PKT_RESET   - flag a reboot, performed from loop().
// ---------------------------------------------------------------
void onRadioIRQ() {
    bool tx_ok, tx_fail, rx_ready;
    radio.clearIRQFlags(tx_ok, tx_fail, rx_ready);

    if (!rx_ready) return;

    CommsPacket pkt;
    while (radio.receivePacket(pkt)) {
        switch (pkt.type) {
            case PKT_SYNC:
                // Correct the local timer phase to match the base station
                // A timestamp above our TOP means the base runs another rate (its
                // PKT_RATE was missed); correcting would push TCNT1 past OCR1A.
                if (pkt.timestamp <= timer1_top) pllCorrect((int16_t)(pkt.timestamp - TCNT1));
                break;

            case PKT_TRIGGER:
                //! Zero is reserved; ignore the packet rather than clear the delay
                if (pkt.tick_delay == 0) break;
                //! tick_delay is declared externally (timers.h)
                tick_delay = pkt.tick_delay;
                // Correct the local timer phase to match the base station
                // A timestamp above our TOP means the base runs another rate (its
                // PKT_RATE was missed); correcting would push TCNT1 past OCR1A.
                if (pkt.timestamp <= timer1_top) pllCorrect((int16_t)(pkt.timestamp - TCNT1));
                break;

            case PKT_RATE:
                // Only act on a TOP that differs: the base station also sends
                // its rate at every boot.
                if (RadioController::isRatePacket(pkt) && pkt.timestamp != timer1_top) {
                    new_timer1_top = pkt.timestamp;
                }
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
  // Trigger rate set by the base station's 'F' command (10 Hz by default)
  timer1_init(loadTimer1Top());

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
  // New trigger rate from the base station: store it, then reset so the board
  // comes up cleanly at the new rate (PLL re-locks on the next sync packet).
  if (new_timer1_top != 0) {
      saveTimer1Top(new_timer1_top);
      DEBUG_INFO_PRINTLN("[INFO] New trigger rate received. Resetting.");
      Serial.flush();
      systemReset();   // does not return
  }

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

