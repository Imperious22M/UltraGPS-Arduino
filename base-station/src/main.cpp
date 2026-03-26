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


//! Base Station Parameters
constexpr unsigned int TRIGGER_HIGH_DURATION = 300;     //! How long to hold the high state of the trigger pin (microseconds)
constexpr uint8_t ECHO_PIN[]={A0, A1, A2, A3, A4, A5};  //! List of all the receivers ultrasonic echo pins
//! Number of receivers being used
constexpr uint8_t RECV_CNT = sizeof(ECHO_PIN)/sizeof(ECHO_PIN[0]);
#define echo_value() (PINC & 63)                        //! Macro to isolate certain pins in PORTC
//! Serial Communication Parammters
constexpr unsigned int BAUD_RATE  = 38400;
//! PLL Synchronization Parameters
constexpr unsigned long SYNC_PLL_INTERVAL = 100;  //! How often to send a

//! Tick delay sent to receiver: how many Timer1 ticks after the packet arrives to fire the trigger.
constexpr uint16_t TRIGGER_TICK_DELAY = 100;

//! Makes the measure_echo_times tick count loop
//! Exit as soon as all 0's are detected in the PINC
//! register (When all receivers have received)
//#define ECHO_EARLY_EXIT

//! Instantiate all classes
constexpr uint8_t radioIRQPin = 2;    //! Must be interrupt-capable (2 or 3 on Uno)
RadioController radioCtrl(9, 10);     //! CE, CSN
LedControl leds(3, 4, 6, 7, 8);      //! STAT_1, STAT_2, STAT_3, PULSE_TRIG, RADIO_STATUS

//! Forward declarations for command callbacks (defined after measure/simulate helpers)
void onPulse(const CmdArg* args);
void onSimulate(const CmdArg* args);
void onSetDelay(const CmdArg* args);

//! Command definitions
static const CommandDef CMD_PULSE    = { CommandType::PULSE,     0, {},                  onPulse    };
static const CommandDef CMD_SIMULATE = { CommandType::SIMULATE,  0, {},                  onSimulate };
//! Example of command with an argument
//static const CommandDef CMD_SETDELAY = { CommandType::SET_DELAY, 1, { ArgType::INT },    onSetDelay };

//! Global command table — maps single ASCII letters to their command definitions
static const CommandEntry GLOBAL_CMD_TABLE[] = {
    { 'P', &CMD_PULSE    },  //! Trigger ultrasonic pulse
    { 'S', &CMD_SIMULATE },  //! Run simulation mode
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
  timer1_init_15Hz();
  
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
    //constexpr long max_tick_val = 0x31000;
    //! The total time that it is looping here is way too large
    //! WARN
    constexpr long max_tick_val = 0x31000/4; //! About 60 ms of sampling time
    constexpr byte ALL_ECHOES = 63;  //! All 6 echo pins (bits 0-5)
    byte seen_all_high = 0;  //! Track if we've seen all echoes go HIGH
    
    //! Base sampling time around TCNT1 values
    uint16_t base_tick = TCNT1;

    //Serial.println(F("[INFO] Starting echo time measurement loop..."));
    //Serial.println(millis());

    for(my_ticks=0; my_ticks < max_tick_val; my_ticks++)
    //while(TCNT1-base_tick < timer1_top/2)
    {
      this_value = echo_value();
      if (this_value != previous_value)
      {
        value[nwp] = previous_value = this_value;
        ticks[nwp++] = my_ticks;

        #ifdef ECHO_EARLY_EXIT
        //! Track when all echoes have gone HIGH
        if (this_value == ALL_ECHOES) {
          seen_all_high = 1;
        }
        #endif

        if (nwp >= sizeof(value)/sizeof(value[0])) {
          break;
        }

        #ifdef ECHO_EARLY_EXIT
        //! Exit early: all 6 echoes went HIGH then returned LOW
        //! AKA all 6 echos have been received
        if (seen_all_high && this_value == 0) {
          break;
        }
        #endif
      }
      //! TODO: Move calculations constants out (LIKE TIMER1_TOP*0.8)
      if((int)(TCNT1 - base_tick)%timer1_top >= (uint16_t)(timer1_top*0.8) ) {
        //Serial.println(F("[WARNING] Exiting echo time measurement loop early to avoid overflow."));
        break;
      }

    }

    //Serial.println(millis()- millis_before);
    //Serial.println(F("[INFO] End time measurement loop..."));

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

void simulate_echo_times() {
  //! Simulate all 6 receivers with values centered around 3000
  //! Add small variations to make it more realistic
  constexpr unsigned long BASE_COUNT = 3000;
  constexpr int VARIATION = 200;  // ±200 counts variation

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

void onPulse(const CmdArg*) {

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
    resetTriggerFlag();

    //! Now that we are triggering at a set rate. 
    measure_echo_times(false); //! Most of the slow down is here

    leds.setPulseTrig(success);
    Serial.print(this_ticks[0]);   Serial.print(", ");
    Serial.print(this_ticks[1]);   Serial.print(", ");
    Serial.print(this_ticks[2]);   Serial.print(", ");
    Serial.print(this_ticks[3]);   Serial.print(", ");
    Serial.print(this_ticks[4]);   Serial.print(", ");
    Serial.println(this_ticks[5]);

}

void onSimulate(const CmdArg*) {
    DEBUG_PRINTLN("Simulation mode activated");

    leds.setPulseTrig(true);
    simulate_echo_times();

    Serial.print(this_ticks[0]);   Serial.print(", ");
    Serial.print(this_ticks[1]);   Serial.print(", ");
    Serial.print(this_ticks[2]);   Serial.print(", ");
    Serial.print(this_ticks[3]);   Serial.print(", ");
    Serial.print(this_ticks[4]);   Serial.print(", ");
    Serial.println(this_ticks[5]);

    leds.setPulseTrig(false);
}

//! Periodically sends a trigger packet to synchronize the receiver's PLL
void syncPLL(){
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

    //cmdCtrl.poll();

    if (currentTime - lastSyncTime >= (unsigned int)SYNC_PLL_INTERVAL) {
        syncPLL();
        lastSyncTime = currentTime;
    }
    onPulse(nullptr);  //! For testing, just call the pulse command callback directly to send a trigger and print values
    //delay(100);
  }

