#include "led_control.h"

LedControl::LedControl(uint8_t stat1, uint8_t stat2, uint8_t stat3,
                       uint8_t pulseTrig, uint8_t radioStatus)
    : _pins{stat1, stat2, stat3, pulseTrig, radioStatus}
{}

void LedControl::init(bool animation) {
    pinMode(_pins.stat1,       OUTPUT);
    pinMode(_pins.stat2,       OUTPUT);
    pinMode(_pins.stat3,       OUTPUT);
    pinMode(_pins.pulseTrig,   OUTPUT);
    pinMode(_pins.radioStatus, OUTPUT);

    digitalWrite(_pins.stat1,       LOW);
    digitalWrite(_pins.stat2,       LOW);
    digitalWrite(_pins.stat3,       LOW);
    digitalWrite(_pins.pulseTrig,   LOW);
    digitalWrite(_pins.radioStatus, LOW);

    //! Do a small initiation routine with the LED's :)
    if(animation) {
        startupAnimation();
    }
}

// --- Setters ---

void LedControl::setStat1(bool state) {
    _stat1 = state;
    digitalWrite(_pins.stat1, state ? HIGH : LOW);
}

void LedControl::setStat2(bool state) {
    _stat2 = state;
    digitalWrite(_pins.stat2, state ? HIGH : LOW);
}

void LedControl::setStat3(bool state) {
    _stat3 = state;
    digitalWrite(_pins.stat3, state ? HIGH : LOW);
}

void LedControl::setPulseTrig(bool state) {
    _pulseTrig = state;
    digitalWrite(_pins.pulseTrig, state ? HIGH : LOW);
}

void LedControl::setRadioStatus(bool state) {
    _radioStatus = state;
    digitalWrite(_pins.radioStatus, state ? HIGH : LOW);
}

// --- Getters ---

bool LedControl::getStat1()       const { return _stat1; }
bool LedControl::getStat2()       const { return _stat2; }
bool LedControl::getStat3()       const { return _stat3; }
bool LedControl::getPulseTrig()   const { return _pulseTrig; }
bool LedControl::getRadioStatus() const { return _radioStatus; }

// --- Private methods ---
void LedControl::startupAnimation() {
    const uint8_t pins[] = {_pins.stat1, _pins.stat2, _pins.stat3, _pins.pulseTrig, _pins.radioStatus};
    const size_t num_pins = sizeof(pins) / sizeof(pins[0]);
    const uint16_t delay_ms = 80;

    //! Ramp up
    for (size_t j = 0; j < num_pins; j++) {
        digitalWrite(pins[j], HIGH);
        delay(delay_ms);
        digitalWrite(pins[j], LOW);
        delay(delay_ms);
    }
    //! Ramp down
    for (size_t j = num_pins; j > 0; j--) {
        digitalWrite(pins[j-1], HIGH);
        delay(delay_ms);
        digitalWrite(pins[j-1], LOW);
        delay(delay_ms);
    }
    //! Half Ramp up, leave center LED on
    for (size_t j = 0; j <= (num_pins / 2); j++) {
        digitalWrite(pins[j], HIGH);
        delay(delay_ms);
        if(j!=(num_pins / 2)) {
            digitalWrite(pins[j], LOW);
            delay(delay_ms);
        }
    }
    //! Final spread from center pulse outwards
    digitalWrite(pins[num_pins / 2], LOW);
    delay(delay_ms);
    for (size_t j = 0; j < (num_pins / 2)+1; j++) {
        digitalWrite(pins[(num_pins / 2)+j], HIGH);
        digitalWrite(pins[(num_pins / 2)-j], HIGH);
        delay(delay_ms);
        digitalWrite(pins[(num_pins / 2)+j], LOW);
        digitalWrite(pins[(num_pins / 2)-j], LOW);
        delay(delay_ms);
    }

}
