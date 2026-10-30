//! @file command_control.cpp
//! @brief CommandControl implementation: the base station's command definitions,
//!        letter table and command handlers.
//!
//! What each command does, its arguments and its reply are documented in
//! command_control.h.

#include "command_control.h"
#include "debug_macros.h"
#include "timers.h"
#include "sysreset.h"

CommandControl::CommandControl(RadioController& radio, LedControl& leds,
                               SettingsEeprom& settings, UltrasonicSampling& sampler)
    : CommandHandler(_cmdTable, CMD_COUNT),
      _radio(radio), _leds(leds), _settings(settings), _sampler(sampler),
      // Command table — maps single ASCII letters to their command definitions
      _cmdTable{
          { 'P', &_cmdPulse },          // Trigger an ultrasonic pulse
          { 'S', &_cmdSimulate },       // Run one pulse in simulation mode
          { 'C', &_cmdContinuous },     // Run in continuous mode
          { 'R', &_cmdReset },          // Reset the transmitter station (and optionally this board)
          { 'D', &_cmdDelaySearch },    // Search for the best burst delay, store and apply it
          { 'E', &_cmdDelayDefault },   // Store the default burst delay (SONIC_BURST_DELAY)
          { 'M', &_cmdSettingsReset },  // Reset every setting stored in EEPROM to its default
          { 'H', &_cmdSamplingTime },   // Set the ultrasonic sampling time in milliseconds
      }
{
    // Command definitions: { type, number of arguments, argument types, callback }
#ifdef SIMULATE_REAL_COMMANDS
    _cmdPulse         = { CommandType::PULSE,     0, {}, &CommandControl::dispatch<&CommandControl::onSimulatePulse> };
    _cmdContinuous    = { CommandType::CONTINUOUS,  0, {}, &CommandControl::dispatch<&CommandControl::onSimulateContinuous> };
#else
    _cmdPulse         = { CommandType::PULSE,     0, {}, &CommandControl::dispatch<&CommandControl::onPulse> };
    _cmdContinuous    = { CommandType::CONTINUOUS,  0, {}, &CommandControl::dispatch<&CommandControl::onContinuous> };
#endif
    _cmdSimulate      = { CommandType::SIMULATE,  0, {}, &CommandControl::dispatch<&CommandControl::onSimulatePulse> };
    _cmdReset         = { CommandType::RESET,     0, {}, &CommandControl::dispatch<&CommandControl::onReset> };
    _cmdDelaySearch   = { CommandType::DELAY_SEARCH, 0, {}, &CommandControl::dispatch<&CommandControl::onDelaySearch> };
    _cmdDelayDefault  = { CommandType::DELAY_DEFAULT, 0, {}, &CommandControl::dispatch<&CommandControl::onDelayDefault> };
    _cmdSettingsReset = { CommandType::SETTINGS_RESET, 0, {}, &CommandControl::dispatch<&CommandControl::onResetSettings> };
    _cmdSamplingTime  = { CommandType::SAMPLING_TIME, 1, { ArgType::INT }, &CommandControl::dispatch<&CommandControl::onSetSamplingTime> };
    // Example of a command with an argument
    //_cmdSetDelay    = { CommandType::SET_DELAY, 1, { ArgType::INT }, &CommandControl::dispatch<&CommandControl::onSetDelay> };
}

template <void (CommandControl::*Handler)(const CmdArg*)>
void CommandControl::dispatch(CommandHandler& handler, const CmdArg* args) {
    // Only a CommandControl hands its own table (and therefore this callback) to
    // CommandHandler, so handler is always a CommandControl.
    (static_cast<CommandControl&>(handler).*Handler)(args);
}

void CommandControl::runContinuous() {
    // The handler registered for 'C': onContinuous(), or onSimulateContinuous()
    // with SIMULATE_REAL_COMMANDS
    _cmdContinuous.callback(*this, nullptr);
}

void CommandControl::handleUltrasonics(bool print) {
    handleUltrasonics(print, _settings.getBurstDelayUs());
}

void CommandControl::handleUltrasonics(bool print, uint16_t delay_us) {
    // If the compare match is imminent, let this cycle's trigger pass first so the
    // packet has a full cycle to arrive before the transmitter fires with it.
    if ((uint16_t)(timer1_top - TCNT1) < _settings.getTriggerSendGuardTicks()) {
        trigger_fired = false;
        while(!trigger_fired){};
    }
    trigger_fired = false; // Clear the trigger flag to prepare for the next pulse
    bool success = _radio.sendTrigger(TCNT1, delay_us); 

    // Wait for the next pulse
    while(!trigger_fired){};
    
    // Reset the trigger flag to signal that the most recent trigger has been sent and
    // read, now that we are triggering at a set rate.
    resetTriggerFlag();
    // Measure the echo times with interrupts left enabled (false): passing true
    // would block the Timer1 ISR, and every other interrupt, while sampling.
    _sampler.measureEchoTimes(false);
    _leds.setPulseTrig(success);

    // Print only if enabled
    if(!print){
      return;
    }
    String responseString;
    // Add a little marker to mark continuous mode and normal (non-continuous)
    // data in the serial output
    if(pollingState == PollingState::CONTINUOUS) {
      responseString = "C: ";
    }else{
      responseString = "N: ";
    }
    responseString +=
    String(_sampler.getTicks(0)) + ", " +
    String(_sampler.getTicks(1)) + ", " +
    String(_sampler.getTicks(2)) + ", " +
    String(_sampler.getTicks(3)) + ", " +
    String(_sampler.getTicks(4)) + ", " +
    String(_sampler.getTicks(5));
    Serial.println(responseString);
    _leds.setPulseTrig(false);
}

// ---------------------------------------------------------------------------
// Command callbacks
// ---------------------------------------------------------------------------

// 'P' — leave continuous mode (if on) and print a single pulse
void CommandControl::onPulse(const CmdArg*) {
  if(pollingState != PollingState::POLLING) {
    pollingState = PollingState::POLLING;
  }
    handleUltrasonics(true);
}

// 'C' — continuous mode; loop() repeats it through runContinuous()
void CommandControl::onContinuous(const CmdArg*) {
    pollingState = PollingState::CONTINUOUS;
    handleUltrasonics(true);
}

// 'S' (and 'P' with SIMULATE_REAL_COMMANDS) — single pulse, simulated tick counts
void CommandControl::onSimulatePulse(const CmdArg*) {
    DEBUG_PRINTLN("Simulation mode activated");

    if(pollingState != PollingState::POLLING) {
      pollingState = PollingState::POLLING;
    }

    handleUltrasonics(false); // Handle the ultrasonic logic without printing to serial
    _sampler.simulateEchoTimes(); // Replace the measured tick counts with simulated ones
    
    String responseString;
    // Add a little marker to mark normal (non-continuous) data in the serial output
    responseString = "N: ";
    responseString +=
    String(_sampler.getTicks(0)) + ", " +
    String(_sampler.getTicks(1)) + ", " +
    String(_sampler.getTicks(2)) + ", " +
    String(_sampler.getTicks(3)) + ", " +
    String(_sampler.getTicks(4)) + ", " +
    String(_sampler.getTicks(5));

    Serial.println(responseString);
}

// 'C' with SIMULATE_REAL_COMMANDS — continuous mode, simulated tick counts
void CommandControl::onSimulateContinuous(const CmdArg*) {
    DEBUG_PRINTLN("Simulation mode activated");
    if(pollingState != PollingState::CONTINUOUS) {
      pollingState = PollingState::CONTINUOUS;
    }

    handleUltrasonics(false); // Handle the ultrasonic logic without printing to serial
    _sampler.simulateEchoTimes(); // Replace the measured tick counts with simulated ones
    
    String responseString;
    // Add a little marker to mark continuous-mode data in the serial output
    responseString = "C: ";
    responseString +=
    String(_sampler.getTicks(0)) + ", " +
    String(_sampler.getTicks(1)) + ", " +
    String(_sampler.getTicks(2)) + ", " +
    String(_sampler.getTicks(3)) + ", " +
    String(_sampler.getTicks(4)) + ", " +
    String(_sampler.getTicks(5));

    Serial.println(responseString);
}

// 'R' — reset the transmitter station, then (optionally) this board
void CommandControl::onReset(const CmdArg*) {
    DEBUG_INFO_PRINTLN("[CMD] Reset requested");
    pollingState = PollingState::POLLING;

    bool sent = _radio.sendReset();
    _leds.setPulseTrig(sent);

    // Acknowledge in the same "N: ..." format as a measurement line
    Serial.println(F("N: R"));
    Serial.flush();

    if (_settings.getResetSelfAfterRemoteReset()) {
        delay(_settings.getResetSelfDelayMs());
        systemReset();   // Does not return
    }
    _leds.setPulseTrig(false);
}

// 'D' — burst-delay search
void CommandControl::onDelaySearch(const CmdArg*) {
    DEBUG_INFO_PRINTLN("[CMD] Delay search started");
    pollingState = PollingState::POLLING;

    const uint16_t search_min   = _settings.getSearchDelayMinUs();
    const uint16_t search_max   = _settings.getSearchDelayMaxUs();
    const uint16_t search_step  = _settings.getSearchDelayStepUs();
    const uint8_t  samples      = _settings.getSearchSamplesPerStep();
    const bool     print_steps  = _settings.getSearchPrintSteps();

    uint16_t      best_delay = search_min;
    unsigned long best_avg   = 0;
    uint16_t      best_valid = 0;
    bool          have_best  = false;
    const uint16_t readings_per_step = (uint16_t)samples * RECV_CNT;

    for (uint32_t d = search_min; d <= search_max; d += search_step) {
        unsigned long sum   = 0;
        uint16_t      valid = 0;

        // A zero delay is reserved (the transmitter ignores it), so the 0 us
        // step is measured with the smallest delay that is actually applied.
        const uint16_t delay_sent = d ? (uint16_t)d : 1;
        for (uint8_t s = 0; s < samples; s++) {
            handleUltrasonics(false, delay_sent);
            for (uint8_t i = 0; i < RECV_CNT; i++) {
                if (_sampler.getTicks(i) > 0) {
                    sum += _sampler.getTicks(i);
                    valid++;
                }
            }
        }

        unsigned long avg = valid ? (sum / valid) : 0;

        if (print_steps) {
            Serial.print(F("D: ")); Serial.print((uint16_t)d);
            Serial.print(F(", "));  Serial.print(avg);
            Serial.print(F(", "));  Serial.print(valid);
            Serial.print('/');      Serial.println(readings_per_step);
        }

        // The delay with more valid readings wins; on a tie, the lower mean wins.
        bool better = !have_best
                   || valid > best_valid
                   || (valid == best_valid && valid > 0 && avg < best_avg);
        if (better) {
            have_best  = true;
            best_delay = (uint16_t)d;
            best_avg   = avg;
            best_valid = valid;
        }
    }

    // Persist and apply the winner, if any step produced a valid reading. A zero
    // winner only happens when the sweep starts at 0; store the 1 us that was
    // actually sent.
    if (best_valid > 0) {
        uint16_t apply = best_delay ? best_delay : 1;
        _settings.setBurstDelayUs(apply);
    }

    Serial.print(F("D: ")); Serial.print(best_delay);
    Serial.print(F(", "));  Serial.print(best_avg);
    Serial.print(F(", "));  Serial.print(best_valid);
    Serial.print('/');      Serial.println(readings_per_step);
    _leds.setPulseTrig(false);
}

// 'E' — store the default burst delay (SONIC_BURST_DELAY)
void CommandControl::onDelayDefault(const CmdArg*) {
    DEBUG_INFO_PRINTLN("[CMD] Restoring default burst delay");
    pollingState = PollingState::POLLING;

    _settings.setBurstDelayUs(SONIC_BURST_DELAY);

    Serial.print(F("E: ")); Serial.println(_settings.getBurstDelayUs());
}

// 'M' — reset every setting stored in EEPROM to its default
void CommandControl::onResetSettings(const CmdArg*) {
    DEBUG_INFO_PRINTLN("[CMD] Restoring default settings");
    pollingState = PollingState::POLLING;

    // Reply first: if the stored baud rate was not BAUD_RATE, the port switches
    // to BAUD_RATE as soon as the defaults are applied.
    Serial.println(F("M: OK"));
    Serial.flush();

    _settings.resetToDefaults();
    _sampler.applySettings(_settings);
}

// 'H <ms>' — set the ultrasonic sampling time
void CommandControl::onSetSamplingTime(const CmdArg* args) {
    DEBUG_INFO_PRINTLN("[CMD] Set sampling time");
    pollingState = PollingState::POLLING;

    const int32_t ms = args[0].i;
    if (ms >= SAMPLING_TIME_MIN_MS && ms <= _sampler.maxSamplingTimeMs()
        && _settings.setSamplingTimeMs((uint16_t)ms)) {
        _sampler.applySettings(_settings);
    }

    Serial.print(F("H: ")); Serial.print(_sampler.getSamplingTimeMs());
    Serial.print(F(", "));  Serial.println(_sampler.getMaxTicks());
}
