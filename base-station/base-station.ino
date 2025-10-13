/*
* C code running on an Arduino to generate matlab data -- units of the outputs is counts

example:
4404, 3226, 4063, 4041, 3133, 4109
::
::

In the above example values represent the time it takes for the sound to travel the distance 
to each of the six towers respectively.  The unit of time for the number is "counts" but will 
definitely have an Ax+B type of offset where x is the count value, A is the scaling factor 
from counts to distance (or time) and B is the scaling offset necessary to adjust for counts 
not starting exactly at zero.

These values are output by the Arduino serially at 250000 baud and thus can be recieved by
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

Since your Raspberry Pi project deals with sonar-based distances, "counts" could be 
used in time-of-flight (ToF) calculations, where the system counts clock cycles 
between the sonar pulse transmission and echo reception to determine distance.

*/

#include <SPI.h>
//! https://nrf24.github.io/RF24/ 
//! Library used
#include <nRF24L01.h>
#include <RF24.h>
#include <RF24_config.h>
#include <printf.h>

//! Base Station Parameters
constexpr uint8_t CE_PIN = 9;  
constexpr uint8_t CSN_PIN = 10;
constexpr uint8_t TRIGGER_PIN = 5;
constexpr char*   TRANSMITTER_STATION_ID = "A1";        //! Identifies which transmission station is being targetted
constexpr unsigned int TRIGGER_HIGH_DURATION = 300;     //! How long to hold the high state of the trigger pin
constexpr uint8_t ECHO_PIN[]={A0, A1, A2, A3, A4, A5};  //! List of all the receivers ultrasonic echo pins
//! Number of receivers being used
constexpr uint8_t RECV_CNT = sizeof(ECHO_PIN)/sizeof(ECHO_PIN[0]); 
#define echo_value() (PINC & 63)                        //! Macro to isolate certain pins in PORTC

//! Radio Parameters
constexpr byte  RADIO_ADDRESS[6] = "abcde";             //! Radio channel ID to use, this must match the ID used in the transmitter station

//! Tuning Parameters

//! How long to wait between transmissions, if this is too short
//! the transmitter and base station transmit pins will fall out of sync
//! because of the time it takes for the base station to
//! wait for the ACK packet.
constexpr uint16_t  TRANSMIT_DELAY = 200;             

//! Debuging Flag
//! Only uncomment if debugging the arduino program
//#define DEBUG

RF24 radio(CE_PIN, CSN_PIN); // CE, CSN

void setup() 
{
  
  Serial.begin(19200);
  printf_begin();
  #ifdef DEBUG
  Serial.println("Started");
  #endif

  pinMode(TRIGGER_PIN, OUTPUT);

  for(uint8_t i=0; i < RECV_CNT ;i++)
  {
    pinMode(ECHO_PIN[i], INPUT_PULLUP);
  }

  while(!radio.begin()){
      //! If radio fails to start, flash the built-in LED
      //! and print an error message
      Serial.println("Radio is not hardware responding...");
  };

  radio.setPALevel(RF24_PA_LOW);
  radio.stopListening();
  radio.openWritingPipe(RADIO_ADDRESS);
  
  #ifdef DEBUG
  //! Print the radio details
  radio.printDetails();
  #endif
  delay(1000);
}

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
       digitalWrite(TRIGGER_PIN, HIGH); 
       digitalWrite(TRIGGER_PIN, HIGH);
       digitalWrite(TRIGGER_PIN, HIGH);
       delayMicroseconds(TRIGGER_HIGH_DURATION);
       digitalWrite(TRIGGER_PIN, LOW); 
      
 
       for(my_ticks=0;my_ticks < 0x31000; my_ticks++)
       {
         this_value=echo_value();
         if (this_value != previous_value) 
         {
          value[nwp]=previous_value = this_value;
          ticks[nwp++] = my_ticks;
          if(nwp >= sizeof(value)/sizeof(value[0])) break;
         }
       }
    interrupts();
    

 for(max=i=0;i<nwp;i++)
 {
  if (value[i] >= value[max]) max = i;
 }

 for(j=0;j<6;j++) {this_not_used_ticks[j]=1; this_ticks[j]=0;} 

 //abits = 32+16+8+4+2+1; // 0x3F
 for(i=max+1;i<nwp;i++)
 {
   if (ticks[i] > 66000*4) break;
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


void loop() 
{ 
 
  bool success = radio.write(TRANSMITTER_STATION_ID, sizeof(TRANSMITTER_STATION_ID));  // Send message to TX to start pinging

  #ifdef DEBUG
  if (success) {
      Serial.println("Message sent successfully!");
  } else {
      Serial.println("Message failed to send!");
  }  
  #endif
  
  //! Measure the ultrasonic receive times
  measure_echo_times();
  
  Serial.print(this_ticks[0]);   Serial.print(", ");
  Serial.print(this_ticks[1]);   Serial.print(", ");
  Serial.print(this_ticks[2]);   Serial.print(", ");
  Serial.print(this_ticks[3]);   Serial.print(", ");
  Serial.print(this_ticks[4]);   Serial.print(", ");
  Serial.println(this_ticks[5]);   
  
  
  
  // Serial.println("0,0 \n 0,0 \n 0,0");*/


  //! Delay to prevent the transmit station from being overwhelmed
  delay(TRANSMIT_DELAY);
}


///////////////////////////

