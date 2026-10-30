//! @file timers.cpp
//! @brief Timer1 set-up functions, the trigger globals and the TIMER1_COMPA ISR
//!        of the base station.
//!
//! The set-up functions and the globals are documented in timers.h.

#include "timers.h"

// Globals declared in timers.h and documented there (except TRIGGER_PULSE_US).

// Pin for the trigger output
const int triggerPin = 5;
// Flag set by the TIMER1_COMPA ISR when the trigger fires
volatile bool trigger_fired = false;
// Timer1 TOP value (OCR1A), set by the timer1_init_*() functions
uint16_t timer1_top = 0;
//! @brief Trigger pulse duration in microseconds.
//!
//! The HC-SR04 datasheet only says we need 10 us, but I am using 50 us to be
//! safe: https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf
constexpr uint16_t TRIGGER_PULSE_US = 50;
// Burst delay in microseconds, stored by pllCorrect(); the ISR below does not apply it
uint16_t tick_delay = 0;


// The timer1_init_*() functions are documented in timers.h. They differ only in
// the TOP value they write to OCR1A and timer1_top.
// Refer to: https://wolles-elektronikkiste.de/en/timer-and-pwm-part-2-16-bit-timer1
//! @brief Start Timer1 in CTC mode with prescaler 256 and the given TOP.
//!
//! The compare-match interrupt then fires every (TOP + 1) x 16 us. Safe to call
//! again to change the rate on the fly (the count restarts from 0).
//! @param top Value written to OCR1A and timer1_top.
void timer1_init(uint16_t top) {
    const uint8_t sreg = SREG;
    cli();                                  // 16-bit register writes must not be interrupted
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);    // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = top;                           // Counts 0..top, resets on match
    timer1_top = top;
    TCNT1  = 0;
    TIFR1  = (1 << OCF1A);                  // Drop a compare match left pending by the old rate
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
    SREG = sreg;
}

void timer1_init_10Hz() {
    timer1_init(6249);
}

void timer1_init_5Hz() {
    timer1_init(6249*2);
}

void timer1_init_15Hz() {
    timer1_init(4166);
}

void timer1_init_20Hz() {
    timer1_init(3124);
}

//! @brief Periodic Timer1 compare match A interrupt: fires triggerPin.
//!
//! Runs once per Timer1 cycle (10 Hz after timer1_init_10Hz()). Holds
//! triggerPin HIGH for TRIGGER_PULSE_US, then sets trigger_fired.
//! @note The base station fires the trigger at once. Unlike the transmitter
//!       station's copy of this ISR, it does not wait tick_delay first.
ISR(TIMER1_COMPA_vect) {
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(TRIGGER_PULSE_US);
    digitalWrite(triggerPin, LOW);
    trigger_fired = true;
}

