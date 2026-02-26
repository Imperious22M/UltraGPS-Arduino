#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include "radio.h"

// ---------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------
RadioController::RadioController(uint8_t ce_pin, uint8_t csn_pin, uint8_t trigger_pin, uint8_t fail_threshold)
    : radio(ce_pin, csn_pin),
      synced(false),
      //consecutive_fails(0),
      trigger_pin(trigger_pin)
      //fail_threshold(fail_threshold)
{}

// ---------------------------------------------------------------
// init()
// ---------------------------------------------------------------
bool RadioController::init() {
    DEBUG_PRINTLN("[RADIO] Initializing...");
    pinMode(trigger_pin, OUTPUT);

    if (!radio.begin()) {
        DEBUG_INFO_PRINTLN("[RADIO] radio.begin() failed.");
        return false;
    }
    if (!radio.isChipConnected()) {
        DEBUG_INFO_PRINTLN("[RADIO] Chip not connected. Check wiring.");
        return false;
    }

    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_LOW);
    radio.setPayloadSize(8);
    radio.disableDynamicPayloads();
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setChannel(100);

    // RX listens on TX_TO_RX, writes on RX_TO_TX
    radio.openWritingPipe(PIPE_RX_TO_TX);
    radio.openReadingPipe(1, PIPE_TX_TO_RX);

    DEBUG_PRINTLN("[RADIO] OK.");
    radio.printDetails();
    return true;
}

// ---------------------------------------------------------------
// requestSync()
//
// Sends PKT_SYNC_REQUEST to transmitter to kick off the handshake.
// Always returns true due to auto-ack being disabled in radio settings
// ---------------------------------------------------------------
bool RadioController::requestSync() {
    Serial.println(F("[SYNC] Sending sync request..."));

    CommsPacket pkt = CommsPacket(PKT_SYNC_REQUEST,0);
    radio.stopListening();
    delayMicroseconds(130);

    radio.write(&pkt, sizeof(pkt));
    // Switch to RX immediately and stay there —
    // the transmitter will wait 50ms before sending ping
    // giving us guaranteed time to get back into listen mode
    radio.startListening();
    delayMicroseconds(130);
    Serial.println(F("[SYNC] Listening for ping..."));
    return true;
}

// ---------------------------------------------------------------
// doSyncHandshake()
//
// Called after request is sent:
//   1. Wait for ping from transmitter
//   2. Echo it back
//   3. Wait for confirm with one_way_us
//   4. Compute clock_offset_us
// ---------------------------------------------------------------
bool RadioController::doSyncHandshake(uint32_t timeout_ms) {
    // Radio is already in startListening() from requestSync() — don't touch it

    bool got_ping = false;
    uint32_t stashed_rx_time = 0;
    uint32_t stashed_tx_time = 0;
    CommsPacket pkt = CommsPacket(PKT_NO_DATA,0);

    // --- Wait for ping ---
    uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline && !got_ping) {
        if (!radio.available()) continue;

        uint32_t local_rx_time = micros();
        // Read ping packer
        radio.read(&pkt, sizeof(pkt));

        if (pkt.type != PKT_SYNC_PING) {
            Serial.print(F("[SYNC] Expected PKT_SYNC_PING("));
            Serial.print(PKT_SYNC_PING);
            Serial.println(F("), ignoring."));
            continue;
        }
        // Save RX (local) and TX (transmitter) time
        stashed_rx_time = local_rx_time;
        stashed_tx_time = pkt.timestamp; //! Contains the TX timestamp
        got_ping = true;

        Serial.print(F("[SYNC] Ping OK. tx_timestamp_us="));
        Serial.println(pkt.timestamp);

        // Switch to TX to echo — transmitter is now in RX waiting for echo
        radio.stopListening();
        delayMicroseconds(130);

        //! Change packet type and send echo packet        
        Serial.println(F("[SYNC] Sending echo..."));
        pkt.type = PKT_SYNC_ECHO;
        radio.write(&pkt, sizeof(pkt));

        // Back to receive mode for confirm — transmitter waits 50ms before sending it
        radio.startListening();
        delayMicroseconds(130);
        Serial.println(F("[SYNC] Echo sent. Listening for confirm..."));
    }

    if (!got_ping) {
        Serial.println(F("[SYNC] Timed out waiting for ping."));
        return false;
    }

    // --- Wait for confirm ---
    // Confirm packet has round trip time (RTT)/2 -> single trip time
    uint32_t confirm_deadline = millis() + timeout_ms;
    while (millis() < confirm_deadline) {
        if (!radio.available()) continue;

        //uint8_t buf[8] = {};
        radio.read(&pkt, sizeof(pkt));

        if (pkt.type != PKT_SYNC_CONFIRM) {
            Serial.print(F("[SYNC] Expected PKT_SYNC_CONFIRM("));
            Serial.print(PKT_SYNC_CONFIRM);
            Serial.println(F("), ignoring."));
            continue;
        }

        // Calculate clock offset from TX clock to local clock
        // accounting for single trip time
        clock_offset_us = (int32_t)(stashed_rx_time)
                        - (int32_t)(stashed_tx_time + pkt.timestamp);

        Serial.print(F("[SYNC] one_way_us="));    Serial.println(pkt.timestamp);
        Serial.print(F("[SYNC] clock_offset_us=")); Serial.println(clock_offset_us);
        Serial.println(F("[SYNC] Sync complete."));

        radio.startListening();
        return true;
    }

    Serial.println(F("[SYNC] Confirm timed out."));
    return false;
}

// ---------------------------------------------------------------
// syncReceiver()
//
// Top-level sync function — call on startup or re-sync.
// Sends request then performs handshake.
// If max_attempts is zero, then it will try indefinetly
// ---------------------------------------------------------------
bool RadioController::syncReceiver(uint8_t max_attempts) {
    bool exit_flag = false;
    uint8_t attempt = 1;
    do{
        Serial.print(F("[SYNC] Attempt ")); 
        Serial.print(attempt);
        Serial.print(F(" of ")); 
        Serial.println(max_attempts);

        if(max_attempts!=0){
          attempt+=1;
        }
        if(max_attempts!=0 && attempt>max_attempts){
          exit_flag = true;
        }

        if (!requestSync()) continue;
        if (!doSyncHandshake()) continue;
        synced = true;
        return true;

    }while(!exit_flag);

    Serial.println(F("[SYNC] All attempts failed."));
    return false;
}

// ---------------------------------------------------------------
// handleTrigger()
// ---------------------------------------------------------------
void RadioController::handleTrigger() {
    if (!packet_ready || !synced) return;

    //! May need a call to noInterrupts() here if packet_ready and irq_time need to be read atomically, 
    //! but in testing it seems to work fine without it 
    //! something to keep in mind if weird behavior around the trigger is seen timing
    packet_ready = false;

    uint32_t local_rx_time = irq_time;

    if (!radio.available()) return;

    CommsPacket pkt = CommsPacket(PKT_TRIGGER,0);
    radio.read(&pkt, sizeof(pkt));

    if (pkt.type == PKT_SYNC_REQUEST) {
        // Shouldn't happen, but guard against stale packets
        return;
    }

    if (pkt.type != PKT_TRIGGER) return;

    uint32_t local_tx_time = (uint32_t)((int32_t)pkt.timestamp + clock_offset_us);
    uint32_t elapsed_us    = local_rx_time - local_tx_time;

    if (elapsed_us < LATENCY_COMPENSATION_US) {
        delayMicroseconds(LATENCY_COMPENSATION_US - elapsed_us);
    }

    digitalWrite(trigger_pin, HIGH);
    delayMicroseconds(300);
    digitalWrite(trigger_pin, LOW);
}

// ---------------------------------------------------------------
// onRadioIRQ()
//
// Radio IRQ handler — set packet_ready and irq_time for main loop to handle
// ---------------------------------------------------------------
void onRadioIRQ() {
    irq_time     = micros();
    packet_ready = true;
}