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
      consecutive_fails(0),
      trigger_pin(trigger_pin),
      fail_threshold(fail_threshold)
{}

// ---------------------------------------------------------------
// init()
// ---------------------------------------------------------------
bool RadioController::init() {
    DEBUG_PRINTLN("[RADIO] Initializing...");
    pinMode(trigger_pin, OUTPUT);

    if (!radio.begin()) {
        Serial.println(F("[RADIO] radio.begin() failed."));
        return false;
    }
    if (!radio.isChipConnected()) {
        Serial.println(F("[RADIO] Chip not connected. Check wiring."));
        return false;
    }

    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_HIGH);
    radio.setPayloadSize(8);
    radio.disableDynamicPayloads();
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setChannel(100);
    // Only assert IRQ pin on RX data received; mask TX-done and max-retries
    radio.maskIRQ(true, true, false);

    // Base station only ever transmits to the transmitter station
    radio.openWritingPipe(PIPE_TX_TO_RX);
    radio.openReadingPipe(1, PIPE_RX_TO_TX);

    DEBUG_PRINTLN("[RADIO] OK.");
    #ifdef DEBUG
    radio.printDetails();
    #endif
    return true;
}

// ---------------------------------------------------------------
// sendSyncPing()
//
// Sends PKT_SYNC_PING carrying the base-station's current clock
// value. No response is expected — the transmitter updates its
// clock offset on receipt. Sent 3× for reliability.
// ---------------------------------------------------------------
void RadioController::sendSyncPing() {
    CommsPacket pkt(PKT_SYNC_PING, 0);
    pkt.timestamp = micros();
    radio.stopListening();
    radio.write(&pkt, sizeof(pkt));
    radio.write(&pkt, sizeof(pkt));
    radio.write(&pkt, sizeof(pkt));
    DEBUG_PRINTLN("[SYNC] Ping sent.");
}

// ---------------------------------------------------------------
// syncTransmitter()
//
// Sends several sync pings on startup so the transmitter can
// establish its initial clock offset. No ACK needed — one-way.
// Always succeeds once the radio is initialised.
// ---------------------------------------------------------------
bool RadioController::syncTransmitter(uint8_t max_attempts, uint32_t request_timeout_ms) {
    DEBUG_PRINTLN("[SYNC] Sending initial sync pings (one-way)...");
    for (uint8_t i = 0; i < 5; i++) {
        sendSyncPing();
        delay(10);  // let transmitter drain FIFO between bursts
    }
    synced = true;
    DEBUG_PRINTLN("[SYNC] Initial sync pings sent.");
    return true;
}

// ---------------------------------------------------------------
// sendTrigger()
//
// 1. Sends a sync ping so the transmitter has a fresh clock offset.
// 2. Waits briefly for the transmitter to process the ping.
// 3. Fires the local trigger pin and sends PKT_TRIGGER 3× (burst).
// ---------------------------------------------------------------
bool RadioController::sendTrigger() {
    // Refresh clock sync immediately before trigger (one-way, no ACK)
    sendSyncPing();

    // Give the transmitter time to drain the ping FIFO before trigger packets arrive.
    // 3× ping burst at 1 Mbps takes ~750 µs; 5 ms gap gives the transmitter enough time
    // to fully process the ping FIFO (including any Serial debug output) before the
    // trigger packets arrive — avoiding both being drained in the same FIFO sweep.
    //delay(5);

    digitalWrite(trigger_pin, HIGH);
    uint32_t fire_time = micros();

    DEBUG_PRINTF("[TX] Trigger fired at %lu us\n", fire_time);

    CommsPacket pkt(PKT_TRIGGER, 0);
    pkt.timestamp = fire_time;

    // radio is already in TX mode from sendSyncPing()
    radio.write(&pkt, sizeof(pkt));
    radio.write(&pkt, sizeof(pkt));
    radio.write(&pkt, sizeof(pkt));

    uint32_t elapsed = micros() - fire_time;
    if (elapsed < 10) delayMicroseconds(10 - elapsed);

    digitalWrite(trigger_pin, LOW);
    return true;
}

// ---------------------------------------------------------------
// onRadioIRQ()
//
// Radio IRQ handler — captures packet arrival time.
// Used by doSyncHandshake() for accurate RTT measurement.
// ---------------------------------------------------------------
void onRadioIRQ() {
    irq_time     = micros();
    packet_ready = true;
}
