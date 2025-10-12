#include <SPI.h>
//! https://nrf24.github.io/RF24/ 
//! Library used
#include <nRF24L01.h>
#include <RF24.h>
#include <RF24_config.h>
#include <printf.h>

//! Transmitter Station Parameters
constexpr uint8_t CE_PIN = 9;  
constexpr uint8_t CSN_PIN = 10;
constexpr uint8_t TRIGGER_PIN = 5;
constexpr char*   STATION_ID = "A1";                  //! A null-terminated string identifying this transmitter station
constexpr unsigned int TRIGGER_HIGH_DURATION = 300;   //! How long to hold the high state of the trigger pin

//! Tuning Parameters
constexpr unsigned int RECEIVE_DELAY = 10130;         //! Microsecond tuning delay after receiving ping message (MUST BE CALIBRATED) 

RF24 radio(CE_PIN, CSN_PIN); // CE, CSN
const byte address[6] = "abcde";

//! Debug flag
//! Only uncomment if debugging the arduino program
//#define DEBUG

void setup() {

  Serial.begin(19200);
  printf_begin();
  Serial.println("Started");
  
  pinMode(TRIGGER_PIN, OUTPUT);

  while(!radio.begin()){
    //! If radio fails to start, flash the built-in LED
    //! and print an error message
      Serial.println("Radio hardware not responding");
  }

  //! Setup the radio parameters
  radio.setPALevel(RF24_PA_LOW);
  radio.openReadingPipe(0, address);
  radio.startListening();  // Start in RX mode
  
  //! Print the radio details
  radio.printDetails();
  delay(1000);
}

void loop() {

  //! If the ping command was received
  //! TODO: Maybe make this an ISR?
  if (radio.available()) {
      char receivedMessage[32] = "";
      radio.read(&receivedMessage, sizeof(receivedMessage));
      //radio.flush_rx();
      //! Prevent interrupts to minimize chance of anything interferring with 
      //! time-critical functions (trigger pin)
      noInterrupts();

      //! This delay must exist since the radio.available function
      //! returns RIGHT AFTER it receives a message, while
      //! the base-station's radio.write function WAITS for the
      //! ACK packet from the transmitter station. 
      //! In short, this causes the trigger pin in the transmitter station 
      //! to trigger before the base station trigger pin, this delay 
      //! synchronizes both signals.
      delayMicroseconds(RECEIVE_DELAY);

      #ifdef DEBUG
      Serial.print("Received: ");
      Serial.println(receivedMessage);
      #endif
      noInterrupts();
      //! Trigger if the station id in the message matches our
      //! station ID. It will only match up to 3 characters
      if (strncmp(receivedMessage, STATION_ID, 3) == 0) {
        digitalWrite(TRIGGER_PIN, HIGH);
        digitalWrite(TRIGGER_PIN, HIGH);
        digitalWrite(TRIGGER_PIN, HIGH);
        delayMicroseconds(TRIGGER_HIGH_DURATION);
        digitalWrite(TRIGGER_PIN, LOW);
      }
      //! Allow interrupts
      interrupts();
  }
}

