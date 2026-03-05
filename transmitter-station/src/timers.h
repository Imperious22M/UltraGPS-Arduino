// Contains all the helper functions for setting up the timer
// and scheduling the trigger interrupt

#ifndef TIMERS_H
#define TIMERS_H

#include <Arduino.h>
#include <util/atomic.h>

extern volatile uint16_t timer_offset;
extern const int triggerPin;
extern volatile bool trigger_fired;

// Function prototypes
void timer1_init();
uint16_t read_timer1();                 //! Not used  (meant for PLL but not implemented)
void set_timer_offset(uint16_t value);  //! Not used
void schedule_trigger(uint16_t raw_tcnt, uint16_t cycles);

#endif