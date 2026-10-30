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
void timer1_init_10Hz() {
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = 6249;                          // Top — counts 0..6249, resets on match
    timer1_top = 6249;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
}

void timer1_init_5Hz() {
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = 6249*2;                        // Top — counts 0..6249*2, resets on match
    timer1_top = 6249*2;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
}

void timer1_init_15Hz() {
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = 4166;                          // Top — counts 0..4166 (4167 counts) ≈ 15 Hz
    timer1_top = 4166;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
}

void timer1_init_20Hz() {
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = 3124;                          // Top — counts 0..3124 (3125 counts) = 20 Hz
    timer1_top = 3124;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
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

