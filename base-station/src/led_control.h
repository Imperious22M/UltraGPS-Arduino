#pragma once
#include <Arduino.h>

//! Controls all status LEDs on the base station PCB
class LedControl {
public:
    struct Pins {
        uint8_t stat1;
        uint8_t stat2;
        uint8_t stat3;
        uint8_t pulseTrig;
        uint8_t radioStatus;
    };

    //! Constructor — stores pin assignments only, does not call pinMode
    LedControl(uint8_t stat1, uint8_t stat2, uint8_t stat3,
               uint8_t pulseTrig, uint8_t radioStatus);

    //! Call from setup() — sets all pins as OUTPUT and drives them LOW
    void init(bool animation = true);

    //! Setters
    void setStat1(bool state);
    void setStat2(bool state);
    void setStat3(bool state);
    void setPulseTrig(bool state);
    void setRadioStatus(bool state);

    //! Getters — return last written state
    bool getStat1()       const;
    bool getStat2()       const;
    bool getStat3()       const;
    bool getPulseTrig()   const;
    bool getRadioStatus() const;

private:
    Pins  _pins;

    //! Shadow state (avoids digitalRead on OUTPUT pins)
    bool _stat1       = false;
    bool _stat2       = false;
    bool _stat3       = false;
    bool _pulseTrig   = false;
    bool _radioStatus = false;

    //! Fun little animation to run on startup — cycles through all LEDs in a pattern
    void startupAnimation();
};
