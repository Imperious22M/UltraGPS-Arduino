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

RadioController radioCtrl(9, 10, 5);  //! CE, CSN, Trigger pins

//! Base Station Parameters
constexpr unsigned int TRIGGER_HIGH_DURATION = 300;     //! How long to hold the high state of the trigger pin (microseconds)
constexpr uint8_t ECHO_PIN[]={A0, A1, A2, A3, A4, A5};  //! List of all the receivers ultrasonic echo pins
constexpr struct {
    //! On the PCB these map from STAT_LED_1 to LED_5
    uint8_t STAT_1  = 3;
    uint8_t STAT_2 = 4;
    uint8_t STAT_3 = 6;
    uint8_t PULSE_TRIG = 7;     //! Ultrasonic Pulse (STAT_2)
    uint8_t RADIO_STATUS = 8;   //! Radio Conn Status (STAT_1)
    //! Number of status pins
    uint8_t COUNT = 5;
}STAT_LEDS_PIN;                                         //! List of all the pins for each status LED
//! Number of receivers being used
constexpr uint8_t RECV_CNT = sizeof(ECHO_PIN)/sizeof(ECHO_PIN[0]);
#define echo_value() (PINC & 63)                        //! Macro to isolate certain pins in PORTC
//! Serial Communication Parammters
constexpr unsigned int BAUD_RATE  = 38400;

//! Tuning Parameters

//! How long to wait between transmissions, if this is too short
//! the transmitter and base station transmit pins will fall out of sync
//! because of the time it takes for the base station to
//! wait for the ACK packet.
constexpr uint16_t  TRANSMIT_DELAY = 200;

//! Makes the measure_echo_times tick count loop
//! Exit as soon as all 0's are detected in the PINC
//! register (When all receivers have received)
//#define ECHO_EARLY_EXIT

void setup()
{
  //! Serial setup
  Serial.begin(BAUD_RATE);
  while (!Serial && millis() < 3000);
  DEBUG_PRINTLN("Started");
  //Serial.println("test");

  for(uint8_t i=0; i < RECV_CNT ;i++)
  {
    pinMode(ECHO_PIN[i], INPUT_PULLUP);
  }

  //! Initialize all status LED pins
  pinMode(STAT_LEDS_PIN.STAT_1, OUTPUT);
  pinMode(STAT_LEDS_PIN.STAT_2, OUTPUT);
  pinMode(STAT_LEDS_PIN.STAT_3, OUTPUT);
  pinMode(STAT_LEDS_PIN.PULSE_TRIG, OUTPUT);
  pinMode(STAT_LEDS_PIN.RADIO_STATUS, OUTPUT);

  // Initialze radio and synchronize transmitters clocks
  if (!radioCtrl.init()) {
    DEBUG_INFO_PRINTLN("[ERROR] Radio init failed. Halting.");
    while (true) {
      DEBUG_INFO_PRINTLN("[ERROR] Still halted.");
      digitalWrite(STAT_LEDS_PIN.RADIO_STATUS, HIGH);
      delay(1000);
      digitalWrite(STAT_LEDS_PIN.RADIO_STATUS, LOW);
      delay(1000);
    }
  }
  if (!radioCtrl.syncTransmitter()) {
    DEBUG_INFO_PRINTLN("[ERROR] Sync failed. Halting.");
    while (true) {
        DEBUG_INFO_PRINTLN("[ERROR] Still halted.");
        digitalWrite(STAT_LEDS_PIN.RADIO_STATUS, HIGH);
        delay(1000);
        digitalWrite(STAT_LEDS_PIN.RADIO_STATUS, LOW);
        delay(1000);
      }
  }

  //! Radio initialized successfully - keep RADIO_STATUS LED on
  digitalWrite(STAT_LEDS_PIN.RADIO_STATUS, HIGH);

  delay(1000);
}

//! Global variables for echo times
static byte value[100]={0};
static unsigned long ticks[100]={0};
static unsigned long this_ticks[6];
static unsigned long this_not_used_ticks[6];
unsigned int nwp;

void measure_echo_times() {
    register byte this_value, previous_value;
    register unsigned long my_ticks;
    unsigned int i;

    unsigned int max, j;
    byte bits;
    const char text[] = "Ping\n";

    nwp=0;
    this_value=previous_value=0;

    noInterrupts();

    //! This determines the "count" value for each sensor
    constexpr long max_tick_val = 0x31000;
    constexpr byte ALL_ECHOES = 63;  //! All 6 echo pins (bits 0-5)
    byte seen_all_high = 0;  //! Track if we've seen all echoes go HIGH

    for(my_ticks=0; my_ticks < max_tick_val; my_ticks++)
    {
      this_value = echo_value();
      if (this_value != previous_value)
      {
        value[nwp] = previous_value = this_value;
        ticks[nwp++] = my_ticks;

        //! Track when all echoes have gone HIGH
        if (this_value == ALL_ECHOES) {
          seen_all_high = 1;
        }

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
    }
    interrupts();
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


void loop()
{

  //! Only send a message if a ping is requested using the serial line
  if (Serial.available() > 0) {
    // Read the incoming byte
    char incomingByte = Serial.read();

    // Trigger a pulse only if 'P'  is received
    if (incomingByte == 'P') {
        // Send message to TX to start pinging
        //bool success = radio.write(TRANSMITTER_STATION_ID, sizeof(TRANSMITTER_STATION_ID));

        bool success = radioCtrl.sendTrigger();
        measure_echo_times(); //! Measure the echo times immediately after sending the trigger
        
        if(success) {
          digitalWrite(STAT_LEDS_PIN.PULSE_TRIG, HIGH);
        } else {
          digitalWrite(STAT_LEDS_PIN.PULSE_TRIG, LOW);
        }

        if(success) {
          DEBUG_PRINTLN("Message sent successfully!");
        } else {
          DEBUG_PRINTLN("Message failed to send!");
        }

        

        Serial.print(this_ticks[0]);   Serial.print(", ");
        Serial.print(this_ticks[1]);   Serial.print(", ");
        Serial.print(this_ticks[2]);   Serial.print(", ");
        Serial.print(this_ticks[3]);   Serial.print(", ");
        Serial.print(this_ticks[4]);   Serial.print(", ");
        Serial.println(this_ticks[5]);

        
        //! Delay to prevent the transmit station from being overwhelmed
        delay(TRANSMIT_DELAY);
    }
    //! Simulation mode triggered by 'S'
    else if (incomingByte == 'S') {
        DEBUG_PRINTLN("Simulation mode activated");

        //! Turn on pulse LED to mimic real ping
        digitalWrite(STAT_LEDS_PIN.PULSE_TRIG, HIGH);

        //! Generate simulated echo times
        simulate_echo_times();

        //! Output simulated values in same format as real measurements
        Serial.print(this_ticks[0]);   Serial.print(", ");
        Serial.print(this_ticks[1]);   Serial.print(", ");
        Serial.print(this_ticks[2]);   Serial.print(", ");
        Serial.print(this_ticks[3]);   Serial.print(", ");
        Serial.print(this_ticks[4]);   Serial.print(", ");
        Serial.println(this_ticks[5]);

        //! Turn off pulse LED
        digitalWrite(STAT_LEDS_PIN.PULSE_TRIG, LOW);

        //! Delay to match pulse mode timing
        delay(TRANSMIT_DELAY);
    }
    else {
      //! Turn off the pulse LED
      digitalWrite(STAT_LEDS_PIN.PULSE_TRIG, LOW);
    }
  }

}

///////////////////////////

