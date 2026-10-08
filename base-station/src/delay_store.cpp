#include "delay_store.h"
#include <EEPROM.h>

namespace {

//! Marks a record written by this firmware.
constexpr uint16_t DELAY_STORE_MAGIC = 0xB0D5;

struct DelayRecord {
    uint16_t magic;      //! DELAY_STORE_MAGIC when a value is stored
    uint16_t delay_us;   //! The burst delay
    uint16_t check;      //! ~delay_us, guards against a partial write
};

bool recordValid(const DelayRecord &rec) {
    return rec.magic == DELAY_STORE_MAGIC
        && rec.check == (uint16_t)~rec.delay_us
        && rec.delay_us >= DELAY_STORE_MIN_US
        && rec.delay_us <= DELAY_STORE_MAX_US;
}

} // namespace

bool delayStoreLoad(uint16_t &out_delay_us) {
    DelayRecord rec;
    EEPROM.get(DELAY_STORE_EEPROM_ADDR, rec);
    if (!recordValid(rec)) return false;
    out_delay_us = rec.delay_us;
    return true;
}

bool delayStoreSave(uint16_t delay_us) {
    if (delay_us < DELAY_STORE_MIN_US || delay_us > DELAY_STORE_MAX_US) return false;
    DelayRecord rec = { DELAY_STORE_MAGIC, delay_us, (uint16_t)~delay_us };
    EEPROM.put(DELAY_STORE_EEPROM_ADDR, rec);   //! put() only writes changed bytes
    return true;
}

void delayStoreClear() {
    DelayRecord rec = { 0, 0, 0 };
    EEPROM.put(DELAY_STORE_EEPROM_ADDR, rec);
}
