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
    PKT_NO_DATA       = 1,   // Used just for initialization
    PKT_TRIGGER    = 2    // TX → RX  broadcast current timer value
};

// Packet structure between base station and ultrasonic transmitter
// 8 bytes, packet size is fixed
struct CommsPacket {
    PacketType type;
    uint8_t _pad[3] = {};
    uint16_t timestamp;
    uint16_t tick_delay;

    //! Constructors
    CommsPacket() : type(PKT_NO_DATA), timestamp(0), tick_delay(0) {}
    CommsPacket(PacketType rType, uint32_t rTime_ms)
        : type(rType), timestamp(rTime_ms), tick_delay(0) {}
    CommsPacket(PacketType rType, uint16_t rTime_ms, uint16_t rTick_delay)
        : type(rType), timestamp(rTime_ms), tick_delay(rTick_delay) {}
};

//! Microsecond time that the trigger signal will wait compared
//! to the trigger signal sent by the base station
//! THIS CAN BE MUCH SHORTER, IT IS POSSIBLE TO
//! OBTAIN TIME SYNC SIGNALS WITH U_S PRECISION
//! BUT THE DELAY IS REQUIRED FOR THE RECEIVERS TO
//! PROPERLY GO INTO THEIR MEASUREMENT CYCLE AND ASSERT THE ECHO PIN
//! So if this is too small you get a dead zone around the receivers
constexpr uint32_t LATENCY_COMPENSATION_US = 4000;

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
    RadioController(uint8_t ce_pin, uint8_t csn_pin);

    bool init();
    bool syncReceiver(uint8_t max_attempts = 5);
    void handleRadioPacket(LedControl &leds);

private:
    RF24    radio;
    bool _readPacket(CommsPacket &pkt_out);
};

#endif
