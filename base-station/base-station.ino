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
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(9, 10); // CE, CSN

const byte address[6] = "00001";

const int ECHO_PIN[]={A0, A1, A2, A3, A4, A5};
const int RECV_CNT=6;

#define TRIGGER_PIN (PIN5)
#define echo_value() (PINC & 63)

void setup() 
{
  unsigned int i;
  
  Serial.begin(19200);  // tbd probably want faster
  pinMode(TRIGGER_PIN, OUTPUT);

  for(i=0;i<sizeof(ECHO_PIN)/sizeof(ECHO_PIN[0]);i++)
  {
    //pinMode(ECHO_PIN[i], INPUT_PULLUP);
    pinMode(ECHO_PIN[i], INPUT_PULLUP);
  }

  if(!radio.begin()){
    while(true){
      Serial.println("Radio is hardware responding...");
    }
  };
  radio.openWritingPipe(address);
  radio.setPALevel(RF24_PA_HIGH);
  radio.stopListening();
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

    delay(10);
    noInterrupts();
       digitalWrite(TRIGGER_PIN, HIGH); 
       digitalWrite(TRIGGER_PIN, HIGH);
       digitalWrite(TRIGGER_PIN, HIGH);
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
 
 const char message[] = "P";
    bool success = radio.write(&message, sizeof(message));  // Send message to TX to start pinging
    //delay(100);

    /*
    if (success) {
        Serial.println("Message sent successfully!");
    } else {
        Serial.println("Message failed to send!");
    }     
    */
   
    


    //delay(200);

      measure_echo_times();
  
 //Serial.print("2000, 7000, ");
/*for (int i= 0; i<nwp; i++)
{ Serial.print(ticks[i]/10000); Serial.print(", ");
 Serial.println(value[i]);
 }
 
 Serial.println("0,0 \n 0,0 \n 0,0");*/
 Serial.print(this_ticks[0]);   Serial.print(", ");
 Serial.print(this_ticks[1]);   Serial.print(", ");
 Serial.print(this_ticks[2]);   Serial.print(", ");
 Serial.print(this_ticks[3]);   Serial.print(", ");
 Serial.print(this_ticks[4]);   Serial.print(", ");
 Serial.println(this_ticks[5]); 
}


///////////////////////////



