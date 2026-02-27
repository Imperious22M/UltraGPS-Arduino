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
      trigger_pin(trigger_pin)
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

    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_HIGH);
    radio.setPayloadSize(8);
    radio.disableDynamicPayloads();
    radio.setAutoAck(false);
    radio.setRetries(0, 0);
    radio.setChannel(100);

    // Transmitter only ever receives from the base station
    radio.openWritingPipe(PIPE_RX_TO_TX);
    radio.openReadingPipe(1, PIPE_TX_TO_RX);

    // Enter RX mode immediately — no bidirectional handshake needed
    radio.startListening();

    DEBUG_PRINTLN("[RADIO] OK.");
    #ifdef DEBUG
    radio.printDetails();
    #endif
    return true;
}

// ---------------------------------------------------------------
// syncReceiver()
//
// One-way protocol: no handshake needed. The base station sends
// PKT_SYNC_PING packets; clock_offset_us is updated in
// handleRadioPacket() on receipt. Just return true immediately.
// ---------------------------------------------------------------
bool RadioController::syncReceiver(uint8_t max_attempts) {
    DEBUG_INFO_PRINTLN("[SYNC] Receiver ready. Waiting for pings from base station.");
    // Clock offset is set when the first PKT_SYNC_PING arrives in loop().
    return true;
}

// ---------------------------------------------------------------
// handleRadioPacket()
//
// Called from loop(). Drains the entire RX FIFO each call.
//
//   PKT_SYNC_PING  → update clock_offset_us (one-way clock sync)
//   PKT_TRIGGER    → fire ultrasonic pulse with latency compensation
//                    (debounced to one fire per 50 ms burst window)
// ---------------------------------------------------------------
void RadioController::handleRadioPacket(LedControl &leds) {
    // Run on IRQ flag or if the FIFO already has data (e.g. burst leftovers)
    if (!packet_ready && !radio.available()) return;

    packet_ready = false;
    uint32_t local_rx_time = irq_time;  // time of first packet in this batch

    leds.setPulseTrig(true);

    while (radio.available()) {
        CommsPacket pkt(PKT_NO_DATA, 0);
        radio.read(&pkt, sizeof(pkt));

        if (pkt.type == PKT_SYNC_PING) {
            // One-way clock sync: derive offset from this packet's timestamps.
            // clock_offset_us converts a base-station timestamp to the local
            // equivalent of that moment.
            clock_offset_us = (int32_t)local_rx_time - (int32_t)pkt.timestamp;
            synced = true;
            DEBUG_INFO_PRINTF("[SYNC] clock_offset_us=%ld\n", clock_offset_us);

        } else if (pkt.type == PKT_TRIGGER && synced) {
            // Debounce: only fire once per 50 ms burst window
            static uint32_t last_trigger_ms = 0;
            uint32_t now_ms = millis();
            if (now_ms - last_trigger_ms >= 50) {
                last_trigger_ms = now_ms;

                // Capture fresh receive time here — local_rx_time is the IRQ time of the
                // *first* packet in this batch (possibly a ping), so it would give a wrong
                // elapsed_us if pings and the trigger were drained in the same FIFO sweep.
                uint32_t trigger_rx_time = micros();
                uint32_t local_tx_time   = (uint32_t)((int32_t)pkt.timestamp + clock_offset_us);
                uint32_t elapsed_us      = local_rx_time - local_tx_time;
                //Serial.print("[TRIGGER] Packet received. ");
                //Serial.print("Base station sent at ");
                //Serial.print(pkt.timestamp/1000);
                //Serial.print(" ms, local rx time is ");
                //Serial.print(trigger_rx_time/1000);
                //Serial.print(" ms, elapsed is ");
                //Serial.print(elapsed_us/1000);
                //Serial.println(" us.");
                //Serial.println(local_tx_time/1000);

                if (elapsed_us < LATENCY_COMPENSATION_US) {
                    delayMicroseconds(LATENCY_COMPENSATION_US - elapsed_us);
                    DEBUG_INFO_PRINTF("[TRIGGER] Elapsed=%lu us, waiting %lu us\n",
                                      elapsed_us, LATENCY_COMPENSATION_US - elapsed_us);
                    Serial.print("[TRIGGER] DELAYED!");
                }
                
                //! Manual delay
                delay(1);
                digitalWrite(trigger_pin, HIGH);
                delayMicroseconds(300);
                digitalWrite(trigger_pin, LOW);
                DEBUG_INFO_PRINTLN("[TRIGGER] Fired.");
            }
            // Continue draining FIFO to discard duplicate burst packets
        }
    }

    leds.setPulseTrig(false);
}

// ---------------------------------------------------------------
// onRadioIRQ()
//
// Radio IRQ handler — set packet_ready and irq_time for loop() to handle.
// ---------------------------------------------------------------
void onRadioIRQ() {
    irq_time     = micros();
    packet_ready = true;
}
