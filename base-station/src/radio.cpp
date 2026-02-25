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

    // TX writes to RX, listens on RX_TO_TX for sync traffic
    radio.openWritingPipe(PIPE_TX_TO_RX);
    radio.openReadingPipe(1, PIPE_RX_TO_TX);

    DEBUG_PRINTLN("[RADIO] OK.");
    radio.printDetails();
    return true;
}

// ---------------------------------------------------------------
// waitForSyncRequest()
//
// Blocks until receiver sends PKT_SYNC_REQUEST.
// Returns false on timeout.
// ---------------------------------------------------------------
bool RadioController::waitForSyncRequest(uint32_t timeout_ms) {
    DEBUG_PRINTLN("[SYNC] Waiting for sync request from receiver...");

    CommsPacket pkt = CommsPacket(PKT_NO_DATA, 0);
    radio.startListening();

    uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline) {
        if (!radio.available()) continue;

        radio.read(&pkt, sizeof(pkt));

        if (pkt.type != PKT_SYNC_REQUEST) {
            DEBUG_INFO_PRINTF("[SYNC] Ignoring unexpected packet type: %d\n", pkt.type);
            continue;
        }

        DEBUG_PRINTLN("[SYNC] Sync request received.");
        radio.stopListening();
        return true;
    }

    DEBUG_PRINTLN("[SYNC] Timed out waiting for sync request.");
    radio.stopListening();
    return false;
}

// ---------------------------------------------------------------
// doSyncHandshake()
//
// Called after sync request is received:
//   1. Send ping with TX timestamp
//   2. Wait for echo
//   3. Compute one_way_us = RTT / 2
//   4. Send confirm with one_way_us
//
// Returns false on any failure.
// ---------------------------------------------------------------
bool RadioController::doSyncHandshake(uint32_t timeout_ms) {
    DEBUG_PRINTLN("[SYNC] Starting handshake...");

    // Give receiver guaranteed time to get back into RX mode
    // after it finishes writing the sync request
    delay(50);

    CommsPacket ping = CommsPacket(PKT_SYNC_PING, 0);
    ping.timestamp = micros();
    uint32_t t_send = micros();

    radio.stopListening();
    delayMicroseconds(130);

    DEBUG_PRINTLN("[SYNC] Sending ping...");
    if (!radio.write(&ping, sizeof(ping))) {
        DEBUG_INFO_PRINTLN("[SYNC] Ping TX failed.");
        radio.startListening();
        return false;
    }

    DEBUG_PRINTLN("[SYNC] Ping sent, switching to RX for echo...");
    radio.startListening();
    delayMicroseconds(130);

    // Wait for echo
    uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline) {
        if (!radio.available()) continue;

        uint32_t t_recv = micros();
        CommsPacket echo = CommsPacket(PKT_NO_DATA, 0);
        radio.read(&echo, sizeof(echo));

        if (echo.type != PKT_SYNC_ECHO) {
            DEBUG_INFO_PRINTF("[SYNC] Expected PKT_SYNC_ECHO(%d), ignoring.\n", PKT_SYNC_ECHO);
            continue;
        }

        if (echo.timestamp != ping.timestamp) {
            DEBUG_INFO_PRINTLN("[SYNC] Timestamp mismatch, ignoring.");
            continue;
        }

        uint32_t rtt        = t_recv - t_send;
        uint32_t one_way_us = rtt / 2;

        DEBUG_PRINTF("[SYNC] RTT: %lu us  One-way: %lu us\n", rtt, one_way_us);

        // Wait before sending confirm so receiver is back in RX
        delay(50);

        radio.stopListening();
        delayMicroseconds(130);

        CommsPacket confirm = CommsPacket(PKT_SYNC_CONFIRM, 0);
        confirm.timestamp = one_way_us;

        DEBUG_PRINTLN("[SYNC] Sending confirm...");
        if (!radio.write(&confirm, sizeof(confirm))) {
            DEBUG_INFO_PRINTLN("[SYNC] Confirm TX failed.");
            radio.startListening();
            return false;
        }

        DEBUG_PRINTLN("[SYNC] Handshake complete.");
        radio.startListening();
        return true;
    }

    DEBUG_PRINTLN("[SYNC] Echo timed out.");
    radio.stopListening();
    return false;
}

// ---------------------------------------------------------------
// syncTransmitter()
//
// Top-level sync function — call on startup or re-sync.
// Blocks until sync succeeds or max attempts exceeded.
// ---------------------------------------------------------------
bool RadioController::syncTransmitter(uint8_t max_attempts, uint32_t request_timeout_ms) {
    for (uint8_t attempt = 1; attempt <= max_attempts; attempt++) {
        DEBUG_PRINTF("[SYNC] Attempt %u of %u\n", attempt, max_attempts);

        if (!waitForSyncRequest(request_timeout_ms)) continue;
        if (!doSyncHandshake()) continue;

        synced = true;
        return true;
    }

    DEBUG_PRINTLN("[SYNC] All attempts failed.");
    return false;
}

// ---------------------------------------------------------------
// sendTrigger()
// ---------------------------------------------------------------
bool RadioController::sendTrigger() {
    digitalWrite(trigger_pin, HIGH);
    uint32_t fire_time = micros();

    CommsPacket pkt = CommsPacket(PKT_TRIGGER, 0);
    pkt.timestamp = fire_time;

    radio.stopListening();
    bool ok = radio.write(&pkt, sizeof(pkt));

    uint32_t elapsed = micros() - fire_time;
    if (elapsed < 10) delayMicroseconds(10 - elapsed);

    digitalWrite(trigger_pin, LOW);

    if (!ok) DEBUG_INFO_PRINTLN("[TX] Trigger packet failed.");
    return ok;
}

// ---------------------------------------------------------------
// listenForSyncRequests()
// ---------------------------------------------------------------
bool RadioController::listenForSyncRequests() {
    // Re-sync if receiver requests it mid-operation
    radio.startListening();
    CommsPacket pkt = CommsPacket(PKT_NO_DATA, 0);

    if (radio.available()) {
        radio.read(&pkt, sizeof(pkt));
        if (pkt.type == PKT_SYNC_REQUEST) {
            DEBUG_INFO_PRINTLN("[TX] Re-sync requested by receiver.");
            synced = false;
            if (!doSyncHandshake()) {
                DEBUG_INFO_PRINTLN("[TX] Re-sync failed.");
            } else {
                synced = true;
                DEBUG_INFO_PRINTLN("[TX] Re-sync complete.");
            }
        }
        radio.stopListening();
    }

    return synced;
}
