//! @file settings-eeprom.cpp
//! @brief SettingsEeprom implementation: loading and saving the EEPROM record,
//!        and the setters' range checks.
//!
//! The API is documented in settings-eeprom.h.

#include "settings-eeprom.h"
#include <EEPROM.h>
#include <util/crc16.h>

//! Marks a record written by this firmware (the old burst-delay record used 0xB0D5).
static constexpr uint16_t SETTINGS_MAGIC = 0x5E77;

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

SettingsEeprom::SettingsEeprom() {
    loadDefaults(_data);
}

bool SettingsEeprom::begin() {
    Header header;
    uint16_t stored_crc;
    EEPROM.get(SETTINGS_EEPROM_ADDR, header);
    EEPROM.get(DATA_ADDR, _data);
    EEPROM.get(CRC_ADDR, stored_crc);

    if (header.magic == SETTINGS_MAGIC
        && header.version == SETTINGS_LAYOUT_VERSION
        && header.size == sizeof(Data)
        && stored_crc == crc(_data)
        && isValid(_data)) {
        return true;
    }

    // No valid record: first boot, a record from other firmware or with another
    // layout version or size, a corrupted record or one holding an out-of-range
    // value. Store and use the defaults.
    loadDefaults(_data);
    save();
    return false;
}

void SettingsEeprom::resetToDefaults() {
    const uint32_t old_baud = _data.baudRate;
    loadDefaults(_data);
    save();
    if (_data.baudRate != old_baud) {
        applyBaudRate(_data.baudRate);
    }
}

void SettingsEeprom::loadDefaults(Data &data) {
    data.triggerHighDuration = TRIGGER_HIGH_DURATION;
    for (uint8_t i = 0; i < RECV_CNT; i++) {
        data.echoPin[i] = ECHO_PIN[i];
    }
    data.recvCnt                   = RECV_CNT;
    data.baudRate                  = BAUD_RATE;
    data.syncPllInterval           = SYNC_PLL_INTERVAL;
    data.burstDelayUs              = SONIC_BURST_DELAY;
    data.triggerSendGuardTicks     = TRIGGER_SEND_GUARD_TICKS;
    data.searchDelayMinUs          = SEARCH_DELAY_MIN_US;
    data.searchDelayMaxUs          = SEARCH_DELAY_MAX_US;
    data.searchDelayStepUs         = SEARCH_DELAY_STEP_US;
    data.searchSamplesPerStep      = SEARCH_SAMPLES_PER_STEP;
    data.searchPrintSteps          = SEARCH_PRINT_STEPS;
    data.resetSelfAfterRemoteReset = RESET_SELF_AFTER_REMOTE_RESET;
    data.resetSelfDelayMs          = RESET_SELF_DELAY_MS;
    data.samplingTimeMs            = SAMPLING_TIME_MS;
    data.maxSavedTicks             = MAX_SAVED_TICKS;
}

bool SettingsEeprom::isValid(const Data &data) {
    if (data.recvCnt != RECV_CNT) return false;
    for (uint8_t i = 0; i < RECV_CNT; i++) {
        if (!echoPinIsValid(data.echoPin[i])) return false;
    }
    return data.triggerHighDuration >= TRIGGER_HIGH_DURATION_MIN
        && data.triggerHighDuration <= TRIGGER_HIGH_DURATION_MAX
        && isValidBaudRate(data.baudRate)
        && data.syncPllInterval >= SYNC_PLL_INTERVAL_MIN
        && data.syncPllInterval <= SYNC_PLL_INTERVAL_MAX
        && data.burstDelayUs >= BURST_DELAY_MIN_US
        && data.burstDelayUs <= BURST_DELAY_MAX_US
        && data.triggerSendGuardTicks <= TRIGGER_SEND_GUARD_MAX_TICKS
        && data.searchDelayMinUs <= BURST_DELAY_MAX_US
        && data.searchDelayMaxUs <= BURST_DELAY_MAX_US
        && data.searchDelayStepUs >= 1
        && data.searchDelayStepUs <= BURST_DELAY_MAX_US
        && data.searchSamplesPerStep >= 1
        && data.searchPrintSteps <= 1
        && data.resetSelfAfterRemoteReset <= 1
        && data.samplingTimeMs >= SAMPLING_TIME_MIN_MS
        && data.samplingTimeMs <= SAMPLING_TIME_MAX_MS
        && data.maxSavedTicks >= MAX_SAVED_TICKS_MIN
        && data.maxSavedTicks <= SAMPLE_BUFFER_SIZE;
}

uint16_t SettingsEeprom::crc(const Data &data) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&data);
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < sizeof(data); i++) {
        crc = _crc16_update(crc, bytes[i]);
    }
    return crc;
}

void SettingsEeprom::save() const {
    const Header header = { SETTINGS_MAGIC, SETTINGS_LAYOUT_VERSION, (uint8_t)sizeof(Data) };
    const uint16_t data_crc = crc(_data);
    // put() only writes the bytes that differ
    EEPROM.put(SETTINGS_EEPROM_ADDR, header);
    EEPROM.put(DATA_ADDR, _data);
    EEPROM.put(CRC_ADDR, data_crc);
}

template <typename T>
bool SettingsEeprom::store(T &field, T value, bool valid) {
    if (!valid) return false;
    field = value;
    save();
    return true;
}

void SettingsEeprom::applyBaudRate(uint32_t baud) {
    Serial.flush();      // let anything queued go out at the old rate
    Serial.begin(baud);
}

bool SettingsEeprom::isValidBaudRate(uint32_t baud) {
    return baudRateIsValid(baud);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

unsigned int SettingsEeprom::getTriggerHighDuration() const { return _data.triggerHighDuration; }

uint8_t SettingsEeprom::getEchoPin(uint8_t receiver) const {
    return receiver < RECV_CNT ? _data.echoPin[receiver] : 0xFF;
}

uint8_t       SettingsEeprom::getRecvCnt() const                   { return RECV_CNT; }
uint32_t      SettingsEeprom::getBaudRate() const                  { return _data.baudRate; }
unsigned long SettingsEeprom::getSyncPllInterval() const           { return _data.syncPllInterval; }
uint16_t      SettingsEeprom::getBurstDelayUs() const              { return _data.burstDelayUs; }
uint16_t      SettingsEeprom::getTriggerSendGuardTicks() const     { return _data.triggerSendGuardTicks; }
uint16_t      SettingsEeprom::getSearchDelayMinUs() const          { return _data.searchDelayMinUs; }
uint16_t      SettingsEeprom::getSearchDelayMaxUs() const          { return _data.searchDelayMaxUs; }
uint16_t      SettingsEeprom::getSearchDelayStepUs() const         { return _data.searchDelayStepUs; }
uint8_t       SettingsEeprom::getSearchSamplesPerStep() const      { return _data.searchSamplesPerStep; }
bool          SettingsEeprom::getSearchPrintSteps() const          { return _data.searchPrintSteps != 0; }
bool          SettingsEeprom::getResetSelfAfterRemoteReset() const { return _data.resetSelfAfterRemoteReset != 0; }
uint16_t      SettingsEeprom::getResetSelfDelayMs() const          { return _data.resetSelfDelayMs; }
uint16_t      SettingsEeprom::getSamplingTimeMs() const            { return _data.samplingTimeMs; }
uint8_t       SettingsEeprom::getMaxSavedTicks() const             { return _data.maxSavedTicks; }

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

bool SettingsEeprom::setTriggerHighDuration(unsigned int us) {
    return store(_data.triggerHighDuration, (uint16_t)us,
                 us >= TRIGGER_HIGH_DURATION_MIN && us <= TRIGGER_HIGH_DURATION_MAX);
}

bool SettingsEeprom::setEchoPin(uint8_t receiver, uint8_t pin) {
    if (receiver >= RECV_CNT) return false;
    return store(_data.echoPin[receiver], pin, echoPinIsValid(pin));
}

bool SettingsEeprom::setBaudRate(uint32_t baud) {
    const bool changed = baud != _data.baudRate;
    if (!store(_data.baudRate, baud, isValidBaudRate(baud))) return false;
    if (changed) {
        applyBaudRate(baud);
    }
    return true;
}

bool SettingsEeprom::setSyncPllInterval(unsigned long ms) {
    return store(_data.syncPllInterval, (uint32_t)ms,
                 ms >= SYNC_PLL_INTERVAL_MIN && ms <= SYNC_PLL_INTERVAL_MAX);
}

bool SettingsEeprom::setBurstDelayUs(uint16_t us) {
    return store(_data.burstDelayUs, us, us >= BURST_DELAY_MIN_US && us <= BURST_DELAY_MAX_US);
}

bool SettingsEeprom::setTriggerSendGuardTicks(uint16_t ticks) {
    return store(_data.triggerSendGuardTicks, ticks, ticks <= TRIGGER_SEND_GUARD_MAX_TICKS);
}

bool SettingsEeprom::setSearchDelayMinUs(uint16_t us) {
    return store(_data.searchDelayMinUs, us, us <= BURST_DELAY_MAX_US);
}

bool SettingsEeprom::setSearchDelayMaxUs(uint16_t us) {
    return store(_data.searchDelayMaxUs, us, us <= BURST_DELAY_MAX_US);
}

bool SettingsEeprom::setSearchDelayStepUs(uint16_t us) {
    return store(_data.searchDelayStepUs, us, us >= 1 && us <= BURST_DELAY_MAX_US);
}

bool SettingsEeprom::setSearchSamplesPerStep(uint8_t samples) {
    return store(_data.searchSamplesPerStep, samples, samples >= 1);
}

bool SettingsEeprom::setSearchPrintSteps(bool print) {
    return store(_data.searchPrintSteps, (uint8_t)print, true);
}

bool SettingsEeprom::setResetSelfAfterRemoteReset(bool reset) {
    return store(_data.resetSelfAfterRemoteReset, (uint8_t)reset, true);
}

bool SettingsEeprom::setResetSelfDelayMs(uint16_t ms) {
    return store(_data.resetSelfDelayMs, ms, true);
}

bool SettingsEeprom::setSamplingTimeMs(uint16_t ms) {
    return store(_data.samplingTimeMs, ms, ms >= SAMPLING_TIME_MIN_MS && ms <= SAMPLING_TIME_MAX_MS);
}

bool SettingsEeprom::setMaxSavedTicks(uint8_t count) {
    return store(_data.maxSavedTicks, count, count >= MAX_SAVED_TICKS_MIN && count <= SAMPLE_BUFFER_SIZE);
}
