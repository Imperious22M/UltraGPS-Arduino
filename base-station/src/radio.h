#ifndef RADIO_H
#define RADIO_H

#include <Arduino.h>
#include <RF24.h>
#include "debug_macros.h"

// ---------------------------------------------------------------
// Pipe addresses
// ---------------------------------------------------------------
const uint64_t PIPE_TX_TO_RX = 0xF0F0F0F0E1LL;  // transmitter → receiver
const uint64_t PIPE_RX_TO_TX = 0xF0F0F0F0E2LL;  // receiver   → transmitter

// ---------------------------------------------------------------
// Packet types
// ---------------------------------------------------------------
enum PacketType : uint8_t {
    PKT_SYNC_REQUEST  = 0,   // BS → TX  "I want to sync"
    PKT_SYNC_ACK      = 1,   // TX → BS  "request received, ready"
    PKT_SYNC_PING     = 2,   // BS → TX  ping with BS timestamp
    PKT_SYNC_ECHO     = 3,   // TX → BS  echo ping back
    PKT_SYNC_CONFIRM  = 4,   // BS → TX  one-way latency result
    PKT_TRIGGER       = 5,   // BS → TX  fire trigger
    PKT_NO_DATA       = 6    // Used just for initialization
};

// Packet structure between base station and ultrasonic transmitter
// 8 bytes, packet size is fixed
struct CommsPacket {
    PacketType type;
    uint8_t _pad[3] = {};
    uint32_t timestamp;

    CommsPacket(PacketType rType, uint32_t rTime_ms)
        : type(rType), timestamp(rTime_ms) {}
};

// ---------------------------------------------------------------
// RadioController
// ---------------------------------------------------------------
class RadioController {
public:
    RadioController(uint8_t ce_pin, uint8_t csn_pin, uint8_t trigger_pin, uint8_t fail_threshold = 5);

    bool init();
    bool sendSyncRequest(uint32_t timeout_ms = 30000);
    bool doSyncHandshake(uint32_t timeout_ms = 5000);
    bool syncTransmitter(uint8_t max_attempts = 5, uint32_t request_timeout_ms = 30000);
    bool sendTrigger();
    bool listenForSyncRequests();

private:
    RF24    radio;
    bool    synced;
    uint8_t consecutive_fails;
    uint8_t trigger_pin;
    uint8_t fail_threshold;
};

#endif
