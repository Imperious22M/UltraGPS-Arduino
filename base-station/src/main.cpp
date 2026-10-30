//! @file main.cpp
//! @brief Base-station firmware entry point: the global objects, setup(), syncPLL()
//!        and loop().
//!
//! C++ program to control the base station of the UltraGPS system. When commanded
//! over serial, it sends a radio pulse (a trigger packet) to the transmitter station
//! and then measures the next ultrasonic ping, which is received by the set of
//! receivers. The program returns the tick count of all the receivers after the
//! radio pulse is sent.
//!
//! The radio pulse does not start the ping: the transmitter station fires one on
//! every Timer1 cycle (10 Hz), kept in phase with the base station by the radio
//! packets, after the burst delay set by the latest trigger packet.
//!
//! Example program output:
//! @code
//! N: 4404, 3226, 4063, 4041, 3133, 4109
//! ...
//! @endcode
//! Each measurement is one line that starts with "N: ", or with "C: " in continuous
//! mode (the 'C' command).
//!
//! In the above example the values represent the time it takes for the sound to
//! travel the distance to each of the six towers, respectively. The unit of time for
//! the numbers is "counts" (ticks): one count is one iteration of the sampling loop
//! in UltrasonicSampling::measureEchoTimes(), about 1.125 us with the current build
//! (SAMPLING_LOOP_CYCLES CPU cycles at 16 MHz). The values will definitely have an
//! Ax+B type of offset, where x is the count value, A is the scaling factor from
//! counts to distance (or time) and B is the scaling offset necessary to adjust for
//! counts not starting exactly at zero.
//!
//! These values are output by the Arduino over serial at the baud rate stored in
//! EEPROM (BAUD_RATE, 38400, by default) and thus can be received by another program
//! such as the ground-station program or MATLAB.
//!
//! The base station parameters, their defaults and their EEPROM storage live in
//! settings-eeprom.h; the echo sampling lives in ultrasonic-sampling.h; the serial
//! commands (and SIMULATE_REAL_COMMANDS) live in command_control.h.
//!
//! ----------
//!
//! ChatGPT defines counts as the following:
//!
//! In a timing system, "counts" typically refer to the increments of a digital
//! timer or counter that measures time intervals. These counts represent
//! discrete time steps based on the clock signal driving the system.
//!
//! For example:
//! - If a timer is running on a 1 MHz clock, it increments 1 count per microsecond
//!   (1 us).
//! - If a system counts clock cycles from a 10 MHz oscillator, each count represents
//!   0.1 us.
//!
//! Common Uses of Counts in Timing Systems:
//! - Measuring time intervals – Counting clock cycles to determine how long an event
//!   lasts.
//! - Generating delays – Using a specific number of counts to create precise time
//!   delays.
//! - Frequency measurement – Counting pulses within a known time window to determine
//!   frequency.
//! - Timestamping events – Logging counts to mark when events occur relative to a
//!   reference time.
//!
//! "counts" could be used in time-of-flight (ToF) calculations, where the system
//! counts clock cycles between the sonar pulse transmission and echo reception to
//! determine distance.

#include <Arduino.h>
#include <SPI.h>
#include "radio.h"
#include "debug_macros.h"
#include "led_control.h"
#include "command_control.h"
#include "timers.h"
#include "sysreset.h"
#include "settings-eeprom.h"
#include "ultrasonic-sampling.h"

// Instantiate all classes

//! @brief Arduino pin for the radio's IRQ line. Must be interrupt-capable (2 or 3 on Uno).
//!
//! Currently unused: the base station never listens for packets, so nothing attaches
//! an interrupt to this pin; only the transmitter station uses its radio's IRQ line.
constexpr uint8_t radioIRQPin = 2;
//! nRF24L01+ radio link to the transmitter station; the arguments are the CE and CSN pins.
RadioController radioCtrl(9, 10);
//! @brief Status LEDs; the arguments are the pins of STAT_1, STAT_2, STAT_3,
//!        PULSE_TRIG and RADIO_STATUS.
LedControl leds(3, 4, 6, 7, 8);
//! Settings stored in EEPROM, loaded in setup().
SettingsEeprom settings;
//! Echo sampling; its settings are loaded from EEPROM in setup().
UltrasonicSampling sampler;

//! Serial commands; loop() calls its poll() (and, in continuous mode, runContinuous()).
CommandControl cmdCtrl(radioCtrl, leds, settings, sampler);

//! @brief Arduino set-up, run once after power-up or reset.
//!
//! Disarms the watchdog left running by a software reset, loads the settings from
//! EEPROM (writing the defaults on the first boot), starts the serial port at the
//! stored baud rate, applies the echo-sampling settings, then initializes the LEDs,
//! Timer1 (10 Hz), the trigger pin and the radio. The settings are loaded before
//! Serial.begin() because the baud rate is one of them.
//! @note If the radio fails to initialize, setup() never returns: it prints an error
//!       every 2 s and blinks the RADIO_STATUS LED (1 s on, 1 s off).
void setup()
{
  // Disarm the watchdog left running by a software reset (see sysreset.h)
  systemResetGuard();

  // Load the settings from EEPROM (the first boot writes the defaults)
  const bool settings_loaded = settings.begin();

  // Serial setup, at the baud rate stored in EEPROM
  Serial.begin(settings.getBaudRate());
  while (!Serial && millis() < 3000);
  DEBUG_PRINTLN("Started");
  if (settings_loaded) {
    DEBUG_INFO_PRINTLN("[SETUP] Settings loaded from EEPROM");
  } else {
    DEBUG_INFO_PRINTLN("[SETUP] No valid settings in EEPROM, defaults written");
  }


  // Initialize the LEDs (0: without the startup animation)
  leds.init(0);
  
  // Initialize the timers (Timer1 at 10 Hz)
  timer1_init_10Hz();
  // Echo pins, sampling time (capped by the trigger rate, so after the timer start) and transition cap from EEPROM
  sampler.applySettings(settings);
  
  // Pin setup
  pinMode(triggerPin, OUTPUT);

  // Burst delay in force: the default, or the one stored by a 'D' search
  DEBUG_INFO_PRINTF("[SETUP] Burst delay: %u us\n", settings.getBurstDelayUs());

  // Initialize the radio as the link master (it sends to the transmitter station)
  if (!radioCtrl.init(RADIO_ROLE_BASE)) {
    Serial.println(F("[ERROR] Radio init failed. Halting."));
    while (true) {
      Serial.println(F("[ERROR] Still halted."));
      leds.setRadioStatus(true);
      delay(1000);
      leds.setRadioStatus(false);
      delay(1000);
    }
  }

  // Radio initialized successfully: keep the RADIO_STATUS LED on
  leds.setRadioStatus(true);

}

// ---------------------------------------------------------------------------

//! @brief Send one PLL sync packet carrying the current Timer1 count (TCNT1).
//!
//! Called from loop() to keep the transmitter station's PLL in phase. A PKT_SYNC
//! carries only the Timer1 value and never changes the burst delay stored on the
//! transmitter, so it cannot interfere with a measurement.
//! @note Meant to be called periodically, every sync interval (SYNC_PLL_INTERVAL by
//!       default), but loop() currently calls it on every pass (see loop()).
void syncPLL(){
    radioCtrl.sendSync(TCNT1);
}

// ---------------------------------------------------------------------------

//! @brief Arduino main loop: run any complete serial command, send a PLL sync packet
//!        and, in continuous mode, take the next measurement.
//!
//! @note The sync packets and the continuous measurements are meant to be spaced by
//!       the stored sync interval (SYNC_PLL_INTERVAL by default) and by
//!       continuousPollingDelay, but lastSyncTime and lastPulseTime are local, not
//!       static, so in practice both happen on every pass. This known quirk is left
//!       as it is on purpose (see docs/notes.md).
void loop()
{
    long currentTime = millis();
    // Not static (the known quirk noted above loop()): both are 0 on every pass,
    // so each interval check below is true once millis() reaches its interval.
    long lastSyncTime = 0;
    long lastPulseTime = 0;

    cmdCtrl.poll();

    // Periodic functions (timed with millis(), not delay())
    if (millis() - lastSyncTime >= (unsigned int)settings.getSyncPllInterval()) {
        syncPLL();
        lastSyncTime = currentTime;
    }
    if(cmdCtrl.pollingState == PollingState::CONTINUOUS) {
      if (millis() - lastPulseTime >= continuousPollingDelay) {
          lastPulseTime = millis();
          cmdCtrl.runContinuous();
        }
    }

  }

