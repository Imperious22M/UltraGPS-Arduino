//! @file ultrasonic-sampling.cpp
//! @brief UltrasonicSampling implementation: the echo sampling loop, the
//!        reduction to one tick count per receiver, and the simulated timings.
//!
//! The API is documented in ultrasonic-sampling.h.

#include "ultrasonic-sampling.h"
#include "debug_macros.h"
#include "timers.h"   // timer1_top, used by the (disabled) TCNT1 timeout in measureEchoTimes()

UltrasonicSampling::UltrasonicSampling(uint16_t sampling_time_ms, uint8_t max_saved_ticks)
    : _maxTicks(msToTicks(SAMPLING_TIME_MS)),
      _samplingTimeMs(SAMPLING_TIME_MS),
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
    // Timer1 not started yet (global construction): no rate to cap against
    if (timer1_top == 0) return SAMPLING_TIME_MAX_MS;
    // Trigger period = (timer1_top + 1) Timer1 counts of 256 CPU cycles
    const unsigned long period_ms = ((unsigned long)timer1_top + 1) * 256UL / CPU_CYCLES_PER_MS;
    const unsigned long cap = period_ms * SAMPLING_WINDOW_MAX_PERCENT / 100;
    return cap > SAMPLING_TIME_MAX_MS ? SAMPLING_TIME_MAX_MS : (uint16_t)cap;
}

bool UltrasonicSampling::setSamplingTimeMs(uint16_t ms) {
    if (ms < SAMPLING_TIME_MIN_MS || ms > maxSamplingTimeMs()) return false;
    _samplingTimeMs = ms;
    _maxTicks = msToTicks(ms);
    return true;
}

uint16_t UltrasonicSampling::getSamplingTimeMs() const {
    return _samplingTimeMs;
}

bool UltrasonicSampling::setMaxTicks(unsigned long ticks) {
    if (ticks < msToTicks(SAMPLING_TIME_MIN_MS) || ticks > msToTicks(maxSamplingTimeMs())) return false;
    _maxTicks = ticks;
    _samplingTimeMs = ticksToMs(ticks);
    return true;
}

unsigned long UltrasonicSampling::getMaxTicks() const {
    return _maxTicks;
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

unsigned long UltrasonicSampling::msToTicks(uint16_t ms) {
    return (unsigned long)ms * CPU_CYCLES_PER_MS / SAMPLING_LOOP_CYCLES;
}

uint16_t UltrasonicSampling::ticksToMs(unsigned long ticks) {
    return (uint16_t)((ticks * SAMPLING_LOOP_CYCLES + CPU_CYCLES_PER_MS / 2) / CPU_CYCLES_PER_MS);
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
    // keeps them in registers instead of reloading them on every iteration
    const unsigned long max_tick_val = _maxTicks;
    const unsigned int  max_saved    = _maxSavedTicks;
    const byte          echo_mask    = _echoMask;
    byte          *value = _value;
    unsigned long *ticks = _ticks;
    unsigned int   nwp   = 0;

    this_value=previous_value=0;

    if (noInterruptsFlag) {
        noInterrupts();
    }

    // Base sampling time around TCNT1 values. Only the TCNT1 timeout below uses
    // it; it is still captured so the timeout can be uncommented as it was.
    uint16_t base_tick = TCNT1;
    (void)base_tick;

    // Sample the echo pins and only save the changes that the echo pins have made.
    // One iteration is one tick (SAMPLING_LOOP_CYCLES CPU cycles).
    for(my_ticks=0; my_ticks < max_tick_val; my_ticks++)
    {
      this_value = PINC & echo_mask;   // all echo lines in one read of PORTC
      if (this_value != previous_value)
      {
        value[nwp] = previous_value = this_value;
        ticks[nwp++] = my_ticks;

        if (nwp >= max_saved) {
          break;
        }

      }

      // TCNT1 timeout, disabled for now: the sampling time (setSamplingTimeMs())
      // bounds the loop instead. Uncommenting the block below restores it as it
      // was; SAMPLING_LOOP_CYCLES must then be re-counted.
      // TODO: Move calculations constants out (LIKE TIMER1_TOP*0.8)
      // TODO: Review the need for this! (Changing sampling time might be better)
      // This loop breaks out of the sampling time early if 80% of TCNT1 clock cycles
      // have passed.
      // if((int)(TCNT1 - base_tick)%timer1_top >= (uint16_t)(timer1_top*0.8) ) {
      //   break;
      // }

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
   // Stop at a tick past the sampling window (the maximum tick count). The
   // sampling loop never saves one, so this is only a safeguard.
   if (ticks[i] > max_tick_val) break;
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
