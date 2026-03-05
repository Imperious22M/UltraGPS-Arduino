#include "timers.h"

//! PLL time offst (not used)
volatile uint16_t timer_offset = 0;
//! Pin for the one-shot trigger output
const int triggerPin = 5;
//! Flag set by the TIMER1_COMPA ISR when the trigger fires
volatile bool trigger_fired = false;

//! Initialize timers in normal mode, prescaler 1024, start counting from 0
void timer1_init() {
    TCCR1A = 0;                          // Normal mode (WGM13:0 = 0000)
    TCCR1B = (1 << CS12) | (1 << CS10); // Prescaler 1024; CS12=1, CS11=0, CS10=1
    TCNT1  = 0; // Start counting from 0
}

// Returns TCNT1 (timer count) + timer_offset atomically
uint16_t read_timer1() {
    uint16_t tcnt;
    uint16_t offset;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        tcnt   = TCNT1;
        offset = timer_offset;
    }
    return (uint16_t)(tcnt + offset);
}

// Atomically update timer_offset
void set_timer_offset(uint16_t value) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        timer_offset = value;
    }
}

// Schedule the TIMER1_COMPA interrupt to fire in 'cycles' TCNT1 ticks
// raw_tcnt must be the TCNT1 value captured at scheduling time
void schedule_trigger(uint16_t raw_tcnt, uint16_t cycles) {
    OCR1A  = raw_tcnt + cycles;   // uint16_t wraps naturally at 65536
    TIFR1 |= (1 << OCF1A);       // Clear any stale compare flag (write 1 to clear)
    TIMSK1 |= (1 << OCIE1A);     // Enable compare match interrupt
    trigger_fired = false;      // Clear the trigger fired flag
}

// One-shot compare match interrupt: fires once, disables itself
ISR(TIMER1_COMPA_vect) {
    TIMSK1 &= ~(1 << OCIE1A);  // Disable — one shot only
    //! The digital write calls can be improved
    //! By direct register access
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(1000);
    digitalWrite(triggerPin, LOW);
    trigger_fired = true;
}