#ifndef SYSRESET_H
#define SYSRESET_H

// ---------------------------------------------------------------
// Software reset for the ATmega328P (Arduino Uno), shared by both
// stations. Uses the watchdog timer so that every peripheral
// (Timer1, SPI, the nRF24 driver state) comes back up from a true
// hardware reset, unlike a bare jump to address 0.
// ---------------------------------------------------------------

#include <Arduino.h>
#include <avr/wdt.h>

//! Call once at the very top of setup().
//! After a watchdog reset the WDT stays armed with the shortest timeout;
//! clearing MCUSR and disabling it here prevents a reset loop on
//! bootloaders that do not do this themselves.
inline void systemResetGuard() {
    MCUSR = 0;
    wdt_disable();
}

//! Reset the board. Does not return.
inline void systemReset() __attribute__((noreturn));
inline void systemReset() {
    cli();
    wdt_enable(WDTO_15MS);
    while (true) {}
}

#endif // SYSRESET_H
