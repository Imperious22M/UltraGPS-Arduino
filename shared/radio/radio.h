#ifndef RADIO_H
#define RADIO_H

// ---------------------------------------------------------------
// Shared nRF24L01+ radio layer for the UltraGPS firmware.
//
// This file is compiled into BOTH the base station and the
// transmitter station (see shared/ and lib_extra_dirs in each
// platformio.ini). Pipe addresses, radio settings, packet framing
// and every send/receive function live here so the two boards
// cannot drift apart.
//
// Terminology (the project names are historical):
//   base station         -> radio MASTER, sends packets
//   transmitter station  -> radio SLAVE, receives packets and
//                           drives the ultrasonic transmitter
// ---------------------------------------------------------------

#include <Arduino.h>
#include <RF24.h>
#include "debug_macros.h"

// ---------------------------------------------------------------
// Radio link parameters — identical on both boards by construction
// ---------------------------------------------------------------
constexpr uint8_t        RADIO_CHANNEL      = 125;            // 0–125
constexpr rf24_datarate_e RADIO_DATA_RATE   = RF24_1MBPS;
constexpr rf24_pa_dbm_e  RADIO_PA_LEVEL     = RF24_PA_HIGH;

// ---------------------------------------------------------------
// Pipe addresses
// ---------------------------------------------------------------
//! base station -> transmitter station (trigger / sync / reset packets)
const uint64_t PIPE_BASE_TO_TRANSMITTER = 0xF0F0F0F0E1LL;
//! transmitter station -> base station (opened on both ends, currently unused)
const uint64_t PIPE_TRANSMITTER_TO_BASE = 0xF0F0F0F0E2LL;

// ---------------------------------------------------------------
// Which end of the link this board is
// ---------------------------------------------------------------
enum RadioRole : uint8_t {
    RADIO_ROLE_BASE        = 0,   //! base station: writes BASE->TRANSMITTER, reads TRANSMITTER->BASE
    RADIO_ROLE_TRANSMITTER = 1    //! transmitter station: the reverse, and listens continuously
};

// ---------------------------------------------------------------
// Packet types
// ---------------------------------------------------------------
enum PacketType : uint8_t {
    PKT_NO_DATA = 1,   //! Used just for initialization
    PKT_TRIGGER = 2,   //! base -> transmitter: fire a burst; carries Timer1 value + tick delay (> 0)
    PKT_RESET   = 3,   //! base -> transmitter: reset the transmitter station
    PKT_SYNC    = 4    //! base -> transmitter: PLL phase correction only; never touches the tick delay
};

//! Value carried in CommsPacket::timestamp of a PKT_RESET packet.
//! Guards against a corrupted packet being mistaken for a reset.
constexpr uint16_t RESET_MAGIC = 0xA55A;

//! How many times a reset packet is sent (no auto-ack on this link).
constexpr uint8_t RESET_PACKET_REPEATS = 3;

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

static_assert(sizeof(CommsPacket) == 8, "CommsPacket must be exactly 8 bytes");

//! Microsecond time that the trigger signal will wait compared
//! to the trigger signal sent by the base station
constexpr uint32_t LATENCY_COMPENSATION_US = 4000;

// ---------------------------------------------------------------
// RadioController
// ---------------------------------------------------------------
class RadioController {
public:
    RadioController(uint8_t ce_pin, uint8_t csn_pin);

    //! Configure the radio for the given end of the link.
    //! The transmitter station starts listening immediately.
    bool init(RadioRole role);

    // --- base station side -------------------------------------
    //! Send a measurement trigger carrying the local Timer1 value and the burst
    //! delay. tick_delay must be > 0; the transmitter station ignores a trigger
    //! with a zero delay (see sendTrigger() in radio.cpp).
    bool sendTrigger(uint16_t timer_val, uint16_t tick_delay);
    //! Send a PLL sync packet carrying the local Timer1 value. The transmitter
    //! station corrects its timer phase from it and leaves its tick delay alone.
    bool sendSync(uint16_t timer_val);
    //! Send a reset packet (repeated, since the link has no auto-ack).
    //! Returns true if at least one write succeeded.
    bool sendReset(uint8_t repeats = RESET_PACKET_REPEATS);

    // --- transmitter station side ------------------------------
    //! Pull the next packet out of the RX FIFO. Returns false if none.
    bool receivePacket(CommsPacket &pkt_out);
    //! Convenience: receive and accept only a PKT_TRIGGER.
    bool receiveTrigger(uint16_t &out_timer_val, uint16_t &out_tick_delay);
    //! True if the packet is a well-formed reset request.
    static bool isResetPacket(const CommsPacket &pkt);

    // --- both -------------------------------------------------
    void clearIRQFlags(bool &tx_ok, bool &tx_fail, bool &rx_ready);

private:
    RF24 radio;
    RadioRole _role;
    bool _writePacket(const CommsPacket &pkt);
    bool _readPacket(CommsPacket &pkt_out);
};

#endif
