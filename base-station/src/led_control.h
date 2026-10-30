//! @file led_control.h
//! @brief LedControl: drives the base station's five status LEDs and remembers
//!        their state.

#pragma once
#include <Arduino.h>

//! @brief Controls all status LEDs on the base station PCB.
//!
//! Keeps a shadow copy of each LED's state, so the getters never need a
//! digitalRead() on an OUTPUT pin.
class LedControl {
public:
    //! @brief Pin numbers of the five LEDs.
    struct Pins {
        uint8_t stat1;        //!< STAT_1 LED.
        uint8_t stat2;        //!< STAT_2 LED.
        uint8_t stat3;        //!< STAT_3 LED.
        uint8_t pulseTrig;    //!< PULSE_TRIG LED.
        uint8_t radioStatus;  //!< RADIO_STATUS LED.
    };

    //! @brief Store the pin assignments only. Does not call pinMode(); init() does.
    //! @param stat1       Pin of the STAT_1 LED.
    //! @param stat2       Pin of the STAT_2 LED.
    //! @param stat3       Pin of the STAT_3 LED.
    //! @param pulseTrig   Pin of the PULSE_TRIG LED.
    //! @param radioStatus Pin of the RADIO_STATUS LED.
    LedControl(uint8_t stat1, uint8_t stat2, uint8_t stat3,
               uint8_t pulseTrig, uint8_t radioStatus);

    //! @brief Set all pins as OUTPUT and drive them LOW. Call from setup().
    //! @param animation true (the default) to then run startupAnimation(), which
    //!                  blocks for about 2.6 s; false to skip it.
    void init(bool animation = true);

    //! @name Setters
    //! Each one drives its LED's pin and records the state for the getters.
    //! @{

    //! @brief Turn the STAT_1 LED on or off.
    //! @param state true for on (pin HIGH), false for off (pin LOW).
    void setStat1(bool state);
    //! @brief Turn the STAT_2 LED on or off.
    //! @param state true for on (pin HIGH), false for off (pin LOW).
    void setStat2(bool state);
    //! @brief Turn the STAT_3 LED on or off.
    //! @param state true for on (pin HIGH), false for off (pin LOW).
    void setStat3(bool state);
    //! @brief Turn the PULSE_TRIG LED on or off.
    //! @param state true for on (pin HIGH), false for off (pin LOW).
    void setPulseTrig(bool state);
    //! @brief Turn the RADIO_STATUS LED on or off.
    //! @param state true for on (pin HIGH), false for off (pin LOW).
    void setRadioStatus(bool state);

    //! @}

    //! @name Getters
    //! Each one returns the last state written by its setter, from the shadow
    //! copy (false until that setter is first called).
    //! @{

    //! @brief Get the last state written to the STAT_1 LED.
    //! @return true if the LED was last turned on.
    bool getStat1()       const;
    //! @brief Get the last state written to the STAT_2 LED.
    //! @return true if the LED was last turned on.
    bool getStat2()       const;
    //! @brief Get the last state written to the STAT_3 LED.
    //! @return true if the LED was last turned on.
    bool getStat3()       const;
    //! @brief Get the last state written to the PULSE_TRIG LED.
    //! @return true if the LED was last turned on.
    bool getPulseTrig()   const;
    //! @brief Get the last state written to the RADIO_STATUS LED.
    //! @return true if the LED was last turned on.
    bool getRadioStatus() const;

    //! @}

private:
    Pins  _pins;  //!< Pin assignments given to the constructor.

    //! @name Shadow state
    //! Last state written to each LED (avoids digitalRead on OUTPUT pins).
    //! @{

    bool _stat1       = false;  //!< STAT_1 LED.
    bool _stat2       = false;  //!< STAT_2 LED.
    bool _stat3       = false;  //!< STAT_3 LED.
    bool _pulseTrig   = false;  //!< PULSE_TRIG LED.
    bool _radioStatus = false;  //!< RADIO_STATUS LED.

    //! @}

    //! @brief Fun little animation to run on startup: cycles through all LEDs in
    //!        a pattern.
    //!
    //! Run by init() when its animation argument is true. Blocks for about
    //! 2.6 s and leaves every LED off; the shadow state is not changed.
    void startupAnimation();
};
