//! @file radio.h
//! @brief Shared nRF24L01+ radio layer for the UltraGPS firmware.
//!
//! This library is compiled into BOTH the base station and the transmitter station (see
//! shared/ and lib_extra_dirs in each platformio.ini). Pipe addresses, radio settings, packet
//! framing and every send/receive function live here so the two boards cannot drift apart.
//! debug_macros.h is not part of the library: each project supplies its own from its src/
//! directory (build_flags = -I src), so the debug level stays per-project.
//!
//! Terminology (the project names are historical):
//! - base station -> radio MASTER, sends packets.
//! - transmitter station -> radio SLAVE (the receiver), receives packets and drives the
//!   ultrasonic transmitter.

#ifndef RADIO_H
#define RADIO_H

#include <Arduino.h>
#include <RF24.h>
#include "debug_macros.h"

// ---------------------------------------------------------------------------
// Radio link parameters — identical on both boards by construction
// ---------------------------------------------------------------------------

constexpr uint8_t        RADIO_CHANNEL      = 125;            //!< RF channel (0–125).
constexpr rf24_datarate_e RADIO_DATA_RATE   = RF24_1MBPS;     //!< On-air data rate.
constexpr rf24_pa_dbm_e  RADIO_PA_LEVEL     = RF24_PA_HIGH;   //!< Transmit power level.

// ---------------------------------------------------------------------------
// Pipe addresses
// ---------------------------------------------------------------------------

//! Pipe address for base station -> transmitter station (trigger / sync / reset packets).
const uint64_t PIPE_BASE_TO_TRANSMITTER = 0xF0F0F0F0E1LL;
//! Pipe address for transmitter station -> base station (opened on both ends, currently unused).
const uint64_t PIPE_TRANSMITTER_TO_BASE = 0xF0F0F0F0E2LL;

// ---------------------------------------------------------------------------
// Which end of the link this board is
// ---------------------------------------------------------------------------

//! @brief Which end of the radio link a board is; passed to RadioController::init().
enum RadioRole : uint8_t {
    RADIO_ROLE_BASE        = 0,   //!< Base station: writes BASE->TRANSMITTER, reads TRANSMITTER->BASE.
    RADIO_ROLE_TRANSMITTER = 1    //!< Transmitter station: the reverse, and listens continuously.
};

// ---------------------------------------------------------------------------
// Packet types
// ---------------------------------------------------------------------------

//! @brief Packet type, carried in CommsPacket::type.
//!
//! Every packet that is sent goes from the base station to the transmitter station.
enum PacketType : uint8_t {
    PKT_NO_DATA = 1,   //!< Used just for initialization (default-constructed packet); never sent.
    PKT_TRIGGER = 2,   //!< Measurement trigger: carries the Timer1 value + burst delay (> 0).
    PKT_RESET   = 3,   //!< Reset the transmitter station; timestamp must hold RESET_MAGIC.
    PKT_SYNC    = 4,   //!< PLL phase correction only (Timer1 value); never touches the burst delay.
    PKT_RATE    = 5    //!< New trigger rate: timestamp = Timer1 TOP, tick_delay = ~TOP. The transmitter stores it and resets.
};

//! @brief Value carried in CommsPacket::timestamp of a PKT_RESET packet.
//!
//! Guards against a corrupted packet being mistaken for a reset.
constexpr uint16_t RESET_MAGIC = 0xA55A;

//! @brief How many times a reset packet is sent (no auto-ack on this link).
//!
//! Default for the repeat count of RadioController::sendReset().
constexpr uint8_t RESET_PACKET_REPEATS = 3;

//! @name Trigger rate (Timer1 frequency), shared by both stations
//! @{

//! Timer1 counts per second: 16 MHz / prescaler 256 = 62500 (one count = 16 us).
constexpr uint32_t TIMER1_COUNTS_PER_S = F_CPU / 256UL;
constexpr uint8_t TRIGGER_RATE_MIN_HZ = 1;   //!< Slowest trigger rate (Timer1 TOP 62499).
constexpr uint8_t TRIGGER_RATE_MAX_HZ = 50;  //!< Fastest trigger rate (20 ms period).
constexpr uint8_t TRIGGER_RATE_HZ     = 10;  //!< Default trigger rate.

//! @}

//! @brief Timer1 TOP (OCR1A) value for a trigger rate.
//!
//! TOP = round(62500 / hz) - 1, so the period is (TOP + 1) x 16 us.
//! @param hz Trigger rate in Hz, TRIGGER_RATE_MIN_HZ to TRIGGER_RATE_MAX_HZ.
//! @return The TOP value.
constexpr uint16_t triggerRateToTop(uint8_t hz) {
    return (uint16_t)((TIMER1_COUNTS_PER_S + hz / 2) / hz - 1);
}

//! @brief True if @p top is a Timer1 TOP value within the allowed rate range.
//! @param top Timer1 TOP value.
constexpr bool timer1TopIsValid(uint16_t top) {
    return top >= triggerRateToTop(TRIGGER_RATE_MAX_HZ) && top <= triggerRateToTop(TRIGGER_RATE_MIN_HZ);
}

//! @brief Packet structure between the base station and the ultrasonic transmitter station.
//!
//! 8 bytes; the packet size is fixed (the radio is set to a fixed payload of
//! sizeof(CommsPacket), and the static_assert below checks the size).
//! What timestamp and tick_delay carry depends on the type:
//! - PKT_TRIGGER: the base station's Timer1 count (TCNT1) and the burst delay (> 0).
//! - PKT_SYNC: the base station's Timer1 count; tick_delay is 0 and ignored.
//! - PKT_RESET: RESET_MAGIC; tick_delay is 0 and ignored.
struct CommsPacket {
    PacketType type;          //!< What the packet is; decides what the other fields carry.
    uint8_t _pad[3] = {};     //!< Unused padding; zeroed by every constructor.
    uint16_t timestamp;       //!< Base station's Timer1 count (TCNT1), or RESET_MAGIC.
    uint16_t tick_delay;      //!< Burst delay in microseconds (not Timer1 ticks).

    // Constructors
    //! @brief Empty packet: type PKT_NO_DATA, timestamp and tick_delay 0.
    CommsPacket() : type(PKT_NO_DATA), timestamp(0), tick_delay(0) {}
    //! @brief Packet with no burst delay (tick_delay 0).
    //! @param rType    Packet type.
    //! @param rTime_ms Value for timestamp: a Timer1 count or RESET_MAGIC, not milliseconds
    //!                 despite the name. Only the low 16 bits are kept.
    CommsPacket(PacketType rType, uint32_t rTime_ms)
        : type(rType), timestamp(rTime_ms), tick_delay(0) {}
    //! @brief Packet with type, timestamp and tick_delay given.
    //! @param rType       Packet type.
    //! @param rTime_ms    Value for timestamp: a Timer1 count or RESET_MAGIC, not milliseconds
    //!                    despite the name.
    //! @param rTick_delay Value for tick_delay: the burst delay in microseconds, or 0 when the
    //!                    packet type does not use it.
    CommsPacket(PacketType rType, uint16_t rTime_ms, uint16_t rTick_delay)
        : type(rType), timestamp(rTime_ms), tick_delay(rTick_delay) {}
};

static_assert(sizeof(CommsPacket) == 8, "CommsPacket must be exactly 8 bytes");

//! @brief Microsecond time that the trigger signal will wait compared to the trigger signal
//!        sent by the base station.
//! @note Not used yet: nothing reads this constant (see "Half-built features" in
//!       docs/notes.md).
constexpr uint32_t LATENCY_COMPENSATION_US = 4000;

// ---------------------------------------------------------------------------
// RadioController
// ---------------------------------------------------------------------------

//! @brief The UltraGPS radio link: nRF24L01+ setup plus every packet send and receive.
//!
//! Construct it with the CE and CSN pins, then call init() with the board's RadioRole before
//! using any other method.
class RadioController {
public:
    //! @brief Store the CE and CSN pins. Does not touch the radio; init() does that.
    //! @param ce_pin  Arduino pin wired to the nRF24L01+ CE pin.
    //! @param csn_pin Arduino pin wired to the nRF24L01+ CSN (SPI chip-select) pin.
    RadioController(uint8_t ce_pin, uint8_t csn_pin);

    //! @brief Configure the radio for the given end of the link.
    //!
    //! Applies RADIO_CHANNEL, RADIO_DATA_RATE and RADIO_PA_LEVEL, a fixed payload of
    //! sizeof(CommsPacket), no auto-ack or retries, and an IRQ pin that only signals received
    //! data. The base station is left in standby (it never listens); the transmitter station
    //! starts listening immediately.
    //! @param role Which end of the link this board is.
    //! @return true if the radio is configured; false if RF24::begin() failed or the chip is
    //!         not connected (the reason is printed to Serial whatever the debug level).
    bool init(RadioRole role);

    // --- base station side -------------------------------------
    //! @brief Send a measurement trigger carrying the local Timer1 value and the burst delay.
    //!
    //! The transmitter station corrects its timer phase from @p timer_val (as for a sync
    //! packet) and stores @p tick_delay: the number of microseconds it waits after its Timer1
    //! compare match before firing the ultrasonic burst. The stored delay is used by every
    //! compare match until the next PKT_TRIGGER replaces it; PKT_SYNC packets do not touch it.
    //! @param timer_val  Local Timer1 count (TCNT1) to send.
    //! @param tick_delay Burst delay in microseconds, from 1 to 16383.
    //! @return true if the radio reported the packet as sent (with no auto-ack, that does not
    //!         mean it was received); false if it was not sent, including when @p tick_delay
    //!         is 0.
    //! @note A @p tick_delay of 0 is reserved: the transmitter station drops a trigger with a
    //!       zero delay without applying it, so that a stray or malformed packet can never
    //!       clear the delay in force. This function refuses to send one, so callers must
    //!       pass at least 1 us.
    //! @warning The transmitter station applies the delay with delayMicroseconds(), which is
    //!          limited to 16383 us; larger values overflow and give a shorter delay. This
    //!          function does not check the upper limit.
    bool sendTrigger(uint16_t timer_val, uint16_t tick_delay);
    //! @brief Send a PLL sync packet carrying the local Timer1 value.
    //!
    //! The transmitter station corrects its timer phase from it and leaves its burst delay
    //! alone (the packet's tick_delay is sent as 0).
    //! @param timer_val Local Timer1 count (TCNT1) to send.
    //! @return true if the radio reported the packet as sent (not proof that it was received).
    bool sendSync(uint16_t timer_val);
    //! @brief Send a reset packet (repeated, since the link has no auto-ack).
    //!
    //! Each copy is a PKT_RESET carrying RESET_MAGIC and is followed by a 2 ms pause that
    //! gives the receiver time to drain its FIFO, so the call blocks for at least 2 ms per
    //! copy.
    //! @param repeats Number of copies to send; 0 sends nothing.
    //! @return true if at least one write succeeded.
    bool sendReset(uint8_t repeats = RESET_PACKET_REPEATS);
    //! @brief Send a new trigger rate (Timer1 TOP) to the transmitter station.
    //!
    //! Sent @p repeats times like a reset, since the link has no auto-ack. A
    //! transmitter whose TOP differs stores the new one and resets itself.
    //! @param timer1_top New Timer1 TOP value (see triggerRateToTop()).
    //! @param repeats    Number of copies sent.
    //! @return true if at least one write succeeded.
    bool sendRate(uint16_t timer1_top, uint8_t repeats = RESET_PACKET_REPEATS);

    // --- transmitter station side ------------------------------
    //! @brief Pull the next packet out of the RX FIFO.
    //! @param pkt_out Receives the packet; left unchanged if none is waiting.
    //! @return true if a packet was read, false if none was waiting.
    bool receivePacket(CommsPacket &pkt_out);
    //! @brief Convenience: receive and accept only a PKT_TRIGGER.
    //!
    //! Reads one packet; a packet of any other type is consumed and dropped.
    //! @param out_timer_val  Set to the trigger's Timer1 value (timestamp).
    //! @param out_tick_delay Set to the trigger's burst delay in microseconds.
    //! @return true if a trigger was read; false if none was waiting or the packet was not a
    //!         trigger (the outputs are then left unchanged).
    //! @note Does not reject the reserved zero delay; check @p out_tick_delay before using it.
    bool receiveTrigger(uint16_t &out_timer_val, uint16_t &out_tick_delay);
    //! @brief Check whether a packet is a well-formed reset request.
    //! @param pkt Packet to check.
    //! @return true if @p pkt is a PKT_RESET whose timestamp holds RESET_MAGIC.
    static bool isResetPacket(const CommsPacket &pkt);
    //! @brief Check whether a packet is a well-formed trigger-rate packet.
    //! @param pkt Received packet.
    //! @return true if it is PKT_RATE, tick_delay == ~timestamp and the TOP is valid.
    static bool isRatePacket(const CommsPacket &pkt);

    // --- both -------------------------------------------------
    //! @brief Read and clear the radio's interrupt flags (RF24::whatHappened()).
    //!
    //! Clearing the flags releases the radio's IRQ pin.
    //! @param tx_ok    Set to whether a packet was sent (TX_DS).
    //! @param tx_fail  Set to whether a send failed after the maximum retries (MAX_RT).
    //! @param rx_ready Set to whether received data is waiting (RX_DR).
    void clearIRQFlags(bool &tx_ok, bool &tx_fail, bool &rx_ready);

private:
    RF24 radio;          //!< RF24 driver for the nRF24L01+ module.
    RadioRole _role;     //!< Which end of the link this board is; set by init().

    //! @brief Send one packet; the transmitter station then goes back to listening.
    //! @param pkt Packet to send.
    //! @return true if RF24::write() reported success.
    bool _writePacket(const CommsPacket &pkt);
    //! @brief Read the next packet from the RX FIFO, if one is waiting.
    //! @param pkt_out Receives the packet; left unchanged if none is waiting.
    //! @return true if a packet was read.
    bool _readPacket(CommsPacket &pkt_out);
};

#endif
