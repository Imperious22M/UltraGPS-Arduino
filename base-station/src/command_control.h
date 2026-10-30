//! @file command_control.h
//! @brief The base station's serial commands: the list of commands, the
//!        letter -> command table and the command handlers.
//!
//! CommandHandler (command_handler.h) buffers, decodes and dispatches the
//! serial input; CommandControl adds the base station's commands to it.

#pragma once
#include <Arduino.h>
#include "command_handler.h"
#include "radio.h"
#include "led_control.h"
#include "settings-eeprom.h"
#include "ultrasonic-sampling.h"

// Definition to simulate "real" commands with pulse data: when defined, 'P' and
// 'C' print simulated tick counts (onSimulatePulse(), onSimulateContinuous())
// instead of measured ones. Used during development to test the serial
// communication and command handling without needing the ultrasonic sensors.
//#define SIMULATE_REAL_COMMANDS

// ---------------------------------------------------------------------------
// Command identity enum — extend this as new commands are added
// ---------------------------------------------------------------------------

//! @brief Enumerates every known command type.
//!
//! Each command's description gives its serial letter; the letters are
//! assigned in the CommandControl constructor. command_handler.h only
//! declares this enum (without the list), so that CommandDef can name it.
enum class CommandType : uint8_t {
    PULSE,          //!< 'P' — trigger an ultrasonic pulse and print the tick counts.
    SIMULATE,       //!< 'S' — run one pulse in simulation mode (simulated tick counts).
    CONTINUOUS,     //!< 'C' — run in continuous mode.
    RESET,          //!< 'R' — reset the transmitter station over radio, and optionally this board.
    DELAY_SEARCH,   //!< 'D' — search for the best burst delay, store and apply it.
    DELAY_DEFAULT,  //!< 'E' — store SONIC_BURST_DELAY as the burst delay (replacing a 'D' result).
    SETTINGS_RESET, //!< 'M' — reset every setting stored in EEPROM to its default.
    SAMPLING_TIME,  //!< 'H' — set the ultrasonic sampling time in milliseconds.
    TRIGGER_RATE,   //!< 'F' — set the trigger rate (Timer1 frequency) in Hz, on both stations.
    BAUD_RATE_SET,  //!< 'B' — set the serial baud rate (stored, applied immediately).
    UNKNOWN,        //!< Not a command; no command definition uses it.
};

// ---------------------------------------------------------------------------
// CommandControl class
// ---------------------------------------------------------------------------

//! @brief The base station's serial commands.
//!
//! The command definitions and the letter -> command table are set up in the
//! constructor; the handlers are private and are only reached through poll()
//! (and runContinuous()). CommandType lists the commands and their letters.
class CommandControl : public CommandHandler {
public:
    //! @brief Construct with the modules the commands drive.
    //!
    //! Only stores references (and sets up the command definitions and the
    //! letter -> command table), so it is safe to construct before setup().
    //! @param radio    Radio link to the transmitter station.
    //! @param leds     Status LEDs.
    //! @param settings Settings stored in EEPROM.
    //! @param sampler  Echo sampler that measures the tick counts.
    CommandControl(RadioController& radio, LedControl& leds,
                   SettingsEeprom& settings, UltrasonicSampling& sampler);

    //! @brief Run the continuous-mode command ('C') once.
    //!
    //! Called from loop() while pollingState is PollingState::CONTINUOUS. Runs
    //! the handler registered for 'C': onContinuous(), or onSimulateContinuous()
    //! if SIMULATE_REAL_COMMANDS is defined.
    void runContinuous();

private:
    //! Number of entries in _cmdTable (a 9th initializer fails to compile).
    static constexpr uint8_t CMD_COUNT = 10;

    RadioController&    _radio;     //!< Radio link to the transmitter station.
    LedControl&         _leds;      //!< Status LEDs.
    SettingsEeprom&     _settings;  //!< Settings stored in EEPROM.
    UltrasonicSampling& _sampler;   //!< Measures the echo times.

    //! @name Command definitions
    //! Filled in by the constructor; _cmdTable points to them.
    //! @{

    CommandDef _cmdPulse;          //!< The 'P' command.
    CommandDef _cmdSimulate;       //!< The 'S' command.
    CommandDef _cmdContinuous;     //!< The 'C' command.
    CommandDef _cmdReset;          //!< The 'R' command.
    CommandDef _cmdDelaySearch;    //!< The 'D' command.
    CommandDef _cmdDelayDefault;   //!< The 'E' command.
    CommandDef _cmdSettingsReset;  //!< The 'M' command.
    CommandDef _cmdSamplingTime;   //!< The 'H' command.
    CommandDef _cmdTriggerRate;    //!< The 'F' command.
    CommandDef _cmdBaudRate;       //!< The 'B' command.

    //! @}

    //! Command table — maps single ASCII letters to their command definitions.
    CommandEntry _cmdTable[CMD_COUNT];

    //! @brief Callback stored in a CommandDef: forwards to the member function
    //!        @p Handler of the CommandControl that decoded the command.
    //! @tparam Handler Command handler (a member function) to call.
    //! @param handler  The CommandHandler that decoded the command; always a
    //!                 CommandControl, since only a CommandControl hands its own
    //!                 table (and therefore this callback) to CommandHandler.
    //! @param args     Decoded arguments, passed on to @p Handler; nullptr when
    //!                 runContinuous() calls the callback.
    template <void (CommandControl::*Handler)(const CmdArg*)>
    static void dispatch(CommandHandler& handler, const CmdArg* args);

    //! @brief Handle one ultrasonic pulse with the burst delay in force: trigger
    //!        it, measure the echo times and print the tick values.
    //!
    //! If @p print is false, it just measures the echo times and sets the LEDs
    //! without printing. Uses the delay in force, SettingsEeprom::getBurstDelayUs():
    //! the delay stored by 'D', or SONIC_BURST_DELAY (stored on the first boot
    //! and by 'E' and 'M').
    //! @param print true to print the tick values to serial.
    //! @see handleUltrasonics(bool, uint16_t), which does the work.
    void handleUltrasonics(bool print = true);

    //! @brief Same as handleUltrasonics(bool), but the transmitter station applies
    //!        @p delay_us before firing its burst; the delay search passes its own
    //!        values here.
    //!
    //! Sends a PKT_TRIGGER carrying TCNT1 and @p delay_us, waits for the next
    //! trigger pulse (Timer1 compare match), then samples the echo lines for up
    //! to the sampling time; read the results with UltrasonicSampling::getTicks().
    //! The PULSE_TRIG LED shows whether the packet was sent. If the next compare
    //! match is less than SettingsEeprom::getTriggerSendGuardTicks() Timer1
    //! ticks away, it first waits for that trigger to pass, so the packet has a
    //! full cycle to arrive before the transmitter fires with it. Blocks until
    //! the next trigger pulse, then for up to the sampling time.
    //! @param print    true to print the tick values to serial (`N: <t0>, ..., <t5>`,
    //!                 or `C: ...` in continuous mode) and turn the LED off again;
    //!                 false to just measure the echo times and set the LEDs.
    //! @param delay_us Burst delay in microseconds. Must not be 0: a zero delay is
    //!                 reserved, and RadioController::sendTrigger() refuses to
    //!                 send it.
    void handleUltrasonics(bool print, uint16_t delay_us);

    //! @name Command handlers
    //! Reached through dispatch() when poll() decodes the command's letter. Every
    //! handler except onContinuous() and onSimulateContinuous() sets pollingState
    //! to PollingState::POLLING, which ends continuous mode.
    //! @{

    //! @brief 'P': trigger a single ultrasonic pulse, measure it and print the
    //!        tick counts.
    //! @param args Unused; 'P' takes no arguments.
    //! @par Reply
    //! One tick count per receiver (0 if it had no echo):
    //! @code
    //! N: <t0>, <t1>, <t2>, <t3>, <t4>, <t5>
    //! @endcode
    void onPulse(const CmdArg* args);

    //! @brief 'C': switch to continuous mode and measure pulses until another
    //!        command ends it.
    //!
    //! Measures and prints one pulse; loop() then calls runContinuous(), which
    //! calls this again, for as long as pollingState stays
    //! PollingState::CONTINUOUS.
    //! @param args Unused; 'C' takes no arguments.
    //! @par Reply
    //! A stream of lines, one per pulse, like the reply to 'P' but marked `C:`:
    //! @code
    //! C: <t0>, <t1>, <t2>, <t3>, <t4>, <t5>
    //! @endcode
    void onContinuous(const CmdArg* args);

    //! @brief 'S' (and 'P' if SIMULATE_REAL_COMMANDS is defined): like 'P', but
    //!        print simulated tick counts.
    //!
    //! A pulse is still triggered and measured, but the tick counts printed come
    //! from UltrasonicSampling::simulateEchoTimes().
    //! @param args Unused; 'S' takes no arguments.
    //! @par Reply
    //! @code
    //! N: <t0>, <t1>, <t2>, <t3>, <t4>, <t5>
    //! @endcode
    void onSimulatePulse(const CmdArg* args);

    //! @brief 'C' if SIMULATE_REAL_COMMANDS is defined: like onContinuous(), but
    //!        print simulated tick counts.
    //! @param args Unused; 'C' takes no arguments.
    //! @par Reply
    //! A stream of lines, one per pulse:
    //! @code
    //! C: <t0>, <t1>, <t2>, <t3>, <t4>, <t5>
    //! @endcode
    void onSimulateContinuous(const CmdArg* args);

    //! @brief 'R': reset the transmitter station over the radio link, acknowledge
    //!        on serial, then (optionally) reset this board as well.
    //!
    //! The reset packet is sent several times (RadioController::sendReset())
    //! because the link has no auto-ack; the reply therefore does not confirm
    //! that it arrived. If SettingsEeprom::getResetSelfAfterRemoteReset() is true
    //! (default RESET_SELF_AFTER_REMOTE_RESET), this board resets too,
    //! SettingsEeprom::getResetSelfDelayMs() milliseconds after the reply, and
    //! the call does not return.
    //! @param args Unused; 'R' takes no arguments.
    //! @par Reply
    //! In the same `N: ...` format as a measurement line:
    //! @code
    //! N: R
    //! @endcode
    void onReset(const CmdArg* args);

    //! @brief 'D': sweep the burst delay and store the best one (most valid
    //!        readings, then lowest mean tick count).
    //!
    //! The sweep runs from the search minimum to the search maximum (inclusive)
    //! in steps of the search step, measuring the same number of pulses at each
    //! delay. These four values are settings, with defaults SEARCH_DELAY_MIN_US,
    //! SEARCH_DELAY_MAX_US, SEARCH_DELAY_STEP_US and SEARCH_SAMPLES_PER_STEP; the
    //! default sweep takes about 15 s. A zero delay is reserved, so a 0 us step is
    //! sent as 1 us (and stored as 1 us if it wins, while the reply shows 0).
    //!
    //! The delay reported is the one with the lowest mean tick count across all
    //! receivers, among the delays with the most valid readings. A receiver that
    //! reports 0 (no echo) is excluded from the mean; delays with more valid
    //! readings are always preferred over delays with fewer, so a delay that
    //! makes receivers miss the burst cannot win by averaging zeros. The winner
    //! is saved to EEPROM and becomes the delay in force
    //! (SettingsEeprom::getBurstDelayUs()) immediately and on every later boot,
    //! until 'E' or 'M' restores the default. If no step produced a single valid
    //! reading, nothing is stored or changed.
    //! @param args Unused; 'D' takes no arguments.
    //! @par Reply
    //! If the print-steps setting is on (default SEARCH_PRINT_STEPS), one line per
    //! step first, in the same format; then, always, the winner:
    //! @code
    //! D: <best_delay_us>, <mean_ticks>, <valid_readings>/<total_readings>
    //! @endcode
    //! `<total_readings>` is the pulses per step times RECV_CNT. If no step
    //! produced a valid reading, the last line reads
    //! `D: <search minimum>, 0, 0/<total_readings>`.
    void onDelaySearch(const CmdArg* args);

    //! @brief 'E': store SONIC_BURST_DELAY as the burst delay, in EEPROM and RAM.
    //!
    //! Replaces a delay stored by 'D'; the default stays in force now and on
    //! every later boot, until 'D' stores another one.
    //! @param args Unused; 'E' takes no arguments.
    //! @par Reply
    //! @code
    //! E: <delay_us now in force>
    //! @endcode
    void onDelayDefault(const CmdArg* args);

    //! @brief 'M': reset every setting stored in EEPROM to its default in
    //!        settings-eeprom.h, now and on every later boot.
    //!
    //! Also re-applies the sampling settings (UltrasonicSampling::applySettings()).
    //! The reply is sent, and flushed, before the defaults are applied: if the
    //! stored baud rate was not BAUD_RATE, the serial port switches to BAUD_RATE
    //! as soon as they are.
    //! @param args Unused; 'M' takes no arguments.
    //! @par Reply
    //! @code
    //! M: OK
    //! @endcode
    void onResetSettings(const CmdArg* args);

    //! @brief 'H': set the ultrasonic sampling time in milliseconds, store it in
    //!        EEPROM and apply it.
    //!
    //! Sent as `H <ms>` (`H80` works too), with `<ms>` from SAMPLING_TIME_MIN_MS
    //! to UltrasonicSampling::maxSamplingTimeMs(), i.e. 1 ms to 80 % of the trigger period (80 ms at 10 Hz, 40 ms at 20 Hz). The milliseconds are converted
    //! to Timer1 counts (16 us each), see UltrasonicSampling::msToCounts();
    //! sampling stops once TCNT1 has advanced that far. A value out of range changes nothing (the
    //! reply then shows the sampling time still in force). The argument must be
    //! an integer: a missing or non-numeric one is rejected by the decoder and,
    //! like any invalid line, gets no reply.
    //! @param args `args[0].i` is the sampling time in milliseconds.
    //! @par Reply
    //! @code
    //! H: <sampling time in force (ms)>, <Timer1 counts>
    //! @endcode
    //! For example `H: 80, 5000` for the default 80 ms.
    void onSetSamplingTime(const CmdArg* args);
    //! @brief 'F': set the trigger rate (Timer1 frequency), store it in EEPROM,
    //!        apply it on the fly and send it to the transmitter station.
    //!
    //! Sent as `F <Hz>`, TRIGGER_RATE_MIN_HZ to TRIGGER_RATE_MAX_HZ (1–50 Hz).
    //! Timer1 restarts with TOP = triggerRateToTop(Hz); a PKT_RATE packet (sent
    //! three times) makes the transmitter station store the same TOP and reset.
    //! The sampling time (stored and in force) is set to its maximum, 80 % of
    //! the new period; 'H' can shorten it afterwards. Out of range changes
    //! nothing.
    //! @param args `args[0].i` is the rate in Hz.
    //! @par Reply
    //! @code
    //! F: <rate in force (Hz)>, <Timer1 TOP>, <sampling time in force (ms)>
    //! @endcode
    void onSetTriggerRate(const CmdArg* args);
    //! @brief 'B': set the serial baud rate, store it in EEPROM and switch to it.
    //!
    //! Sent as `B <baud>` with a rate baudRateIsValid() accepts (300 ... 1000000,
    //! 230400 excluded). The reply goes out at the OLD rate, then the port
    //! switches, so reconnect the host at the new rate. The rate is kept across
    //! resets; 'M' (sent at the new rate) restores BAUD_RATE (38400). An invalid
    //! rate changes nothing and the reply shows the rate still in force.
    //! @param args `args[0].i` is the baud rate.
    //! @par Reply
    //! @code
    //! B: <baud rate in force after the command>
    //! @endcode
    void onSetBaudRate(const CmdArg* args);
    //! @brief Restart Timer1 at the stored trigger rate, send it to the
    //!        transmitter station and re-apply the sampling settings.
    void applyTriggerRate();
    // Example of a command with an argument (see the constructor).
    //void onSetDelay(const CmdArg* args);

    //! @}
};
