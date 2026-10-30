//! @file ultrasonic-sampling.h
//! @brief Samples the receivers' echo lines and reduces them to one tick count
//!        per receiver.
//!
//! A **tick** is one Timer1 count (16 us at prescaler 256): each echo-line change
//! is timestamped with the TCNT1 counts elapsed since sampling started, so the
//! reported values are times in 16 us units, independent of the loop speed.
//! How long the loop runs is measured with Timer1 (TCNT1), not by counting
//! iterations: sampling stops once TCNT1 has advanced by the sampling time
//! (one Timer1 count = 16 us), and the sampling time is capped at
//! SAMPLING_WINDOW_MAX_PERCENT of the Timer1 period, whatever the trigger rate.

#pragma once
#include <Arduino.h>
#include "settings-eeprom.h"

//! @brief Longest sampling window, as a percentage of the Timer1 (trigger) period.
//!
//! The window must end before the next Timer1 compare match, which re-arms the
//! receivers. 80 % is the original TCNT1 cut-off (80 ms at 10 Hz).
constexpr uint8_t SAMPLING_WINDOW_MAX_PERCENT = 80;

//! Microseconds per Timer1 count (prescaler 256 at 16 MHz).
constexpr uint8_t TIMER1_US_PER_COUNT = 16;

//! @brief Samples the echo lines after each trigger and works out each
//!        receiver's tick count.
//!
//! The sampling time and the number of transitions to save are given to the
//! constructor, and the echo pins start as ECHO_PIN. All three can be changed
//! afterwards with the setters, or loaded together from the EEPROM settings
//! with applySettings(). The setters change only this object, not the
//! settings stored in EEPROM.
class UltrasonicSampling {
public:
    //! @brief Set the sampling time and transition limit. Does not touch any pin.
    //!
    //! The echo pins start as ECHO_PIN; applySettings() or setEchoPin() makes
    //! them INPUT_PULLUP. A value out of range is ignored and the default
    //! (SAMPLING_TIME_MS, MAX_SAVED_TICKS) is kept.
    //! @param sampling_time_ms Sampling time in milliseconds, SAMPLING_TIME_MIN_MS
    //!                         to SAMPLING_TIME_MAX_MS.
    //! @param max_saved_ticks  Transitions saved per measurement,
    //!                         MAX_SAVED_TICKS_MIN to SAMPLE_BUFFER_SIZE.
    UltrasonicSampling(uint16_t sampling_time_ms = SAMPLING_TIME_MS,
                       uint8_t max_saved_ticks = MAX_SAVED_TICKS);

    //! @brief Take the echo pins, sampling time and transition limit from the
    //!        EEPROM settings, and set the echo pins to INPUT_PULLUP.
    //!
    //! Call from setup() after SettingsEeprom::begin(), and again whenever those
    //! settings change.
    //! @param settings Settings to copy them from.
    void applySettings(const SettingsEeprom &settings);

    //! @brief Set the sampling time in milliseconds.
    //!
    //! Sampling stops once TCNT1 has advanced msToCounts(@p ms) counts.
    //! @param ms SAMPLING_TIME_MIN_MS (1 ms) to maxSamplingTimeMs().
    //! @return true if set; false, and nothing changes, if @p ms is out of range.
    bool setSamplingTimeMs(uint16_t ms);
    //! @brief Get the sampling time in force.
    //! @return The sampling time in milliseconds.
    uint16_t getSamplingTimeMs() const;

    //! @brief Get the sampling time in force as Timer1 counts.
    //! @return Counts of 16 us; sampling stops once TCNT1 has advanced this far.
    uint16_t getSamplingCounts() const;

    //! @brief Set how many echo-line transitions (tick values) are saved per measurement.
    //!
    //! Sampling stops early once this many have been saved.
    //! @param count MAX_SAVED_TICKS_MIN to SAMPLE_BUFFER_SIZE.
    //! @return true if set; false, and nothing changes, if @p count is out of range.
    bool setMaxSavedTicks(uint8_t count);
    //! @brief Get the transition limit in force.
    //! @return The most echo-line transitions saved per measurement.
    uint8_t getMaxSavedTicks() const;

    //! @brief Set the echo pin of one receiver and make it INPUT_PULLUP.
    //! @param receiver Receiver index, 0 to RECV_CNT - 1.
    //! @param pin      A0 to A5 (PORTC, read in one go by the sampling loop).
    //! @return true if set; false, and nothing changes, if either argument is
    //!         out of range.
    bool setEchoPin(uint8_t receiver, uint8_t pin);

    //! @brief Longest sampling time accepted right now (milliseconds).
    //!
    //! SAMPLING_WINDOW_MAX_PERCENT of the Timer1 period set by the trigger rate
    //! (80 ms at 10 Hz, 40 ms at 20 Hz, 800 ms at 1 Hz). Before Timer1 is
    //! started it returns SAMPLING_TIME_MAX_MS.
    //! @return The cap in milliseconds.
    static uint16_t maxSamplingTimeMs();

    //! @brief Convert milliseconds to Timer1 counts (16 us each).
    //! @param ms Time in milliseconds.
    //! @return ms x 1000 / 16, rounded down.
    static uint16_t msToCounts(uint16_t ms);

    //! @brief Measure the tick timings from the ultrasonic transmitter.
    //!
    //! Samples the echo pins until TCNT1 has advanced getSamplingCounts()
    //! counts since the start (the TCNT1 timeout), saving each
    //! change (up to getMaxSavedTicks() of them), then finds for each receiver
    //! the time (Timer1 counts) at which its echo line fell after all lines were high. Read the
    //! results with getTicks().
    //! @param noInterruptsFlag true to disable interrupts while sampling (they
    //!                         are enabled again afterwards); false, the
    //!                         default, leaves them enabled.
    //! @note Two quirks of the original algorithm are kept: "all lines high" is
    //!       the last saved reading with the numerically largest value, and the
    //!       line that fell is found by subtracting consecutive readings, so a
    //!       reading in which a line rises can credit the wrong receiver.
    void measureEchoTimes(bool noInterruptsFlag = false);

    //! @brief Generate fake timings to test serial functionality.
    //!
    //! Fills the results read by getTicks() with values around 401 (351 to 450)
    //! that change on each call.
    void simulateEchoTimes();

    //! @brief Get one receiver's tick count from the last measurement or simulation.
    //! @param receiver Receiver index, 0 to RECV_CNT - 1.
    //! @return Timer1 counts (16 us each); 0 if the receiver had no echo or is out of range.
    unsigned long getTicks(uint8_t receiver) const;

private:
    uint16_t      _samplingTimeMs;  //!< Sampling time in milliseconds.
    uint16_t      _samplingCounts;  //!< The same as Timer1 counts (msToCounts()).
    uint8_t       _maxSavedTicks;   //!< Most transitions saved per measurement.

    uint8_t _echoPin[RECV_CNT];     //!< Echo pin of each receiver.
    uint8_t _bitMask[RECV_CNT];     //!< PINC bit of each receiver's echo pin.
    uint8_t _echoMask;              //!< PINC bits of all echo pins.

    //! @name Echo-line transitions saved by the last measurement
    //! @{

    byte          _value[SAMPLE_BUFFER_SIZE];  //!< Echo lines (PINC bits) after each change.
    unsigned long _ticks[SAMPLE_BUFFER_SIZE];  //!< Timer1 counts at which each change was seen.
    unsigned int  _nwp;                        //!< Number of transitions saved.

    //! @}

    unsigned long _thisTicks[RECV_CNT];         //!< Result: tick count of each receiver.
    unsigned long _thisNotUsedTicks[RECV_CNT];  //!< 1 until a receiver's tick count is found.

    //! @brief Recompute _echoMask from _bitMask.
    void updateEchoMask();
};
