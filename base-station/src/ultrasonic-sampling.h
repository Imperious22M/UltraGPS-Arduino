//! @file ultrasonic-sampling.h
//! @brief Samples the receivers' echo lines and reduces them to one tick count
//!        per receiver.
//!
//! A **tick** is one full iteration of the sampling loop in
//! UltrasonicSampling::measureEchoTimes(). The sampling time is set in
//! milliseconds and converted to a maximum number of ticks using
//! SAMPLING_LOOP_CYCLES, the number of CPU clock cycles one iteration takes.

#pragma once
#include <Arduino.h>
#include "debug_macros.h"   // DEBUG changes the compiled sampling loop (see SAMPLING_LOOP_CYCLES)
#include "settings-eeprom.h"

//! @brief CPU clock cycles taken by one iteration of the sampling loop in which
//!        no echo line changes.
//!
//! Counted from the compiled loop (avr-gcc 7.3.0, -Os, LTO):
//!
//! | Instructions                                    | Cycles |
//! |-------------------------------------------------|--------|
//! | `cp`, 3 x `cpc` (my_ticks == max_tick_val)      | 4      |
//! | `breq` to the loop exit, not taken              | 1      |
//! | `in` PINC, `and` echo mask, `cp` previous value | 3      |
//! | `breq` past the save, taken                     | 2      |
//! | `ldi`, `sub`, 3 x `sbc` (my_ticks++)            | 5      |
//! | `mov` (previous value back into its register)   | 1      |
//! | `rjmp` back to the top                          | 2      |
//! | **Total**                                       | **18** |
//!
//! At 16 MHz one tick is therefore 1.125 us. The sampling time is approximate:
//! - an iteration that saves a transition takes 44 cycles, so each saved
//!   transition adds 26 cycles that are not counted as ticks;
//! - interrupts stay enabled while sampling (the commands call
//!   measureEchoTimes(false)) and stretch the real time: the millis()
//!   interrupt alone costs about 0.6 % (about 100 cycles every 1.024 ms),
//!   serial traffic adds more;
//! - the Uno's clock is a ceramic resonator, not an exact 16 MHz.
//!
//! @warning The count depends on how the compiler allocates registers in
//!          UltrasonicSampling::measureEchoTimes(), so re-count it whenever
//!          that function, the compiler or its flags change: run
//!          `avr-objdump -d -C .pio/build/uno/firmware.elf`, find the
//!          `in rX, 0x06` (the PINC read) in measureEchoTimes() and add up the
//!          loop path around it as in the table above. Uncommenting the
//!          TCNT1 timeout adds a 16-bit modulo and float arithmetic, several
//!          hundred cycles per iteration.
//! @note    Defining DEBUG adds prints to measureEchoTimes(), which changes how
//!          the loop compiles: max_tick_val no longer fits in registers and is
//!          re-read from the stack on every iteration (30 cycles, 63 with a
//!          transition, with avr-gcc 7.3.0), so a DEBUG build uses 30 here.
//!          INFO_DEBUG does not change the loop.
#ifdef DEBUG
constexpr uint8_t SAMPLING_LOOP_CYCLES = 30;
#else
constexpr uint8_t SAMPLING_LOOP_CYCLES = 18;
#endif

//! @brief Longest sampling window, as a percentage of the trigger period.
//!
//! The window must end before the next Timer1 compare match, which re-arms the
//! receivers. 80 % matches the old TCNT1 cut-off (80 ms at 10 Hz).
constexpr uint8_t SAMPLING_WINDOW_MAX_PERCENT = 80;

//! CPU clock cycles per millisecond (16000 on a 16 MHz Uno).
constexpr uint32_t CPU_CYCLES_PER_MS = F_CPU / 1000UL;

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
    //! Converted to a maximum tick count with msToTicks().
    //! @param ms SAMPLING_TIME_MIN_MS (1 ms) to maxSamplingTimeMs().
    //! @return true if set; false, and nothing changes, if @p ms is out of range.
    bool setSamplingTimeMs(uint16_t ms);
    //! @brief Get the sampling time in force.
    //! @return The sampling time in milliseconds, rounded to the nearest
    //!         millisecond if it was set with setMaxTicks().
    uint16_t getSamplingTimeMs() const;

    //! @brief Set the sampling time directly as a maximum tick count.
    //! @param ticks msToTicks(SAMPLING_TIME_MIN_MS) to msToTicks(maxSamplingTimeMs()).
    //! @return true if set; false, and nothing changes, if @p ticks is out of range.
    bool setMaxTicks(unsigned long ticks);
    //! @brief Get the maximum tick count in force.
    //! @return The maximum number of sampling-loop iterations per measurement.
    unsigned long getMaxTicks() const;

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
    //! SAMPLING_WINDOW_MAX_PERCENT of the trigger period set by the Timer1 rate
    //! (80 ms at 10 Hz, 40 ms at 20 Hz), never more than SAMPLING_TIME_MAX_MS.
    //! Before Timer1 is started it returns SAMPLING_TIME_MAX_MS.
    //! @return The cap in milliseconds.
    static uint16_t maxSamplingTimeMs();

    //! @brief Convert milliseconds to sampling-loop iterations (ticks).
    //!
    //! ticks = ms * CPU_CYCLES_PER_MS / SAMPLING_LOOP_CYCLES, rounded down
    //! (80 ms gives 71111 ticks).
    //! @param ms Time in milliseconds.
    //! @return The number of ticks.
    static unsigned long msToTicks(uint16_t ms);
    //! @brief Convert sampling-loop iterations (ticks) to milliseconds, rounded.
    //! @param ticks Number of ticks.
    //! @return The time in milliseconds, rounded to the nearest millisecond.
    //! @warning ticks * SAMPLING_LOOP_CYCLES must fit in 32 bits (about 238
    //!          million ticks) and the result in a uint16_t (65535 ms, about 58
    //!          million ticks), so larger values give a wrong result.
    //!          setMaxTicks() only passes values up to
    //!          msToTicks(SAMPLING_TIME_MAX_MS).
    static uint16_t ticksToMs(unsigned long ticks);

    //! @brief Measure the tick timings from the ultrasonic transmitter.
    //!
    //! Samples the echo pins for up to getMaxTicks() iterations, saving each
    //! change (up to getMaxSavedTicks() of them), then finds for each receiver
    //! the tick at which its echo line fell after all lines were high. Read the
    //! results with getTicks().
    //! @param noInterruptsFlag true to disable interrupts while sampling (they
    //!                         are enabled again afterwards); false, the
    //!                         default, leaves them enabled.
    //! @note Two quirks of the original algorithm are kept: "all lines high" is
    //!       the last saved reading with the numerically largest value, and the
    //!       line that fell is found by subtracting consecutive readings, so a
    //!       reading in which a line rises can credit the wrong receiver.
    //! @note Never inlined, cloned or specialised for its callers (link-time
    //!       optimisation would otherwise compile it differently depending on
    //!       how it is called), so the sampling loop compiles the same way
    //!       whatever calls it and SAMPLING_LOOP_CYCLES stays valid.
    __attribute__((noinline, noclone, externally_visible))
    void measureEchoTimes(bool noInterruptsFlag = false);

    //! @brief Generate fake timings to test serial functionality.
    //!
    //! Fills the results read by getTicks() with values around 401 (351 to 450)
    //! that change on each call.
    void simulateEchoTimes();

    //! @brief Get one receiver's tick count from the last measurement or simulation.
    //! @param receiver Receiver index, 0 to RECV_CNT - 1.
    //! @return The tick count; 0 if the receiver had no echo or is out of range.
    unsigned long getTicks(uint8_t receiver) const;

private:
    unsigned long _maxTicks;        //!< Most sampling-loop iterations per measurement.
    uint16_t      _samplingTimeMs;  //!< Sampling time (ms); rounded if set with setMaxTicks().
    uint8_t       _maxSavedTicks;   //!< Most transitions saved per measurement.

    uint8_t _echoPin[RECV_CNT];     //!< Echo pin of each receiver.
    uint8_t _bitMask[RECV_CNT];     //!< PINC bit of each receiver's echo pin.
    uint8_t _echoMask;              //!< PINC bits of all echo pins.

    //! @name Echo-line transitions saved by the last measurement
    //! @{

    byte          _value[SAMPLE_BUFFER_SIZE];  //!< Echo lines (PINC bits) after each change.
    unsigned long _ticks[SAMPLE_BUFFER_SIZE];  //!< Tick at which each change was seen.
    unsigned int  _nwp;                        //!< Number of transitions saved.

    //! @}

    unsigned long _thisTicks[RECV_CNT];         //!< Result: tick count of each receiver.
    unsigned long _thisNotUsedTicks[RECV_CNT];  //!< 1 until a receiver's tick count is found.

    //! @brief Recompute _echoMask from _bitMask.
    void updateEchoMask();
};
