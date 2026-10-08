#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Persistent storage for the ultrasonic burst delay (base station only).
//
// The 'D' command finds the best burst delay and saves it here; setup() loads
// it on boot and uses it instead of the compiled-in SONIC_BURST_DELAY. The 'E'
// command clears the record so the default applies again.
//
// The record lives at DELAY_STORE_EEPROM_ADDR in the ATmega328P's 1 KB EEPROM
// and is guarded by a magic value and an inverted copy of the delay, so a blank
// chip or a corrupted record reads back as "not set" rather than as garbage.
// ---------------------------------------------------------------------------

//! First EEPROM byte used by the record (it spans sizeof(DelayRecord) bytes).
constexpr int DELAY_STORE_EEPROM_ADDR = 0;

//! Smallest and largest delay accepted from EEPROM. 0 is reserved by the radio
//! protocol; 16383 is the delayMicroseconds() cap on the transmitter.
constexpr uint16_t DELAY_STORE_MIN_US = 1;
constexpr uint16_t DELAY_STORE_MAX_US = 16383;

//! Read the stored delay. Returns true and sets out_delay_us if a valid record
//! exists; returns false (out_delay_us untouched) if the store is empty/invalid.
bool delayStoreLoad(uint16_t &out_delay_us);

//! Write delay_us to EEPROM. Returns false if the value is out of range.
//! Only bytes that differ are written, so repeated saves of the same value do
//! not wear the EEPROM.
bool delayStoreSave(uint16_t delay_us);

//! Invalidate the record so delayStoreLoad() returns false.
void delayStoreClear();
