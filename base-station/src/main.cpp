/*
C++ program to control the base station of the UltraGPS system. Sends a radio pulse when
to initiate the ultrasonic ping that is then received by the set of receivers. The program
returns the tick count of all the receivers after the radio pulse is sent.

Example progam output:
4404, 3226, 4063, 4041, 3133, 4109
::
::

In the above example values represent the time it takes for the sound to travel the distance
to each of the six towers respectively.  The unit of time for the number is "counts" but will
definitely have an Ax+B type of offset where x is the count value, A is the scaling factor
from counts to distance (or time) and B is the scaling offset necessary to adjust for counts
not starting exactly at zero.

These values are output by the Arduino serially at X (set in the setup function) baud and thus can be recieved by
another program such as matlab.
----------

ChatGPT defines counts as the following:
In a timing system, "counts" typically refer to the incrementss of a digital
timer or counter that measures time intervals. These counts represent
discrete time steps based on the clock signal driving the system.

For example:
If a timer is running on a 1 MHz clock, it increments 1 count per microsecond (1 µs).
If a system counts clock cycles from a 10 MHz oscillator, each count represents 0.1 µs.

Common Uses of Counts in Timing Systems:
Measuring time intervals – Counting clock cycles to determine how long an event lasts.

Generating delays – Using a specific number of counts to create precise time delays.

Frequency measurement – Counting pulses within a known time window to determine frequency.

Timestamping events – Logging counts to mark when events occur relative to a reference time.

"counts" could be used in time-of-flight (ToF) calculations, where the system
counts clock cycles between the sonar pulse transmission and echo reception to
determine distance.

*/

#include <Arduino.h>
#include <SPI.h>
#include "radio.h"
#include "debug_macros.h"
#include "led_control.h"
#include "command_control.h"
#include "timers.h"
#include "sysreset.h"
#include "delay_store.h"

//! Definition to simulate "real" commands with pulse data
//! Used during development to test the serial communication and command handling without needing to trigger the ultrasonic sensor
//#define SIMULATE_REAL_COMMANDS 

//! Base Station Parameters
constexpr unsigned int TRIGGER_HIGH_DURATION = 300;     //! How long to hold the high state of the trigger pin (microseconds)
constexpr uint8_t ECHO_PIN[]={A0, A1, A2, A3, A4, A5};  //! List of all the receivers ultrasonic echo pins
//! Number of receivers being used
constexpr uint8_t RECV_CNT = sizeof(ECHO_PIN)/sizeof(ECHO_PIN[0]);
#define echo_value() (PINC & 63)                        //! Macro to isolate certain pins in PORTC
//! Serial Communication Parammters
constexpr unsigned int BAUD_RATE  = 38400;
//! PLL Synchronization Parameters
constexpr unsigned long SYNC_PLL_INTERVAL = 10;  //! How often to send a sync packet to the receiver to synchronize its PLL (milliseconds)

//! Default burst delay sent to the transmitter station with every measurement trigger (microseconds).
//! Experimentally the ultrasonic receivers need a few ms to "send" their own
//! burst and go into receive mode, so the transmitter's burst is delayed to make
//! sure the receivers are listening by the time it arrives.
//! This is the compiled-in default. The 'D' command searches for a better value,
//! stores it in EEPROM and applies it; the 'E' command returns to this default.
constexpr uint16_t SONIC_BURST_DELAY = 2*1000; //! 2ms delay
static_assert(SONIC_BURST_DELAY > 0, "a zero tick delay is reserved and ignored by the transmitter station");

//! Burst delay actually in use. Loaded from EEPROM in setup() if a value was
//! stored by the 'D' command, otherwise SONIC_BURST_DELAY.
uint16_t burst_delay_us = SONIC_BURST_DELAY;

//! Do not send a trigger packet in the last part of the Timer1 cycle: the packet
//! would race the compare match and the transmitter could fire with the previous
//! trigger's delay. 125 ticks = 2 ms at 16 us/tick.
constexpr uint16_t TRIGGER_SEND_GUARD_TICKS = 125;

//! Delay search ('D' command): sweep the burst delay and report the value that
//! gives the lowest mean tick count across the receivers.
constexpr uint16_t SEARCH_DELAY_MIN_US     = 0;    //! First delay tried (microseconds)
constexpr uint16_t SEARCH_DELAY_MAX_US     = 500;  //! Last delay tried (inclusive)
constexpr uint16_t SEARCH_DELAY_STEP_US    = 10;   //! Step between tries
constexpr uint8_t  SEARCH_SAMPLES_PER_STEP = 3;    //! Pulses averaged at each delay
constexpr bool     SEARCH_PRINT_STEPS      = true; //! Also print one "D: ..." line per step

//! Reset command ('R') behaviour: after sending the reset packet to the transmitter
//! station the base station also resets itself so both boards start from a clean state.
//! Set to false to only reset the transmitter station.
constexpr bool RESET_SELF_AFTER_REMOTE_RESET = false;
//! Milliseconds to wait after the reset packet before resetting ourselves, so the
//! acknowledgement line has left the UART and the remote board has rebooted.
constexpr uint16_t RESET_SELF_DELAY_MS = 100;

//! Instantiate all classes
constexpr uint8_t radioIRQPin = 2;    //! Must be interrupt-capable (2 or 3 on Uno)
RadioController radioCtrl(9, 10);     //! CE, CSN
LedControl leds(3, 4, 6, 7, 8);      //! STAT_1, STAT_2, STAT_3, PULSE_TRIG, RADIO_STATUS

//! Forward declarations for command callbacks (defined after measure/simulate helpers)
void onPulse(const CmdArg* args);
void onSimulate(const CmdArg* args);
void onSetDelay(const CmdArg* args);
void onContinuous(const CmdArg* args);
void onSimulatePulse(const CmdArg*);
void onSimulateContinous(const CmdArg* args);
void onReset(const CmdArg* args);
void onDelaySearch(const CmdArg* args);
void onDelayDefault(const CmdArg* args);

//! Command definitions
#ifdef SIMULATE_REAL_COMMANDS
static const CommandDef CMD_PULSE    = { CommandType::PULSE,     0, {}, onSimulatePulse };
static const CommandDef CMD_CONTINOUS = { CommandType::CONTINOUS,  0, {}, onSimulateContinous };
#else
static const CommandDef CMD_PULSE    = { CommandType::PULSE,     0, {}, onPulse };
static const CommandDef CMD_CONTINOUS = { CommandType::CONTINOUS,  0, {}, onContinuous };
#endif
static const CommandDef CMD_SIMULATE = { CommandType::SIMULATE,  0, {}, onSimulatePulse};
static const CommandDef CMD_RESET    = { CommandType::RESET,     0, {}, onReset };
static const CommandDef CMD_DELAYSRCH = { CommandType::DELAY_SEARCH, 0, {}, onDelaySearch };
static const CommandDef CMD_DELAYDFLT = { CommandType::DELAY_DEFAULT, 0, {}, onDelayDefault };
//! Example of command with an argument
//static const CommandDef CMD_SETDELAY = { CommandType::SET_DELAY, 1, { ArgType::INT },    onSetDelay };

//! Global command table — maps single ASCII letters to their command definitions
static const CommandEntry GLOBAL_CMD_TABLE[] = {
    { 'P', &CMD_PULSE    },  //! Trigger ultrasonic pulse
    { 'S', &CMD_SIMULATE },  //! Run simulation mode
    { 'C', &CMD_CONTINOUS },  //! Run continuous mode
    { 'R', &CMD_RESET    },  //! Reset the transmitter station (and this board)
    { 'D', &CMD_DELAYSRCH },  //! Search for the best burst delay, store and apply it
    { 'E', &CMD_DELAYDFLT },  //! Clear the stored burst delay, back to the default
};

CommandControl cmdCtrl(GLOBAL_CMD_TABLE, sizeof(GLOBAL_CMD_TABLE) / sizeof(GLOBAL_CMD_TABLE[0]));

void setup()
{
  //! Disarm the watchdog left running by a software reset (see sysreset.h)
  systemResetGuard();

  //! Serial setup
  Serial.begin(BAUD_RATE);
  while (!Serial && millis() < 3000);
  DEBUG_PRINTLN("Started");

  for(uint8_t i=0; i < RECV_CNT ;i++)
  {
    pinMode(ECHO_PIN[i], INPUT_PULLUP);
  }

  //! Initialiaze LEDs
  leds.init(0);
  
  //! Initialize the timers
  timer1_init_10Hz();
  
  //! Pin Setup
  pinMode(triggerPin, OUTPUT);

  //! Use the burst delay found by a previous 'D' search, if one is stored
  uint16_t stored_delay;
  if (delayStoreLoad(stored_delay)) {
    burst_delay_us = stored_delay;
    DEBUG_INFO_PRINTF("[SETUP] Using stored burst delay: %u us\n", burst_delay_us);
  } else {
    DEBUG_INFO_PRINTF("[SETUP] Using default burst delay: %u us\n", burst_delay_us);
  }

  // Initialize radio as the link master (sends to the transmitter station)
  if (!radioCtrl.init(RADIO_ROLE_BASE)) {
    Serial.println(F("[ERROR] Radio init failed. Halting."));
    while (true) {
      Serial.println(F("[ERROR] Still halted."));
      leds.setRadioStatus(true);
      delay(1000);
      leds.setRadioStatus(false);
      delay(1000);
    }
  }

  //! Radio initialized successfully - keep RADIO_STATUS LED on
  leds.setRadioStatus(true);

}

//! Global variables for echo times
static byte value[100]={0};
static unsigned long ticks[100]={0};
static unsigned long this_ticks[6];
static unsigned long this_not_used_ticks[6];
unsigned int nwp;

//! Measure the tick timings from ultrasonic transmitter
void measure_echo_times(bool noInterruptsFlag = false) {
    register byte this_value, previous_value;
    register unsigned long my_ticks;
    unsigned int i;

    unsigned int max, j;
    byte bits;

    nwp=0;
    this_value=previous_value=0;

    if (noInterruptsFlag) {
        noInterrupts();
    }
    //! This determines the "count" value for each sensor
    constexpr long max_tick_val = 0x31000/4; //! About 60 ms of sampling time
    
    //! Base sampling time around TCNT1 values
    uint16_t base_tick = TCNT1;

    for(my_ticks=0; my_ticks < max_tick_val; my_ticks++)
    {
      this_value = echo_value();
      if (this_value != previous_value)
      {
        value[nwp] = previous_value = this_value;
        ticks[nwp++] = my_ticks;

        if (nwp >= sizeof(value)/sizeof(value[0])) {
          break;
        }

      }

      //! TODO: Move calculations constants out (LIKE TIMER1_TOP*0.8)
      //! TODO: Review the need for this! (Changing sampling time might be better)
      //! This loop breaks out of the sampling time early if 80% of TCNT1 clock cycles
      //! have passed.
      if((int)(TCNT1 - base_tick)%timer1_top >= (uint16_t)(timer1_top*0.8) ) {
        break;
      }

    }

    if (noInterruptsFlag){
          interrupts();
    }
    DEBUG_PRINTF("nwp: %u\n", nwp);
    DEBUG_PRINTF("highest tick: %lu\n", my_ticks);

  //! Get the index of the max value
  //! Value = the state of the PINC register
  //! Max value = all pins are set (echos are set)
  //! To detect a distance the ultrasonic sensor
  //! Echo has to be set to high and the time difference
  //! between high and low is the distance
 for(max=i=0;i<nwp;i++)
 {
  if (value[i] >= value[max]) max = i;
 }
  DEBUG_PRINTF("Max PINC value: %u\n", value[max]);
  DEBUG_PRINTF("Max index value: %u\n", max);

  //! Set all values in this_not_used_ticks to 1 and
  ///! this_ticks to 0
 for(j=0;j<6;j++) {this_not_used_ticks[j]=1; this_ticks[j]=0;}

 //abits = 32+16+8+4+2+1; // 0x3F
 //! Start looking for the ticks
 //! After all the receivers have been triggered
 //! (echo pulled high)
 for(i=max+1;i<nwp;i++)
 {
  //! Max value of ticks is 66000*4
   if (ticks[i] > 66000*4) break;
   //! Which analog pin was set (value[t-1] - value[t])
   bits = value[i-1]-value[i];
   for(j=0;j<6;j++)
   {
    if ((bits & 1<<j) && (this_not_used_ticks[j]))
    {
         this_ticks[j] = ticks[i];
         this_not_used_ticks[j]=0;
    }
   }
 }
}

//! Handler function for handling the ultrasonic sensor pulses and printing the tick values
//! If print is true, it will print the values to serial. If false, it will just measure the echo times and set the LEDs without printing.
//! delay_us is the delay the transmitter station applies before firing its burst.
//! It defaults to the delay in force (burst_delay_us: stored by 'D' or the compiled-in
//! SONIC_BURST_DELAY); the delay search passes its own values here.
void handleUltrasonics(bool print = true, uint16_t delay_us = burst_delay_us) {
    //! If the compare match is imminent, let this cycle's trigger pass first so the
    //! packet has a full cycle to arrive before the transmitter fires with it.
    if ((uint16_t)(timer1_top - TCNT1) < TRIGGER_SEND_GUARD_TICKS) {
        trigger_fired = false;
        while(!trigger_fired){};
    }
    trigger_fired = false; //! Clear the trigger flag to prepare for the next pulse
    bool success = radioCtrl.sendTrigger(TCNT1, delay_us); 

    //! Wait for next pulse
    while(!trigger_fired){};
    
    //! Reset the trigger flag to signal that the most recent trigger has been sent and read
    //! Now that we are triggering at a set rate. 
    resetTriggerFlag();
    //! Measure the echo times without interrupts to avoid timing issues with the radio IRQ
    measure_echo_times(false); 
    leds.setPulseTrig(success);

    //! Print only if enabled
    if(!print){
      return;
    }
    String responseString;
    //! Add a little marker to mark continuous mode and normal (non-continuous) data in the serial output
    if(cmdCtrl.pollingState == PollingState::CONTINOUS) {
      responseString = "C: ";
    }else{
      responseString = "N: ";
    }
    responseString +=
    String(this_ticks[0]) + ", " +
    String(this_ticks[1]) + ", " +
    String(this_ticks[2]) + ", " +
    String(this_ticks[3]) + ", " +
    String(this_ticks[4]) + ", " +
    String(this_ticks[5]);
    Serial.println(responseString);
    leds.setPulseTrig(false);
}

//! Generate fake timings to test serial functionality
void simulate_echo_times() {
  //! Simulate all 6 receivers with values centered around 3000
  //! Add small variations to make it more realistic
  constexpr unsigned long BASE_COUNT = 401;
  constexpr int VARIATION = 50;  // ±200 counts variation

  //! Generate simulated values with slight variations for each receiver
  //! Using a simple pattern to create realistic-looking variations
  static unsigned long counter = 0;
  counter++;

  for(uint8_t i = 0; i < RECV_CNT; i++) {
    //! Create a pattern-based variation that changes slightly each call
    //! Each receiver gets a different offset based on its index
    int offset = ((i * 47) + (counter * 13)) % (VARIATION * 2) - VARIATION;
    this_ticks[i] = BASE_COUNT + offset;
  }
}

// ---------------------------------------------------------------------------
// Command callbacks
// ---------------------------------------------------------------------------
//! If not on continuous mode it just prints a single pulse
void onPulse(const CmdArg*) {
  if(cmdCtrl.pollingState != PollingState::POLLING) {
    cmdCtrl.pollingState = PollingState::POLLING;
  }
    handleUltrasonics(true);
}

void onContinuous(const CmdArg*) {
    cmdCtrl.pollingState = PollingState::CONTINOUS;
    handleUltrasonics(true);
}

void onSimulatePulse(const CmdArg*) {
    DEBUG_PRINTLN("Simulation mode activated");

    if(cmdCtrl.pollingState != PollingState::POLLING) {
      cmdCtrl.pollingState = PollingState::POLLING;
    }

    handleUltrasonics(false); //! Handle the ultrasonic logic without printing to serial
    simulate_echo_times();
    
    String responseString;
    //! Add a little marker to mark continuous mode and normal (non-continuous) data in the serial output
    responseString = "N: ";
    responseString +=
    String(this_ticks[0]) + ", " +
    String(this_ticks[1]) + ", " +
    String(this_ticks[2]) + ", " +
    String(this_ticks[3]) + ", " +
    String(this_ticks[4]) + ", " +
    String(this_ticks[5]);

    Serial.println(responseString);
}

void onSimulateContinous(const CmdArg*) {
    DEBUG_PRINTLN("Simulation mode activated");
    if(cmdCtrl.pollingState != PollingState::CONTINOUS) {
      cmdCtrl.pollingState = PollingState::CONTINOUS;
    }

    handleUltrasonics(false); //! Handle the ultrasonic logic without printing to serial
    simulate_echo_times();
    
    String responseString;
    //! Add a little marker to mark continuous mode and normal (non-continuous) data in the serial output
    responseString = "C: ";
    responseString +=
    String(this_ticks[0]) + ", " +
    String(this_ticks[1]) + ", " +
    String(this_ticks[2]) + ", " +
    String(this_ticks[3]) + ", " +
    String(this_ticks[4]) + ", " +
    String(this_ticks[5]);

    Serial.println(responseString);
}

//! 'R' — reset the transmitter station over the radio link, acknowledge with
//! "N: R" on serial, then (optionally) reset this board as well.
void onReset(const CmdArg*) {
    DEBUG_INFO_PRINTLN("[CMD] Reset requested");
    cmdCtrl.pollingState = PollingState::POLLING;

    bool sent = radioCtrl.sendReset();
    leds.setPulseTrig(sent);

    //! Acknowledge in the same "N: ..." format as a measurement line
    Serial.println(F("N: R"));
    Serial.flush();

    if (RESET_SELF_AFTER_REMOTE_RESET) {
        delay(RESET_SELF_DELAY_MS);
        systemReset();   //! does not return
    }
    leds.setPulseTrig(false);
}

//! 'D' — sweep the burst delay from SEARCH_DELAY_MIN_US to SEARCH_DELAY_MAX_US and
//! report the delay with the lowest mean tick count across all receivers.
//! A receiver that reports 0 (no echo) is excluded from the mean; delays with more
//! valid readings are always preferred over delays with fewer, so a delay that
//! makes receivers miss the burst cannot win by averaging zeros.
//! The winner is saved to EEPROM and becomes the delay in force (burst_delay_us)
//! immediately and on every later boot, until 'E' clears it.
//! If no step produced a single valid reading nothing is stored or changed.
//! Output: "D: <best_delay_us>, <mean_ticks>, <valid_readings>/<total_readings>"
void onDelaySearch(const CmdArg*) {
    DEBUG_INFO_PRINTLN("[CMD] Delay search started");
    cmdCtrl.pollingState = PollingState::POLLING;

    uint16_t      best_delay = SEARCH_DELAY_MIN_US;
    unsigned long best_avg   = 0;
    uint16_t      best_valid = 0;
    bool          have_best  = false;
    constexpr uint16_t readings_per_step = (uint16_t)SEARCH_SAMPLES_PER_STEP * RECV_CNT;

    for (uint32_t d = SEARCH_DELAY_MIN_US; d <= SEARCH_DELAY_MAX_US; d += SEARCH_DELAY_STEP_US) {
        unsigned long sum   = 0;
        uint16_t      valid = 0;

        //! A zero delay is reserved (the transmitter ignores it), so the 0 us
        //! step is measured with the smallest delay that is actually applied.
        const uint16_t delay_sent = d ? (uint16_t)d : 1;
        for (uint8_t s = 0; s < SEARCH_SAMPLES_PER_STEP; s++) {
            handleUltrasonics(false, delay_sent);
            for (uint8_t i = 0; i < RECV_CNT; i++) {
                if (this_ticks[i] > 0) {
                    sum += this_ticks[i];
                    valid++;
                }
            }
        }

        unsigned long avg = valid ? (sum / valid) : 0;

        if (SEARCH_PRINT_STEPS) {
            Serial.print(F("D: ")); Serial.print((uint16_t)d);
            Serial.print(F(", "));  Serial.print(avg);
            Serial.print(F(", "));  Serial.print(valid);
            Serial.print('/');      Serial.println(readings_per_step);
        }

        //! More valid readings wins; on a tie the lower mean wins.
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

    //! Persist and apply the winner. A zero winner only happens when the sweep
    //! starts at 0; store the 1 us that was actually sent.
    if (best_valid > 0) {
        uint16_t apply = best_delay ? best_delay : 1;
        if (delayStoreSave(apply)) {
            burst_delay_us = apply;
        }
    }

    Serial.print(F("D: ")); Serial.print(best_delay);
    Serial.print(F(", "));  Serial.print(best_avg);
    Serial.print(F(", "));  Serial.print(best_valid);
    Serial.print('/');      Serial.println(readings_per_step);
    leds.setPulseTrig(false);
}

//! 'E' — forget the burst delay stored by 'D' and go back to SONIC_BURST_DELAY,
//! now and on every later boot.
//! Output: "E: <delay_us now in force>"
void onDelayDefault(const CmdArg*) {
    DEBUG_INFO_PRINTLN("[CMD] Restoring default burst delay");
    cmdCtrl.pollingState = PollingState::POLLING;

    delayStoreClear();
    burst_delay_us = SONIC_BURST_DELAY;

    Serial.print(F("E: ")); Serial.println(burst_delay_us);
}

//! Periodically sends a sync packet to keep the transmitter station's PLL in
//! phase. A PKT_SYNC carries only the Timer1 value and never changes the burst
//! delay stored on the transmitter, so it cannot interfere with a measurement.
void syncPLL(){
    radioCtrl.sendSync(TCNT1);
}
// ---------------------------------------------------------------------------

void loop()
{
    long currentTime = millis();
    long lastSyncTime = 0;
    long lastPulseTime = 0;

    cmdCtrl.poll();

    //! Peridic non-blocking functions
    if (millis() - lastSyncTime >= (unsigned int)SYNC_PLL_INTERVAL) {
        syncPLL();
        lastSyncTime = currentTime;
    }
    if(cmdCtrl.pollingState == PollingState::CONTINOUS) {
      if (millis() - lastPulseTime >= continuousPollingDelay) {
          lastPulseTime = millis();
          #ifdef SIMULATE_REAL_COMMANDS
          onSimulateContinous(nullptr);
          #else
          onContinuous(nullptr);
          #endif
        }
    }

  }

