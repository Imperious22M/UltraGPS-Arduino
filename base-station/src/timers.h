//! @file timers.h
//! @brief Helper functions for setting up Timer1, and the trigger globals shared
//!        with its compare match ISR.
//!
//! Timer1 runs in CTC mode with a prescaler of 256, so one tick is 16 us. Its
//! compare match A interrupt (defined in timers.cpp) pulses triggerPin and sets
//! trigger_fired once per Timer1 cycle.
//!
//! This is the base station's copy. The transmitter station has its own, which
//! has drifted (see docs/notes.md); notably, only the transmitter's ISR delays
//! the trigger by tick_delay.

#ifndef TIMERS_H
#define TIMERS_H

#include <Arduino.h>
#include <avr/io.h>

//! @brief Pin for the trigger output, pulsed HIGH for TRIGGER_PULSE_US by the
//!        TIMER1_COMPA ISR.
//!
//! The timer1_init_*() functions do not call pinMode(); setup() must make it
//! an OUTPUT.
extern const int triggerPin;
//! Flag set by the TIMER1_COMPA ISR when the trigger fires; cleared by the code that waits for it.
extern volatile bool trigger_fired;
//! Set by the TIMER1_COMPA ISR when TCNT1 restarts (once per Timer1 cycle);
//! loop() sends a PLL sync packet and clears it.
extern volatile bool sync_due;
//! @brief Timer1 TOP value (OCR1A), set by the timer1_init_*() functions.
//!
//! TCNT1 counts 0..timer1_top, so one full timer cycle is timer1_top + 1 ticks.
//! It is 0 until a timer1_init_*() function has run.
extern uint16_t timer1_top; 
//! @brief Burst delay in microseconds, as last passed to pllCorrect().
//! @note The base station's ISR does not apply it; only the transmitter
//!       station's copy of the ISR waits tick_delay before firing the trigger.
extern uint16_t tick_delay; 

//! @name Timer1 set-up
//! Each function puts Timer1 in CTC mode with a prescaler of 256, sets OCR1A and
//! timer1_top for its rate, clears TCNT1 and enables the compare match A
//! interrupt (TIMSK1 is overwritten, so Timer1's other interrupts are disabled).
//! Call one of them from setup().
//! @{

//! @brief Start Timer1 (CTC, prescaler 256) with the given TOP; see timers.cpp.
//!
//! Used for the trigger rate set with the 'F' command (triggerRateToTop()).
//! @param top Value for OCR1A; the period is (top + 1) x 16 us.
void timer1_init(uint16_t top);

//! @brief Initialize Timer1 in CTC mode at exactly 10 Hz.
//!
//! 16 MHz / prescaler 256 / (OCR1A + 1) = 16000000 / 256 / 6250 = 10 Hz.
void timer1_init_10Hz();
//! @brief Initialize Timer1 in CTC mode at about 5 Hz: the 10 Hz set-up with
//!        twice the OCR1A value.
//!
//! 16 MHz / prescaler 256 / (6249 * 2 + 1) = 16000000 / 256 / 12499 ≈ 5.0004 Hz.
void timer1_init_5Hz();
//! @brief Initialize Timer1 in CTC mode at about 15 Hz.
//!
//! 16 MHz / prescaler 256 / 4167 ≈ 14.999 Hz (not exactly divisible).
void timer1_init_15Hz();
//! @brief Initialize Timer1 in CTC mode at exactly 20 Hz.
//!
//! 16 MHz / prescaler 256 / 3125 = 20 Hz.
void timer1_init_20Hz();

//! @}

//! @brief PLL correction: adjust TCNT1 by the given signed error (ticks) and
//!        store the burst delay in tick_delay.
//!
//! Inline for minimal latency: the TCNT1 update compiles to a read-add-write.
//! @param error         Signed phase error in Timer1 ticks, added to TCNT1.
//! @param tick_delay_in Burst delay in microseconds, stored in tick_delay.
inline void pllCorrect(int16_t error, uint16_t tick_delay_in) {
    TCNT1 += error;
    tick_delay = tick_delay_in;
}
//! @brief Reset the trigger_fired flag to signal that the most recent value has
//!        been read.
inline void resetTriggerFlag() {
    trigger_fired = false;
}

#endif
