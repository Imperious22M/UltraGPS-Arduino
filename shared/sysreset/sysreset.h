//! @file sysreset.h
//! @brief Software reset for the ATmega328P (Arduino Uno), shared by both stations.
//!
//! Uses the watchdog timer so that every peripheral (Timer1, SPI, the nRF24 driver state)
//! comes back up from a true hardware reset, unlike a bare jump to address 0.

#ifndef SYSRESET_H
#define SYSRESET_H

#include <Arduino.h>
#include <avr/wdt.h>

//! @brief Call once at the very top of setup() to disarm the watchdog after a software reset.
//!
//! After a watchdog reset the WDT stays armed with the shortest timeout; clearing MCUSR and
//! disabling it here prevents a reset loop on bootloaders that do not do this themselves.
inline void systemResetGuard() {
    MCUSR = 0;
    wdt_disable();
}

//! @brief Reset the board. Does not return.
//!
//! Disables interrupts, arms the watchdog with WDTO_15MS and spins until it fires.
//! @see systemResetGuard(), which disarms the watchdog again after the reset.
inline void systemReset() __attribute__((noreturn));
inline void systemReset() {
    cli();
    wdt_enable(WDTO_15MS);
    while (true) {}
}

#endif // SYSRESET_H
