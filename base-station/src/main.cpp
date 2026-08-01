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

//! Tick delay sent to receiver: how many Timer1 ticks after the packet arrives to fire the trigger.
constexpr uint16_t TRIGGER_TICK_DELAY = 100;

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

//! Command definitions
#ifdef SIMULATE_REAL_COMMANDS
static const CommandDef CMD_PULSE    = { CommandType::PULSE,     0, {}, onSimulatePulse };
static const CommandDef CMD_CONTINOUS = { CommandType::CONTINOUS,  0, {}, onSimulateContinous };
#else
static const CommandDef CMD_PULSE    = { CommandType::PULSE,     0, {}, onPulse };
static const CommandDef CMD_CONTINOUS = { CommandType::CONTINOUS,  0, {}, onContinuous };
#endif
static const CommandDef CMD_SIMULATE = { CommandType::SIMULATE,  0, {}, onSimulatePulse};
//! Example of command with an argument
//static const CommandDef CMD_SETDELAY = { CommandType::SET_DELAY, 1, { ArgType::INT },    onSetDelay };

//! Global command table — maps single ASCII letters to their command definitions
static const CommandEntry GLOBAL_CMD_TABLE[] = {
    { 'P', &CMD_PULSE    },  //! Trigger ultrasonic pulse
    { 'S', &CMD_SIMULATE },  //! Run simulation mode
    { 'C', &CMD_CONTINOUS },  //! Run continuous mode
};

CommandControl cmdCtrl(GLOBAL_CMD_TABLE, sizeof(GLOBAL_CMD_TABLE) / sizeof(GLOBAL_CMD_TABLE[0]));

void setup()
{
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

  // Initialize radio
  if (!radioCtrl.init()) {
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
  //Serial.println(F("[SETUP] Ready."));

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
void handleUltrasonics(bool print = true) {
    //! Experimentally the ultrasonic sensor needs about 3ms to "send" the ultrasonic
    //! burst and go into receiver mode. We do not care about the ultrasonic
    //! burst, so we just wait delay the activation of the trigger pin at the transmitter
    //! to ensure that the receiver is in receive mode by the time the transmitter's 
    //! burst is sent.
    constexpr uint16_t SONIC_BURST_DELAY = 2*1000; //! 2ms delay
    trigger_fired = false; //! Clear the trigger flag to prepare for the next pulse
    bool success = radioCtrl.sendTrigger(TCNT1, SONIC_BURST_DELAY); 

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

//! Periodically sends a trigger packet to synchronize the receiver's PLL
void syncPLL(){
    //! TODO
    //! Send a 0 to skip the processing/activation of a pin on the transmitter
    //! NOT YET IMPLEMENTED
    //! MAY NOT EVER BE IMPLEMENTED...
    radioCtrl.sendTrigger(TCNT1, 0);
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

