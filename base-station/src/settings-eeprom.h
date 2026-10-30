//! @file settings-eeprom.h
//! @brief Base-station settings kept in the ATmega328P's built-in EEPROM.
//!
//! This header holds the **default** value of every setting, the range each one
//! must fall in, and the SettingsEeprom class that stores them.
//!
//! How the settings move between EEPROM and RAM:
//! - On the first boot (blank EEPROM, or a stored record that was written by
//!   other firmware, has another layout version or size, fails its CRC or holds
//!   an out-of-range value) SettingsEeprom::begin() writes the defaults below to
//!   EEPROM.
//! - On every boot SettingsEeprom::begin(), called from setup(), loads the
//!   EEPROM record into a RAM copy.
//! - A setter checks the new value, then writes it to EEPROM and updates the RAM
//!   copy at the same time. Setters are only called by serial commands.
//! - A getter returns the RAM copy. Because the RAM copy is loaded from EEPROM in
//!   setup() and every setter keeps the two in step, a getter always returns the
//!   value stored in EEPROM without touching the EEPROM.
//! - The 'M' command (SettingsEeprom::resetToDefaults()) writes the defaults back.
//!
//! @note EEPROM survives reflashing. After the first boot, editing a default in
//!       this file has no effect on a board until 'M' is sent or
//!       SETTINGS_LAYOUT_VERSION is changed.

#pragma once
#include <Arduino.h>
#include "radio.h"   // TRIGGER_RATE_HZ / _MIN_HZ / _MAX_HZ, shared with the transmitter station

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

//! @name Base station parameters
//! @{

//! @brief How long to hold the high state of the trigger pin (microseconds).
//! @note Not used yet: the trigger pulse width is TRIGGER_PULSE_US in timers.cpp.
constexpr unsigned int TRIGGER_HIGH_DURATION = 300;

//! List of all the receivers' ultrasonic echo pins (each one of A0-A5).
constexpr uint8_t ECHO_PIN[]={A0, A1, A2, A3, A4, A5};

//! Number of receivers being used. Read-only: it is the length of ECHO_PIN.
constexpr uint8_t RECV_CNT = sizeof(ECHO_PIN)/sizeof(ECHO_PIN[0]);

//! @}

//! @name Serial communication parameters
//! @{

//! @brief Serial baud rate.
//!
//! Must be a rate baudRateIsValid() accepts (checked at compile time). 32-bit
//! because rates above 65535 (115200 and up) do not fit in a 16-bit unsigned int
//! on the AVR.
constexpr uint32_t BAUD_RATE  = 38400;

//! @}

//! @name PLL synchronization parameters
//! @{

//! @brief How often to send a sync packet to the transmitter station to
//!        synchronize its PLL (milliseconds).
//! @note Almost no effect at present: loop() sends a sync packet on every pass
//!       once millis() reaches the interval (a known quirk, see loop() in main.cpp).
constexpr unsigned long SYNC_PLL_INTERVAL = 10;

//! @}

//! @brief Default burst delay sent to the transmitter station with every
//!        measurement trigger (microseconds).
//!
//! Experimentally the ultrasonic receivers need a few ms to "send" their own
//! burst and go into receive mode, so the transmitter's burst is delayed to make
//! sure the receivers are listening by the time it arrives.
//! This is the compiled-in default. The 'D' command searches for a better value,
//! stores it in EEPROM and applies it; the 'E' command stores this default again.
//! @see SettingsEeprom::getBurstDelayUs(), the burst delay in force.
constexpr uint16_t SONIC_BURST_DELAY = 2*1000; // 2 ms delay

//! @brief Do not send a trigger packet in the last part of the Timer1 cycle,
//!        given in Timer1 ticks before the compare match.
//!
//! The packet would race the compare match and the transmitter could fire with
//! the previous trigger's delay, so CommandControl::handleUltrasonics() first
//! waits for that trigger to pass. 125 ticks = 2 ms at 16 us/tick.
constexpr uint16_t TRIGGER_SEND_GUARD_TICKS = 125;

//! @name Delay search ('D' command)
//! Sweep the burst delay from the first to the last delay in steps, and store
//! the one with the most valid readings, then the lowest mean tick count across
//! the receivers.
//! @{

constexpr uint16_t SEARCH_DELAY_MIN_US     = 0;    //!< First delay tried (microseconds).
constexpr uint16_t SEARCH_DELAY_MAX_US     = 500;  //!< Last delay tried (microseconds, inclusive).
constexpr uint16_t SEARCH_DELAY_STEP_US    = 10;   //!< Step between tries (microseconds).
constexpr uint8_t  SEARCH_SAMPLES_PER_STEP = 3;    //!< Pulses averaged at each delay.
constexpr bool     SEARCH_PRINT_STEPS      = true; //!< Also print one "D: ..." line per step.

//! @}

//! @name Ultrasonic sampling (UltrasonicSampling class, 'H' command)
//! @{

//! @brief How long the echo lines are sampled after each trigger (milliseconds).
//!
//! UltrasonicSampling converts it to a number of sampling-loop iterations (ticks).
//! 80 ms keeps the window the firmware used before this setting existed: the
//! TCNT1 timeout (now commented out) ended sampling at 80 % of the 100 ms
//! Timer1 period, long before the old 0x31000/4 iteration limit was reached.
//! @warning Keep it below the trigger period (100 ms at 10 Hz, 50 ms at 20 Hz).
//!          A longer window runs into the next trigger pulse, which re-arms the
//!          receivers mid-measurement, so the reported ticks come from a later
//!          cycle or are 0.
constexpr uint16_t SAMPLING_TIME_MS = 80;

//! @brief Capacity of the echo-transition buffers.
//!
//! Compile-time only (each entry costs 5 bytes of RAM); it is the largest
//! MAX_SAVED_TICKS accepted.
constexpr uint8_t  SAMPLE_BUFFER_SIZE = 100;

//! @brief How many echo-line transitions (tick values) are saved per measurement.
//!
//! Sampling stops early once this many have been saved. Every receiver's echo
//! line rises and then falls, so a value below 2 * RECV_CNT can leave some
//! receivers reading 0.
constexpr uint8_t  MAX_SAVED_TICKS = SAMPLE_BUFFER_SIZE;

//! @}

//! @name Reset command ('R') behaviour
//! @{

//! @brief If true, after sending the reset packet to the transmitter station the
//!        base station also resets itself, so both boards start from a clean state.
//!
//! Set to false to only reset the transmitter station.
constexpr bool RESET_SELF_AFTER_REMOTE_RESET = false;

//! @brief Milliseconds to wait after the reset packet before resetting ourselves,
//!        so the acknowledgement line has left the UART and the remote board has
//!        rebooted.
constexpr uint16_t RESET_SELF_DELAY_MS = 100;

//! @}

// ---------------------------------------------------------------------------
// Accepted ranges. A setter rejects a value outside its range, and a stored
// record holding one is treated as invalid.
// ---------------------------------------------------------------------------

//! @name Accepted ranges
//! @{

//! Shortest trigger high time (microseconds).
constexpr unsigned int TRIGGER_HIGH_DURATION_MIN = 1;
//! Longest trigger high time (microseconds): the delayMicroseconds() limit.
constexpr unsigned int TRIGGER_HIGH_DURATION_MAX = 16383;
//! Shortest sync interval (milliseconds).
constexpr unsigned long SYNC_PLL_INTERVAL_MIN = 1;
//! Longest sync interval (milliseconds): one minute.
constexpr unsigned long SYNC_PLL_INTERVAL_MAX = 60000;
//! Smallest burst delay (microseconds); 0 is reserved by the radio protocol.
constexpr uint16_t BURST_DELAY_MIN_US = 1;
//! Largest burst delay (microseconds): the delayMicroseconds() limit on the transmitter.
constexpr uint16_t BURST_DELAY_MAX_US = 16383;

//! @brief Largest send guard (Timer1 ticks): timer1_top at 20 Hz, the fastest
//!        timer1_init_*() rate (about one Timer1 period at that rate).
constexpr uint16_t TRIGGER_SEND_GUARD_MAX_TICKS = 3124;

//! Shortest sampling time accepted by 'H' (milliseconds).
constexpr uint16_t SAMPLING_TIME_MIN_MS = 1;
//! Longest sampling time accepted by 'H' (milliseconds): one second.
constexpr uint16_t SAMPLING_TIME_MAX_MS = 1000;
//! Fewest transitions that may be saved per measurement.
constexpr uint8_t  MAX_SAVED_TICKS_MIN = 1;

//! @}

//! @brief First EEPROM byte used by the settings record.
//!
//! The record (header, settings, CRC) is a few dozen bytes of the 1 KB EEPROM.
constexpr int SETTINGS_EEPROM_ADDR = 0;

//! @brief Version of the stored record layout.
//!
//! Change it whenever the stored fields change; a board holding a record with a
//! different version rewrites it with the defaults on its next boot.
constexpr uint8_t SETTINGS_LAYOUT_VERSION = 2;  // 2: trigger rate added

//! @brief Check whether @p baud is a baud rate SettingsEeprom::setBaudRate() accepts.
//!
//! The accepted rates are the standard ones the 16 MHz Uno generates within
//! 2.5 %: 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600,
//! 115200, 250000, 500000 and 1000000 (230400 is left out, it is 3.5 % off).
//! Written as one expression, not a table, so it works in a static_assert and
//! costs no RAM at run time.
//! @param baud Baud rate to check.
//! @return true if @p baud is one of the rates above.
constexpr bool baudRateIsValid(uint32_t baud) {
    return baud == 300UL    || baud == 600UL    || baud == 1200UL   || baud == 2400UL
        || baud == 4800UL   || baud == 9600UL   || baud == 14400UL  || baud == 19200UL
        || baud == 28800UL  || baud == 38400UL  || baud == 57600UL  || baud == 115200UL
        || baud == 250000UL || baud == 500000UL || baud == 1000000UL;
}

//! @brief Check whether @p pin can carry an echo line.
//!
//! The echo lines are sampled as one read of the PORTC input register, so only
//! A0-A5 (PC0-PC5) can be used.
//! @param pin Arduino pin number to check.
//! @return true if @p pin is one of A0 to A5.
constexpr bool echoPinIsValid(uint8_t pin) {
    return pin >= PIN_A0 && pin <= PIN_A5;
}

//! @brief Check that every ECHO_PIN entry from @p index on is a valid echo pin.
//!
//! Recursive so it can be used in a static_assert (C++11 constexpr rules);
//! static because it reads ECHO_PIN, which has a copy in every source file.
//! @param index First ECHO_PIN entry to check.
//! @return true if all of them pass echoPinIsValid() (also when @p index is
//!         past the last entry).
static constexpr bool echoPinsAreValid(uint8_t index = 0) {
    return index >= RECV_CNT || (echoPinIsValid(ECHO_PIN[index]) && echoPinsAreValid(index + 1));
}

// The defaults must be valid, otherwise every boot would find the stored record
// invalid and rewrite it.
static_assert(TRIGGER_HIGH_DURATION >= TRIGGER_HIGH_DURATION_MIN && TRIGGER_HIGH_DURATION <= TRIGGER_HIGH_DURATION_MAX,
              "TRIGGER_HIGH_DURATION is out of range");
static_assert(RECV_CNT >= 1 && RECV_CNT <= 6, "the echo lines are sampled on PORTC, which has six usable pins (A0-A5)");
static_assert(echoPinsAreValid(), "every ECHO_PIN entry must be one of A0-A5");
static_assert(baudRateIsValid(BAUD_RATE), "BAUD_RATE must be a rate baudRateIsValid() accepts");
static_assert(SYNC_PLL_INTERVAL >= SYNC_PLL_INTERVAL_MIN && SYNC_PLL_INTERVAL <= SYNC_PLL_INTERVAL_MAX,
              "SYNC_PLL_INTERVAL is out of range");
static_assert(SONIC_BURST_DELAY > 0, "a zero tick delay is reserved and ignored by the transmitter station");
static_assert(SONIC_BURST_DELAY >= BURST_DELAY_MIN_US && SONIC_BURST_DELAY <= BURST_DELAY_MAX_US,
              "SONIC_BURST_DELAY is out of range");
static_assert(TRIGGER_SEND_GUARD_TICKS <= TRIGGER_SEND_GUARD_MAX_TICKS, "TRIGGER_SEND_GUARD_TICKS is out of range");
static_assert(SEARCH_DELAY_MIN_US <= BURST_DELAY_MAX_US && SEARCH_DELAY_MAX_US <= BURST_DELAY_MAX_US,
              "the delay search range must stay within BURST_DELAY_MAX_US");
static_assert(SEARCH_DELAY_STEP_US >= 1 && SEARCH_DELAY_STEP_US <= BURST_DELAY_MAX_US,
              "SEARCH_DELAY_STEP_US must be at least 1");
static_assert(SEARCH_SAMPLES_PER_STEP >= 1, "SEARCH_SAMPLES_PER_STEP must be at least 1");
static_assert(SAMPLING_TIME_MS >= SAMPLING_TIME_MIN_MS && SAMPLING_TIME_MS <= SAMPLING_TIME_MAX_MS,
              "SAMPLING_TIME_MS is out of range");
static_assert(MAX_SAVED_TICKS >= MAX_SAVED_TICKS_MIN && MAX_SAVED_TICKS <= SAMPLE_BUFFER_SIZE,
              "MAX_SAVED_TICKS must fit in the SAMPLE_BUFFER_SIZE buffers");

// ---------------------------------------------------------------------------
// SettingsEeprom class
// ---------------------------------------------------------------------------

//! @brief Stores the base-station settings in EEPROM and keeps a RAM copy of them.
//!
//! See the file description for how values move between EEPROM and RAM. Every
//! setter returns false, and changes nothing, if the value is out of range.
class SettingsEeprom {
public:
    //! @brief Fill the RAM copy with the defaults. Does not touch the EEPROM.
    SettingsEeprom();

    //! @brief Load the settings from EEPROM into RAM.
    //!
    //! Call once from setup(), before Serial.begin(): the baud rate is one of
    //! the settings. If there is no valid record (blank EEPROM, or a record that
    //! was written by other firmware, has another layout version or size, fails
    //! its CRC or holds an out-of-range value), the defaults are written to
    //! EEPROM and used instead.
    //! @return true if a valid stored record was loaded, false if the defaults
    //!         were written.
    bool begin();

    //! @brief Write every default to EEPROM and RAM (the 'M' command).
    //!
    //! If the stored baud rate was not BAUD_RATE, the serial port switches to
    //! BAUD_RATE immediately; anything already queued for sending goes out at
    //! the old rate first.
    void resetToDefaults();

    //! @name Getters
    //! Return the RAM copy, which always equals the value stored in EEPROM.
    //! @{

    //! @brief Get the trigger high time.
    //! @return The trigger high time in microseconds.
    //! @see TRIGGER_HIGH_DURATION
    unsigned int  getTriggerHighDuration() const;

    //! @brief Get the echo pin of one receiver.
    //! @param receiver Receiver index, 0 to RECV_CNT - 1.
    //! @return The pin, or 0xFF if @p receiver is out of range.
    //! @see ECHO_PIN
    uint8_t       getEchoPin(uint8_t receiver) const;

    //! @brief Get the number of receivers. Read-only: there is no setter.
    //! @return RECV_CNT.
    uint8_t       getRecvCnt() const;

    //! @brief Get the serial baud rate.
    //! @return The baud rate.
    //! @see BAUD_RATE
    uint32_t      getBaudRate() const;

    //! @brief Get the interval between PLL sync packets.
    //! @return The interval in milliseconds.
    //! @see SYNC_PLL_INTERVAL
    unsigned long getSyncPllInterval() const;

    //! @brief Get the burst delay in force.
    //! @return The delay in microseconds: the one stored by 'D', or SONIC_BURST_DELAY.
    //! @see SONIC_BURST_DELAY
    uint16_t      getBurstDelayUs() const;

    //! @brief Get the trigger send guard.
    //! @return The guard in Timer1 ticks.
    //! @see TRIGGER_SEND_GUARD_TICKS
    uint16_t      getTriggerSendGuardTicks() const;

    //! @brief Get the first delay the 'D' search tries.
    //! @return The delay in microseconds.
    //! @see SEARCH_DELAY_MIN_US
    uint16_t      getSearchDelayMinUs() const;

    //! @brief Get the last delay the 'D' search tries (inclusive).
    //! @return The delay in microseconds.
    //! @see SEARCH_DELAY_MAX_US
    uint16_t      getSearchDelayMaxUs() const;

    //! @brief Get the step between the delays the 'D' search tries.
    //! @return The step in microseconds.
    //! @see SEARCH_DELAY_STEP_US
    uint16_t      getSearchDelayStepUs() const;

    //! @brief Get how many pulses the 'D' search averages at each delay.
    //! @return The number of pulses per step.
    //! @see SEARCH_SAMPLES_PER_STEP
    uint8_t       getSearchSamplesPerStep() const;

    //! @brief Get whether the 'D' search prints one "D: ..." line per step.
    //! @return true if it prints a line per step.
    //! @see SEARCH_PRINT_STEPS
    bool          getSearchPrintSteps() const;

    //! @brief Get whether 'R' also resets the base station.
    //! @return true if 'R' resets both boards, false if only the transmitter station.
    //! @see RESET_SELF_AFTER_REMOTE_RESET
    bool          getResetSelfAfterRemoteReset() const;

    //! @brief Get how long 'R' waits before resetting the base station.
    //! @return The wait in milliseconds.
    //! @see RESET_SELF_DELAY_MS
    uint16_t      getResetSelfDelayMs() const;

    //! @brief Get the echo sampling time.
    //! @return The sampling time in milliseconds.
    //! @see SAMPLING_TIME_MS
    uint16_t      getSamplingTimeMs() const;

    //! @brief Get how many echo-line transitions are saved per measurement.
    //! @return The transition limit.
    //! @see MAX_SAVED_TICKS
    uint8_t       getMaxSavedTicks() const;
    //! @brief Get the trigger rate (Timer1 frequency) in Hz.
    //! @return TRIGGER_RATE_MIN_HZ to TRIGGER_RATE_MAX_HZ. @see TRIGGER_RATE_HZ
    uint8_t       getTriggerRateHz() const;

    //! @}

    //! @name Setters
    //! Check the value, then write it to EEPROM and update the RAM copy.
    //! There is no setter for RECV_CNT, which is read-only.
    //! @{

    //! @brief Set the trigger high time (microseconds).
    //! @param us TRIGGER_HIGH_DURATION_MIN to TRIGGER_HIGH_DURATION_MAX.
    //! @return true if stored; false, and nothing changes, if @p us is out of range.
    //! @note Stored only: no code uses this setting yet (the trigger pulse width
    //!       is TRIGGER_PULSE_US in timers.cpp).
    bool setTriggerHighDuration(unsigned int us);

    //! @brief Set the echo pin of one receiver.
    //! @param receiver Receiver index, 0 to RECV_CNT - 1.
    //! @param pin      A0 to A5. Two receivers may share a pin; they then report
    //!                 the same tick count.
    //! @return true if stored; false, and nothing changes, if either argument
    //!         is out of range.
    //! @note UltrasonicSampling keeps its own copy; call
    //!       UltrasonicSampling::applySettings() afterwards.
    bool setEchoPin(uint8_t receiver, uint8_t pin);

    //! @brief Set the baud rate and switch the serial port to it immediately.
    //!
    //! Anything already queued for sending goes out at the old rate first. The
    //! port is only restarted if @p baud differs from the stored rate.
    //! @param baud A rate baudRateIsValid() accepts.
    //! @return true if stored; false, and nothing changes, if @p baud is not
    //!         accepted.
    //! @warning The board also comes up at the stored rate after every reset.
    //!          If the host does not use that rate, connect at it and send 'M',
    //!          or flash firmware with another SETTINGS_LAYOUT_VERSION.
    bool setBaudRate(uint32_t baud);

    //! @brief Set the interval between PLL sync packets (milliseconds).
    //! @param ms SYNC_PLL_INTERVAL_MIN to SYNC_PLL_INTERVAL_MAX.
    //! @return true if stored; false, and nothing changes, if @p ms is out of range.
    //! @note Almost no effect at present: loop() sends a sync packet on every pass
    //!       once millis() reaches the interval (a known quirk, see loop() in
    //!       main.cpp).
    bool setSyncPllInterval(unsigned long ms);

    //! @brief Set the burst delay in force (microseconds).
    //!
    //! The 'D' command uses it to store the delay it finds, and 'E' to store
    //! SONIC_BURST_DELAY.
    //! @param us BURST_DELAY_MIN_US to BURST_DELAY_MAX_US.
    //! @return true if stored; false, and nothing changes, if @p us is out of range.
    bool setBurstDelayUs(uint16_t us);

    //! @brief Set the trigger send guard (Timer1 ticks).
    //! @param ticks 0 to TRIGGER_SEND_GUARD_MAX_TICKS.
    //! @return true if stored; false, and nothing changes, if @p ticks is out of
    //!         range.
    //! @see TRIGGER_SEND_GUARD_TICKS
    bool setTriggerSendGuardTicks(uint16_t ticks);

    //! @brief Set the first delay the 'D' search tries (microseconds).
    //! @param us 0 to BURST_DELAY_MAX_US.
    //! @return true if stored; false, and nothing changes, if @p us is out of range.
    //! @note Not checked against the last delay: if it is larger, 'D' tries no
    //!       delay and stores nothing.
    bool setSearchDelayMinUs(uint16_t us);

    //! @brief Set the last delay the 'D' search tries (microseconds, inclusive).
    //! @param us 0 to BURST_DELAY_MAX_US.
    //! @return true if stored; false, and nothing changes, if @p us is out of range.
    //! @note Not checked against the first delay (see setSearchDelayMinUs()).
    bool setSearchDelayMaxUs(uint16_t us);

    //! @brief Set the step between the delays the 'D' search tries (microseconds).
    //! @param us 1 to BURST_DELAY_MAX_US.
    //! @return true if stored; false, and nothing changes, if @p us is out of range.
    bool setSearchDelayStepUs(uint16_t us);

    //! @brief Set how many pulses the 'D' search averages at each delay.
    //! @param samples At least 1.
    //! @return true if stored; false, and nothing changes, if @p samples is 0.
    bool setSearchSamplesPerStep(uint8_t samples);

    //! @brief Set whether the 'D' search prints one "D: ..." line per step.
    //! @param print true to print a line per step.
    //! @return Always true: any value is accepted.
    bool setSearchPrintSteps(bool print);

    //! @brief Set whether 'R' also resets the base station.
    //! @param reset true to reset both boards, false to reset only the
    //!              transmitter station.
    //! @return Always true: any value is accepted.
    bool setResetSelfAfterRemoteReset(bool reset);

    //! @brief Set how long 'R' waits before resetting the base station (milliseconds).
    //! @param ms Any value.
    //! @return Always true: any value is accepted.
    bool setResetSelfDelayMs(uint16_t ms);

    //! @brief Set the echo sampling time (milliseconds); the 'H' command uses it.
    //! @param ms SAMPLING_TIME_MIN_MS to SAMPLING_TIME_MAX_MS.
    //! @return true if stored; false, and nothing changes, if @p ms is out of range.
    //! @note UltrasonicSampling keeps its own copy; call
    //!       UltrasonicSampling::applySettings() afterwards.
    bool setSamplingTimeMs(uint16_t ms);

    //! @brief Set how many echo-line transitions are saved per measurement.
    //! @param count MAX_SAVED_TICKS_MIN to SAMPLE_BUFFER_SIZE.
    //! @return true if stored; false, and nothing changes, if @p count is out of
    //!         range.
    //! @note UltrasonicSampling keeps its own copy; call
    //!       UltrasonicSampling::applySettings() afterwards.
    bool setMaxSavedTicks(uint8_t count);
    //! @brief Set the trigger rate (the 'F' command). Range TRIGGER_RATE_MIN_HZ to
    //!        TRIGGER_RATE_MAX_HZ.
    //! @note Only stores it: the caller restarts Timer1 and tells the transmitter.
    //! @param hz Trigger rate in Hz.
    //! @return true if stored; false, and nothing changes, if out of range.
    bool setTriggerRateHz(uint8_t hz);

    //! @}

    //! @brief Check whether @p baud is an accepted baud rate (calls baudRateIsValid()).
    //! @param baud Baud rate to check.
    //! @return true if @p baud is accepted.
    static bool isValidBaudRate(uint32_t baud);

private:
    //! @brief Start of the stored record; identifies it and its layout.
    //!
    //! The record is laid out as Header, then Data, then the CRC-16 of Data.
    struct Header {
        uint16_t magic;    //!< Marks a record written by this firmware (SETTINGS_MAGIC).
        uint8_t  version;  //!< Layout version (SETTINGS_LAYOUT_VERSION).
        uint8_t  size;     //!< Size of the settings, sizeof(Data).
    };

    //! @brief The settings as stored in EEPROM.
    //!
    //! Fixed-width fields and no padding (the AVR has none), so the CRC and the
    //! layout do not depend on the compiler. Booleans are stored as 0 or 1.
    struct Data {
        uint16_t triggerHighDuration;        //!< Default: TRIGGER_HIGH_DURATION.
        uint8_t  echoPin[RECV_CNT];          //!< Default: ECHO_PIN.
        uint8_t  recvCnt;                    //!< Always RECV_CNT; checked on load.
        uint32_t baudRate;                   //!< Default: BAUD_RATE.
        uint32_t syncPllInterval;            //!< Default: SYNC_PLL_INTERVAL.
        uint16_t burstDelayUs;               //!< Burst delay in force. Default: SONIC_BURST_DELAY.
        uint16_t triggerSendGuardTicks;      //!< Default: TRIGGER_SEND_GUARD_TICKS.
        uint16_t searchDelayMinUs;           //!< Default: SEARCH_DELAY_MIN_US.
        uint16_t searchDelayMaxUs;           //!< Default: SEARCH_DELAY_MAX_US.
        uint16_t searchDelayStepUs;          //!< Default: SEARCH_DELAY_STEP_US.
        uint8_t  searchSamplesPerStep;       //!< Default: SEARCH_SAMPLES_PER_STEP.
        uint8_t  searchPrintSteps;           //!< Default: SEARCH_PRINT_STEPS.
        uint8_t  resetSelfAfterRemoteReset;  //!< Default: RESET_SELF_AFTER_REMOTE_RESET.
        uint16_t resetSelfDelayMs;           //!< Default: RESET_SELF_DELAY_MS.
        uint16_t samplingTimeMs;             //!< Default: SAMPLING_TIME_MS.
        uint8_t  maxSavedTicks;              //!< Default: MAX_SAVED_TICKS.
        uint8_t  triggerRateHz;              //!< Default: TRIGGER_RATE_HZ.
    };

    static_assert(sizeof(Data) <= 255, "the record header stores the settings size in one byte");

    //! EEPROM address of the settings.
    static constexpr int DATA_ADDR = SETTINGS_EEPROM_ADDR + sizeof(Header);
    //! EEPROM address of their CRC.
    static constexpr int CRC_ADDR  = DATA_ADDR + sizeof(Data);

    Data _data;  //!< RAM copy of the stored settings.

    //! @brief Fill @p data with the defaults.
    //! @param data Record to fill.
    static void loadDefaults(Data &data);

    //! @brief Check that every field of @p data is in range.
    //! @param data Record to check.
    //! @return true if every field is in range and the receiver count is RECV_CNT.
    static bool isValid(const Data &data);

    //! @brief Compute the CRC-16 of @p data, stored after it to detect corruption.
    //! @param data Record to checksum.
    //! @return The CRC (avr-libc's _crc16_update(), starting from 0xFFFF).
    static uint16_t crc(const Data &data);

    //! @brief Write the header, the RAM copy and its CRC to EEPROM.
    //!
    //! Only bytes that differ are written, so storing an unchanged value does
    //! not wear the EEPROM.
    void save() const;

    //! @brief Store @p value in @p field (RAM and EEPROM) if @p valid.
    //! @tparam T    Type of the field.
    //! @param field Field of _data to set.
    //! @param value New value.
    //! @param valid Result of the caller's range check.
    //! @return @p valid.
    template <typename T>
    bool store(T &field, T value, bool valid);

    //! @brief Switch the serial port to @p baud, after sending what is queued.
    //! @param baud New baud rate.
    static void applyBaudRate(uint32_t baud);
};
