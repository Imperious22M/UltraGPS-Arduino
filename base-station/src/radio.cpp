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

    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_HIGH);
    radio.setPayloadSize(8);
    radio.disableDynamicPayloads();
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setChannel(100);
    // Only assert IRQ pin on RX data received; mask TX-done and max-retries
    radio.maskIRQ(true, true, false);

    // Base station transmits to the receiver
    radio.openWritingPipe(PIPE_TX_TO_RX);
    radio.openReadingPipe(1, PIPE_RX_TO_TX);

    if (as_receiver) {
        radio.startListening();
    }

    DEBUG_PRINTLN("[RADIO] OK.");
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

    if (!ok) DEBUG_INFO_PRINTLN("[TX] Trigger packet failed.");
    return ok;
}

// ---------------------------------------------------------------
// receiveTrigger()
// ---------------------------------------------------------------
bool RadioController::receiveTrigger(uint16_t &out_timer_val, uint16_t &out_tick_delay) {
    CommsPacket pkt(PKT_NO_DATA, 0);

    if (!_readPacket(pkt)) return false;
    if (pkt.type != PKT_TRIGGER) return false;
    out_timer_val  = pkt.timestamp;
    out_tick_delay = pkt.tick_delay;

    return true;
}

// ---------------------------------------------------------------
// clearIRQFlags()
// ---------------------------------------------------------------
void RadioController::clearIRQFlags(bool &tx_ok, bool &tx_fail, bool &rx_ready) {
    radio.whatHappened(tx_ok, tx_fail, rx_ready);
}

// ---------------------------------------------------------------
// _readPacket()
// ---------------------------------------------------------------
bool RadioController::_readPacket(CommsPacket &pkt_out) {
    if (!radio.available()) return false;
    radio.read(&pkt_out, sizeof(pkt_out));
    return true;
}
