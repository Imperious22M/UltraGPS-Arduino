// Contains all the helper functions for setting up the timer

#ifndef TIMERS_H
#define TIMERS_H

#include <Arduino.h>
#include <avr/io.h>

extern const int triggerPin;
extern volatile bool trigger_fired;
extern uint16_t timer1_top; 
extern uint16_t tick_delay; 

// Function prototypes
void timer1_init_10Hz();
void timer1_init_5Hz();
void timer1_init_15Hz();
void timer1_init_20Hz();

//! PLL correction: adjust TCNT1 by the given signed error (ticks).
//! Inline for minimal latency — compiles to a read-add-write on TCNT1.
inline void pllCorrect(int16_t error) {
    TCNT1 += error;
}

#endif
