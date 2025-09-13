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


//#define DEBUG

const byte address[6] = "00001";

const int ECHO_PIN[]={A0, A1, A2, A3, A4, A5};
const int RECV_CNT=6;

#define TRIGGER_PIN (PIN4)
#define echo_value() (PINC & 63)

void setup() 
{
  unsigned int i;
  
  //Serial.begin(250000);  // tbd probably want faster
  Serial.begin(19200);  // tbd probably want faster
  pinMode(TRIGGER_PIN, OUTPUT);

  for(i=0;i<sizeof(ECHO_PIN)/sizeof(ECHO_PIN[0]);i++)
  {
    pinMode(ECHO_PIN[i], INPUT);
   // pinMode(ECHO_PIN[i], INPUT );
  }

  //radio.begin();
  //radio.openWritingPipe(address);
  //radio.setPALevel(RF24_PA_MIN);
  //radio.stopListening();
}

static byte value[100]={0}; 
static unsigned long ticks[100]={0};
static unsigned long this_ticks[6];
static unsigned long previous_ticks[6];

void loop() 
{ 
  register byte this_value, previous_value;
  register unsigned long my_ticks;
  unsigned int i;
  unsigned int nwp;
  unsigned int max, j;
  byte bits;
  const char text[] = "Ping\n";

  nwp=0; // n-working pin, what index to store the state of the echo pins and the count
  this_value=previous_value=0;

    //delay(120);
    noInterrupts();

      // Guarantees a KNOWN trigger delay/duration (what is the delay?)
       digitalWrite(TRIGGER_PIN, HIGH); 
       digitalWrite(TRIGGER_PIN, HIGH);
       digitalWrite(TRIGGER_PIN, HIGH);
       digitalWrite(TRIGGER_PIN, HIGH); 
       digitalWrite(TRIGGER_PIN, HIGH);
       digitalWrite(TRIGGER_PIN, HIGH);
       digitalWrite(TRIGGER_PIN, LOW); 
       digitalWrite(TRIGGER_PIN, LOW); 
       digitalWrite(TRIGGER_PIN, LOW); 
       digitalWrite(TRIGGER_PIN, LOW); 
       digitalWrite(TRIGGER_PIN, LOW); 
       digitalWrite(TRIGGER_PIN, LOW); 
 
       // Ticks is 1 time unit of however long the for loop takes to run ()
       // Is each for loop execution time known?
       for(my_ticks=0;my_ticks < 0x28000; my_ticks++)
       {
        // Reads the state of all the port C pins (pinc is maped to the BYTE value of all of port C pins)
        // If the state has changed in any echo pin, store the number of ticks passed.
         this_value=echo_value();
         // If A0-A5 have changed (DOUBLE CHECK)
         if (this_value != previous_value) 
         {
          // Store the current state of the echo pins and the tick count it corresponds to
          value[nwp]=previous_value = this_value;
          ticks[nwp++] = my_ticks;
          // If we have stored 100 elements/reads then break out of the loop
          if(nwp >= sizeof(value)/sizeof(value[0])) break;
         }
       }
    interrupts();
    
// Find the index that continas the max pin state value
// , aka all echo pins are "active" and store it
 for(max=i=0;i<nwp;i++)
 {
  if (value[i] >= value[max]) max = i;
 }


  // zero out ticks
 for(j=0;j<6;j++) this_ticks[j]=0; 


 for(i=max+1;i<nwp;i++)
 {
  // If the tick value is too large (timeout?) break
   if (ticks[i] > 66000*4) break;
   // Get which echo pin was activated
   bits = value[i-1]-value[i];
   
   // Go through all the echo pins
   // and find the tick count that matches that echo pin
   for(j=0;j<6;j++)
   {
    // Compare the echo pin that was triggered (using the change in the portc register value)
    // with the one-left-shift value of j to find if j is the correct value to the echo pin that was
    // triggered
    if (bits & 1<<j) this_ticks[j] = ticks[i];
   }
 }

// hold previous -- but skip single zero values -- and infinity (I hard coded 160000)
for(j=0;j<6;j++)
{
  // Skip single zero values in current read 
  if ((previous_ticks[j] !=0) && (this_ticks[j]  == 0))
  {
    this_ticks[j]=previous_ticks[j];
    previous_ticks[j] = 0;
    continue;
  }
  // skip "infinity" values in current read
  if ((previous_ticks[j] < 160000) && (this_ticks[j]  > 160000))
  {
    this_ticks[j]=previous_ticks[j];
    previous_ticks[j] = 160000;
    continue;
  }

  // Otherwise set the previous to the current val
  previous_ticks[j]=this_ticks[j];
}

 //Serial.print("2000, 7000, ");
 
 /*if (
        (this_ticks[0] == 0) &&
        (this_ticks[1] == 0) &&
        (this_ticks[2] == 0) &&
        (this_ticks[3] == 0) &&
        (this_ticks[4] == 0) &&
        (this_ticks[5] == 0)) return;
*/

#ifdef DEBUG
 //Serial.print("LLim:500, ");
 // Print A0 through A5 (index value corresponds to A-number)
 Serial.print("R0:");
 Serial.print(this_ticks[0]);   Serial.print(", "); // A0
 Serial.print("R1:");
 Serial.print(this_ticks[1]);   Serial.print(", "); // A1
 Serial.print("R2:");
 Serial.print(this_ticks[2]);   Serial.print(", "); // A1
 Serial.print("R3:");
 Serial.print(this_ticks[3]);   Serial.print(", ");// ..
 Serial.print("R4:");
 Serial.print(this_ticks[4]);   Serial.print(", ");
 Serial.print("R5:");
 Serial.println(this_ticks[5]); 
 //Serial.println(", HLim:8000");
 #else
 Serial.print(this_ticks[0]);   Serial.print(", "); // A0
 Serial.print(this_ticks[1]);   Serial.print(", "); // A1
 Serial.print(this_ticks[2]);   Serial.print(", "); // A1
 Serial.print(this_ticks[3]);   Serial.print(", ");// ..
 Serial.print(this_ticks[4]);   Serial.print(", ");
 Serial.println(this_ticks[5]); 
 #endif


}


///////////////////////////



