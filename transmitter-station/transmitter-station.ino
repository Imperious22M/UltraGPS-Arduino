#include <SPI.h>
//! https://nrf24.github.io/RF24/ 
//! Library used
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(7, 8); // CE, CSN
const byte address[6] = "00001";

void setup() {
  Serial.begin(19200);
  Serial.println("Started");
    pinMode(4, OUTPUT);
    pinMode(3, OUTPUT);

    if(!radio.begin()){
      while(true){
        Serial.println("Radio hardware not responding");
      }
    }
    radio.openReadingPipe(0, address);
    radio.setPALevel(RF24_PA_MIN);
    radio.startListening();  // Start in RX mode
}

void loop() {
//noInterrupts();

    if (radio.available()) {
        char receivedMessage[32] = "";
        radio.read(&receivedMessage, sizeof(receivedMessage));
        delayMicroseconds(1);

      Serial.print("Received: ");
        Serial.println(receivedMessage);

        // Triggering event
    //if (strcmp(receivedMessage, "Ping") == 0) {
            digitalWrite(4, HIGH);
            delayMicroseconds(875);
            digitalWrite(4, LOW);
        //}
        //delay(220); // Wait before sending response
        

    }
    
}

