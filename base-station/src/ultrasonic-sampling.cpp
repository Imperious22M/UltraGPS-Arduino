//! @file ultrasonic-sampling.cpp
//! @brief UltrasonicSampling implementation: the echo sampling loop, the
//!        reduction to one tick count per receiver, and the simulated timings.
//!
//! The API is documented in ultrasonic-sampling.h.

#include "ultrasonic-sampling.h"
#include "debug_macros.h"
#include "timers.h"   // timer1_top and trigger_fired, used by the TCNT1 timeout in measureEchoTimes()

UltrasonicSampling::UltrasonicSampling(uint16_t sampling_time_ms, uint8_t max_saved_ticks)
    : _samplingTimeMs(SAMPLING_TIME_MS),
      _samplingCounts(msToCounts(SAMPLING_TIME_MS)),
      _maxSavedTicks(MAX_SAVED_TICKS),
      _echoMask(0),
      _value(),
      _ticks(),
      _nwp(0),
      _thisTicks(),
      _thisNotUsedTicks()
{
    for (uint8_t i = 0; i < RECV_CNT; i++) {
        _echoPin[i] = ECHO_PIN[i];
        _bitMask[i] = bit(ECHO_PIN[i] - PIN_A0);
    }
    updateEchoMask();

    // Out-of-range values leave the defaults set above
    setSamplingTimeMs(sampling_time_ms);
    setMaxSavedTicks(max_saved_ticks);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void UltrasonicSampling::applySettings(const SettingsEeprom &settings) {
    for (uint8_t i = 0; i < RECV_CNT; i++) {
        setEchoPin(i, settings.getEchoPin(i));
    }
    // A stored time longer than the current cap (e.g. after raising the
    // trigger rate) is clamped to the cap.
    const uint16_t cap = maxSamplingTimeMs();
    const uint16_t ms  = settings.getSamplingTimeMs();
    setSamplingTimeMs(ms > cap ? cap : ms);
    setMaxSavedTicks(settings.getMaxSavedTicks());
}

uint16_t UltrasonicSampling::maxSamplingTimeMs() {
    // Timer1 not started yet (global construction): no period to cap against
    if (timer1_top == 0) return SAMPLING_TIME_MAX_MS;
    const uint32_t cap_counts = ((uint32_t)timer1_top + 1) * SAMPLING_WINDOW_MAX_PERCENT / 100;
    const uint32_t cap_ms = cap_counts * TIMER1_US_PER_COUNT / 1000;
    return cap_ms > SAMPLING_TIME_MAX_MS ? SAMPLING_TIME_MAX_MS : (uint16_t)cap_ms;
}

bool UltrasonicSampling::setSamplingTimeMs(uint16_t ms) {
    if (ms < SAMPLING_TIME_MIN_MS || ms > maxSamplingTimeMs()) return false;
    _samplingTimeMs = ms;
    _samplingCounts = msToCounts(ms);
    return true;
}

uint16_t UltrasonicSampling::getSamplingTimeMs() const {
    return _samplingTimeMs;
}

uint16_t UltrasonicSampling::getSamplingCounts() const {
    return _samplingCounts;
}

bool UltrasonicSampling::setMaxSavedTicks(uint8_t count) {
    if (count < MAX_SAVED_TICKS_MIN || count > SAMPLE_BUFFER_SIZE) return false;
    _maxSavedTicks = count;
    return true;
}

uint8_t UltrasonicSampling::getMaxSavedTicks() const {
    return _maxSavedTicks;
}

bool UltrasonicSampling::setEchoPin(uint8_t receiver, uint8_t pin) {
    if (receiver >= RECV_CNT || !echoPinIsValid(pin)) return false;
    _echoPin[receiver] = pin;
    _bitMask[receiver] = bit(pin - PIN_A0);
    updateEchoMask();
    pinMode(pin, INPUT_PULLUP);
    return true;
}

void UltrasonicSampling::updateEchoMask() {
    _echoMask = 0;
    for (uint8_t i = 0; i < RECV_CNT; i++) {
        _echoMask |= _bitMask[i];
    }
}

uint16_t UltrasonicSampling::msToCounts(uint16_t ms) {
    return (uint16_t)((uint32_t)ms * 1000UL / TIMER1_US_PER_COUNT);
}

unsigned long UltrasonicSampling::getTicks(uint8_t receiver) const {
    return receiver < RECV_CNT ? _thisTicks[receiver] : 0;
}

// ---------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------

void UltrasonicSampling::measureEchoTimes(bool noInterruptsFlag) {
    register byte this_value, previous_value;
    register unsigned long my_ticks;
    unsigned int i;

    unsigned int max, j;
    byte bits;

    // Copy the settings and the buffer addresses into locals so the sampling loop
    // keeps them in registers instead of reloading them on every iteration.
    // TCNT1 timeout: the sampling time in Timer1 counts, capped again here in
    // case the trigger rate changed since it was set. These constants are worked
    // out once, outside the loop.
    const uint16_t      period       = timer1_top + 1;   // Timer1 counts per trigger period
    const uint16_t      cap_counts   = (uint16_t)((uint32_t)period * SAMPLING_WINDOW_MAX_PERCENT / 100);
    const uint16_t      limit        = _samplingCounts < cap_counts ? _samplingCounts : cap_counts;
    const unsigned int  max_saved    = _maxSavedTicks;
    const byte          echo_mask    = _echoMask;
    byte          *value = _value;
    unsigned long *ticks = _ticks;
    unsigned int   nwp   = 0;

    this_value=previous_value=0;

    if (noInterruptsFlag) {
        noInterrupts();
    }

    // Base sampling time around TCNT1 values
    const uint16_t base_tick = TCNT1;

    // Sample the echo pins and only save the changes that the echo pins have made.
    // Each change is timestamped with the Timer1 counts (16 us each) elapsed since
    // base_tick, so the results do not depend on how fast the loop runs.
    // my_ticks only counts loop passes (shown in the DEBUG output).
    for(my_ticks=0; ; my_ticks++)
    {
      // TCNT1 counts 0..timer1_top and restarts at the compare match, so add one
      // period when it has wrapped since base_tick.
      const uint16_t now = TCNT1;
      const uint16_t elapsed = now >= base_tick ? now - base_tick : now + period - base_tick;

      // TCNT1 timeout: stop once the sampling time has passed. Also stop if the
      // next compare match has fired (trigger_fired is set by the Timer1 ISR): a
      // long interrupt near the end could otherwise let TCNT1 wrap past
      // base_tick and hide the timeout.
      if (elapsed >= limit || trigger_fired) {
        break;
      }

      this_value = PINC & echo_mask;   // all echo lines in one read of PORTC
      if (this_value != previous_value)
      {
        value[nwp] = previous_value = this_value;
        ticks[nwp++] = elapsed;        // Timer1 counts since the start of sampling

        if (nwp >= max_saved) {
          break;
        }

      }

    }

    if (noInterruptsFlag){
          interrupts();
    }
    _nwp = nwp;
    DEBUG_PRINTF("nwp: %u\n", nwp);
    DEBUG_PRINTF("highest tick: %lu\n", my_ticks);

  // Get the index of the max value (the last one if it occurs more than once)
  // Value = the state of the PINC register
  // Max value = all pins are set (echoes are set)
  // To detect a distance, the ultrasonic sensor's
  // Echo has to be set to high, and the time difference
  // between high and low is the distance
 for(max=i=0;i<nwp;i++)
 {
  if (value[i] >= value[max]) max = i;
 }
  DEBUG_PRINTF("Max PINC value: %u\n", value[max]);
  DEBUG_PRINTF("Max index value: %u\n", max);

  // Set all values in _thisNotUsedTicks to 1 and _thisTicks to 0.
  // This is used to set the arrays to known values.
 for(j=0;j<RECV_CNT;j++) {_thisNotUsedTicks[j]=1; _thisTicks[j]=0;}

 //abits = 32+16+8+4+2+1; // 0x3F
 // Start looking for the ticks after all the receivers have been triggered
 // (echo pulled high). This finds the start for all the echo lines, which is
 // when all of them are set to high, which indicates the sensor is listening.
 for(i=max+1;i<nwp;i++)
 {
   // Which echo line fell (value[t-1] - value[t]). The subtraction, kept from
   // the original code, is only right while lines fall: a reading in which a
   // line rises can credit the wrong receiver.
   bits = value[i-1]-value[i];
   // Find the length in ticks of each echo line, and set _thisNotUsedTicks to 0
   // for each echo line that has been found to prevent its tick value from
   // being overridden.
   for(j=0;j<RECV_CNT;j++)
   {
    if ((bits & _bitMask[j]) && (_thisNotUsedTicks[j]))
    {
         _thisTicks[j] = ticks[i];
         _thisNotUsedTicks[j]=0;
    }
   }
 }
}

void UltrasonicSampling::simulateEchoTimes() {
  // Simulate all receivers with values centered around BASE_COUNT.
  // Add small variations to make it more realistic.
  constexpr unsigned long BASE_COUNT = 401;
  constexpr int VARIATION = 50;  // ±50 counts variation

  // Generate simulated values with slight variations for each receiver,
  // using a simple pattern to create realistic-looking variations
  static unsigned long counter = 0;
  counter++;

  for(uint8_t i = 0; i < RECV_CNT; i++) {
    // Create a pattern-based variation that changes slightly each call.
    // Each receiver gets a different offset based on its index.
    int offset = ((i * 47) + (counter * 13)) % (VARIATION * 2) - VARIATION;
    _thisTicks[i] = BASE_COUNT + offset;
  }
}
