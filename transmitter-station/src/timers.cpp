#include "timers.h"

//! Pin for the trigger output
const int triggerPin = 5;
//! Flag set by the TIMER1_COMPA ISR when the trigger fires
volatile bool trigger_fired = false;
//! Number of ticks in one full timer cycle (OCR1A+1)
uint16_t timer1_top = 0;
//! Trigger pulse duration in microseconds
constexpr unsigned int TRIGGER_PULSE_US = 50;
//! Tick delay in microseconds to apply to the trigger timing, as specified by the base station
uint16_t tick_delay = 0;


//! Initialize Timer1 in CTC mode at exactly 10 Hz.
//! 16 MHz / prescaler 256 / (OCR1A+1) = 16000000 / 256 / 6250 = 10 Hz
//! Refer to: https://wolles-elektronikkiste.de/en/timer-and-pwm-part-2-16-bit-timer1
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
//! 5Hz version of the function above
void timer1_init_5Hz() {
    timer1_init(6249*2);
}
//! Initialize Timer1 in CTC mode at approximately 15 Hz.
//! 16 MHz / prescaler 256 / 4167 = 14.999 Hz (not exactly divisible)
void timer1_init_15Hz() {
    timer1_init(4166);
}
//! Initialize Timer1 in CTC mode at exactly 20 Hz.
//! 16 MHz / prescaler 256 / 3125 = 20 Hz
void timer1_init_20Hz() {
    timer1_init(3124);
}

//! Periodic 10 Hz compare match interrupt — fires triggerPin
ISR(TIMER1_COMPA_vect) {
    //! Apply phase delay specified by the base station (if any) before firing the trigger
    //! Max microsecond delay is 16383
    delayMicroseconds(tick_delay);
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(TRIGGER_PULSE_US);
    digitalWrite(triggerPin, LOW);
    trigger_fired = true;
}
