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
    PKT_SYNC_REQUEST  = 0,   // RX → TX  "I want to sync"
    PKT_SYNC_PING     = 1,   // TX → RX  ping with TX timestamp
    PKT_SYNC_ECHO     = 2,   // RX → TX  echo ping back
    PKT_SYNC_CONFIRM  = 3,   // TX → RX  one-way latency result
    PKT_TRIGGER       = 4,   // TX → RX  fire trigger
    PKT_NO_DATA       = 5    // Used just for initialization
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

//! Microsecond time that the trigger signal will wait compared
//! to the trigger signal sent by the base station
const uint32_t LATENCY_COMPENSATION_US = 2000;

//! IRQ variables
volatile static bool     packet_ready = false;
volatile static uint32_t irq_time     = 0;
// IRQ function or radio
void onRadioIRQ();

// ---------------------------------------------------------------
// RadioController
// ---------------------------------------------------------------
class RadioController {
public:
    RadioController(uint8_t ce_pin, uint8_t csn_pin, uint8_t trigger_pin, uint8_t fail_threshold = 5);

    bool init();
    bool requestSync();
    bool doSyncHandshake(uint32_t timeout_ms = 30000);
    bool syncReceiver(uint8_t max_attempts = 5);
    void handleTrigger();

private:
    RF24    radio;
    bool    synced;
    uint8_t trigger_pin;
    int32_t  clock_offset_us = 0;
};

#endif
