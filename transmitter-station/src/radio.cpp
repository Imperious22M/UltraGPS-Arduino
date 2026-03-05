#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include "radio.h"

// ---------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------
RadioController::RadioController(uint8_t ce_pin, uint8_t csn_pin)
    : radio(ce_pin, csn_pin)
{}

// ---------------------------------------------------------------
// init()
// ---------------------------------------------------------------
bool RadioController::init(bool as_receiver) {
    DEBUG_PRINTLN("[RADIO] Initializing...");

    if (!radio.begin()) {
        Serial.println(F("[RADIO] radio.begin() failed."));
        return false;
    }
    if (!radio.isChipConnected()) {
        Serial.println(F("[RADIO] Chip not connected. Check wiring."));
        return false;
    }

    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_LOW);
    radio.setPayloadSize(8);
    radio.disableDynamicPayloads();
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setChannel(100);

    if (as_receiver) {
        // let IRQ pin only trigger on "data_ready" event in RX mode
        radio.setStatusFlags(RF24_RX_DR);
        radio.openWritingPipe(PIPE_RX_TO_TX);
        radio.openReadingPipe(1, PIPE_TX_TO_RX);
    } else {
        //! Radio must not be in listening mode when in transmitter mode
        radio.stopListening();
        radio.openWritingPipe(PIPE_TX_TO_RX);
        radio.openReadingPipe(1, PIPE_RX_TO_TX);
    }

    DEBUG_PRINTLN("[RADIO] OK.");

    if (as_receiver) {
        radio.startListening();
    }
    return true;
}
// ---------------------------------------------------------------
// sendTrigger()
// ---------------------------------------------------------------
bool RadioController::sendTrigger(uint16_t timer_val, uint16_t tick_delay) {
    DEBUG_INFO_PRINTF("[TX] Sending trigger with clock value: %u\n", timer_val);
    DEBUG_INFO_PRINTF("[TX] Tick delay: %u\n", tick_delay);

    CommsPacket pkt = CommsPacket(PKT_TRIGGER, timer_val, tick_delay);

    radio.stopListening();
    bool ok = radio.write(&pkt, sizeof(pkt));

    if (!ok) DEBUG_INFO_PRINTLN("[TX] Sync trigger packet failed.");
    return ok;
}

//! Receives the trigger packet from the transmitter
bool RadioController::receiveTrigger(uint16_t &out_timer_val, uint16_t &out_tick_delay) {
    CommsPacket pkt(PKT_NO_DATA, 0);
    
    if (!_readPacket(pkt)) return false;
    if (pkt.type != PKT_TRIGGER) return false;
    out_timer_val = pkt.timestamp;
    out_tick_delay = pkt.tick_delay;

    return true;
}

// ---------------------------------------------------------------
// clearIRQFlags()
//
// Wraps RF24::whatHappened() to clear the radio's IRQ status register
// and report which event(s) fired. Safe to call from an ISR (SPI poll only).
// ---------------------------------------------------------------
void RadioController::clearIRQFlags(bool &tx_ok, bool &tx_fail, bool &rx_ready) {
    radio.whatHappened(tx_ok, tx_fail, rx_ready);
}

// ---------------------------------------------------------------
// readPacket()
//
// Reads one packet from the RX FIFO without changing listen state.
// Returns false if the FIFO is empty.
// Safe to call from an ISR (SPI poll only, no startListening/stopListening).
// ---------------------------------------------------------------
bool RadioController::_readPacket(CommsPacket &pkt_out) {
    if (!radio.available()) return false;
    radio.read(&pkt_out, sizeof(pkt_out));
    return true;
}

