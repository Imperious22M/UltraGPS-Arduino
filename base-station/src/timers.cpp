#include "timers.h"

//! Pin for the trigger output
const int triggerPin = 5;
//! Flag set by the TIMER1_COMPA ISR when the trigger fires
volatile bool trigger_fired = false;
//! Number of ticks in one full timer cycle (OCR1A+1)
uint16_t timer1_top = 0;
//! Trigger pulse duration in microseconds
//! Datasheet only says we need 10uS, but I am using 50uS to be safe
//! https://cdn.sparkfun.com/datasheets/Sensors/Proximity/HCSR04.pdf
constexpr uint16_t TRIGGER_PULSE_US = 50;
//! Tick delay in microseconds to apply to the trigger timing, as specified by the base station
uint16_t tick_delay = 0;


//! Initialize Timer1 in CTC mode at exactly 10 Hz.
//! 16 MHz / prescaler 256 / (OCR1A+1) = 16000000 / 256 / 6250 = 10 Hz
//! Refer to: https://wolles-elektronikkiste.de/en/timer-and-pwm-part-2-16-bit-timer1
void timer1_init_10Hz() {
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = 6249;                          // Top — counts 0..6249, resets on match
    timer1_top = 6249;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
}
//! 5Hz version of the function above
void timer1_init_5Hz() {
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = 6249*2;                          // Top — counts 0..6249*2, resets on match
    timer1_top = 6249*2;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
}
//! Initialize Timer1 in CTC mode at approximately 15 Hz.
//! 16 MHz / prescaler 256 / 4167 = 14.999 Hz (not exactly divisible)
void timer1_init_15Hz() {
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = 4166;                          // Top — counts 0..4166 (4167 counts) ≈ 15 Hz
    timer1_top = 4166;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
}
//! Initialize Timer1 in CTC mode at exactly 20 Hz.
//! 16 MHz / prescaler 256 / 3125 = 20 Hz
void timer1_init_20Hz() {
    TCCR1A = 0;                             // CTC mode: WGM10=0, WGM11=0
    TCCR1B = (1 << WGM12) | (1 << CS12);   // CTC: WGM12=1; prescaler 256: CS12=1
    OCR1A  = 3124;                          // Top — counts 0..3124 (3125 counts) = 20 Hz
    timer1_top = 3124;
    TCNT1  = 0;
    TIMSK1 = (1 << OCIE1A);                 // Enable compare match A interrupt
}

//! Periodic 10 Hz compare match interrupt — fires triggerPin
ISR(TIMER1_COMPA_vect) {
    //! Apply phase delay specified by the base station (if any) before firing the trigger
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(TRIGGER_PULSE_US);
    digitalWrite(triggerPin, LOW);
    trigger_fired = true;
}

