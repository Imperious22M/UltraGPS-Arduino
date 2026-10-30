//! @file radio.cpp
//! @brief RadioController implementation for the shared nRF24L01+ radio layer.
//!
//! The API is documented in radio.h.

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include "radio.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
RadioController::RadioController(uint8_t ce_pin, uint8_t csn_pin)
    : radio(ce_pin, csn_pin), _role(RADIO_ROLE_BASE)
{}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------
bool RadioController::init(RadioRole role) {
    _role = role;
    DEBUG_PRINTLN("[RADIO] Initializing...");

    if (!radio.begin()) {
        Serial.println(F("[RADIO] radio.begin() failed."));
        return false;
    }
    if (!radio.isChipConnected()) {
        Serial.println(F("[RADIO] Chip not connected. Check wiring."));
        return false;
    }

    radio.setDataRate(RADIO_DATA_RATE);
    radio.setPALevel(RADIO_PA_LEVEL);
    radio.setPayloadSize(sizeof(CommsPacket));
    radio.disableDynamicPayloads();
    // Deliberately unreliable: a retried trigger would arrive too late
    // to be useful, so a dropped packet is better than a late one.
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setChannel(RADIO_CHANNEL);
    // Only assert the IRQ pin on RX data received; mask TX-done and max-retries.
    radio.maskIRQ(true, true, false);

    if (_role == RADIO_ROLE_BASE) {
        // Base station transmits to the transmitter station
        radio.openWritingPipe(PIPE_BASE_TO_TRANSMITTER);
        radio.openReadingPipe(1, PIPE_TRANSMITTER_TO_BASE);
        radio.stopListening();
    } else {
        // Transmitter station listens to the base station
        radio.openWritingPipe(PIPE_TRANSMITTER_TO_BASE);
        radio.openReadingPipe(1, PIPE_BASE_TO_TRANSMITTER);
        radio.startListening();
    }

    DEBUG_PRINTLN("[RADIO] OK.");
    return true;
}

// ---------------------------------------------------------------------------
// sendTrigger()
//
// Refuses tick_delay == 0, which is reserved. What the delay means
// and its 16383 us limit are documented in radio.h.
// ---------------------------------------------------------------------------
bool RadioController::sendTrigger(uint16_t timer_val, uint16_t tick_delay) {
    DEBUG_INFO_PRINTF("[TX] Sending trigger with clock value: %u\n", timer_val);
    DEBUG_INFO_PRINTF("[TX] Tick delay: %u\n", tick_delay);

    if (tick_delay == 0) {
        DEBUG_INFO_PRINTLN("[TX] Trigger with zero delay would be ignored by the transmitter.");
        return false;
    }

    CommsPacket pkt(PKT_TRIGGER, timer_val, tick_delay);
    bool ok = _writePacket(pkt);

    if (!ok) DEBUG_INFO_PRINTLN("[TX] Trigger packet failed.");
    return ok;
}

// ---------------------------------------------------------------------------
// sendSync()
// ---------------------------------------------------------------------------
bool RadioController::sendSync(uint16_t timer_val) {
    CommsPacket pkt(PKT_SYNC, timer_val, (uint16_t)0);
    bool ok = _writePacket(pkt);

    if (!ok) DEBUG_PRINTLN("[TX] Sync packet failed.");
    return ok;
}

// ---------------------------------------------------------------------------
// sendReset()
// ---------------------------------------------------------------------------
bool RadioController::sendReset(uint8_t repeats) {
    DEBUG_INFO_PRINTLN("[TX] Sending reset packet.");

    CommsPacket pkt(PKT_RESET, RESET_MAGIC, (uint16_t)0);
    bool any_ok = false;
    for (uint8_t i = 0; i < repeats; i++) {
        if (_writePacket(pkt)) any_ok = true;
        // Give the receiver time to drain its FIFO between copies
        delay(2);
    }

    if (!any_ok) DEBUG_INFO_PRINTLN("[TX] Reset packet failed.");
    return any_ok;
}

// ---------------------------------------------------------------------------
// receivePacket()
// ---------------------------------------------------------------------------
bool RadioController::receivePacket(CommsPacket &pkt_out) {
    return _readPacket(pkt_out);
}

// ---------------------------------------------------------------------------
// receiveTrigger()
// ---------------------------------------------------------------------------
bool RadioController::receiveTrigger(uint16_t &out_timer_val, uint16_t &out_tick_delay) {
    CommsPacket pkt;

    if (!_readPacket(pkt)) return false;
    if (pkt.type != PKT_TRIGGER) return false;
    out_timer_val  = pkt.timestamp;
    out_tick_delay = pkt.tick_delay;

    return true;
}

// ---------------------------------------------------------------------------
// isResetPacket()
// ---------------------------------------------------------------------------
bool RadioController::sendRate(uint16_t timer1_top, uint8_t repeats) {
    DEBUG_INFO_PRINTF("[TX] Sending rate packet, TOP %u\n", timer1_top);
    CommsPacket pkt(PKT_RATE, timer1_top, (uint16_t)~timer1_top);
    bool any_ok = false;
    for (uint8_t i = 0; i < repeats; i++) {
        if (_writePacket(pkt)) any_ok = true;
        // Give the receiver time to drain its FIFO between copies
        delay(2);
    }
    return any_ok;
}

bool RadioController::isRatePacket(const CommsPacket &pkt) {
    return pkt.type == PKT_RATE
        && pkt.tick_delay == (uint16_t)~pkt.timestamp
        && timer1TopIsValid(pkt.timestamp);
}

bool RadioController::isResetPacket(const CommsPacket &pkt) {
    return pkt.type == PKT_RESET && pkt.timestamp == RESET_MAGIC;
}

// ---------------------------------------------------------------------------
// clearIRQFlags()
// ---------------------------------------------------------------------------
void RadioController::clearIRQFlags(bool &tx_ok, bool &tx_fail, bool &rx_ready) {
    radio.whatHappened(tx_ok, tx_fail, rx_ready);
}

// ---------------------------------------------------------------------------
// _writePacket()
// ---------------------------------------------------------------------------
bool RadioController::_writePacket(const CommsPacket &pkt) {
    radio.stopListening();
    bool ok = radio.write(&pkt, sizeof(pkt));
    // The transmitter station must go back to listening after a write;
    // the base station never listens, so leave it in standby.
    if (_role == RADIO_ROLE_TRANSMITTER) radio.startListening();
    return ok;
}

// ---------------------------------------------------------------------------
// _readPacket()
// ---------------------------------------------------------------------------
bool RadioController::_readPacket(CommsPacket &pkt_out) {
    if (!radio.available()) return false;
    radio.read(&pkt_out, sizeof(pkt_out));
    return true;
}
