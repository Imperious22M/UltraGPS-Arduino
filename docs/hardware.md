# Hardware

Both stations are Arduino Unos (ATmega328P at 16 MHz) with an nRF24L01+ radio
module and five status LEDs. The pin assignments are identical apart from the
radio IRQ line, which only the transmitter station uses.

## Bill of materials

| Item | Qty | Notes |
|------|-----|-------|
| Arduino Uno | 2 | One per station |
| nRF24L01+ radio module | 2 | 2.4 GHz, SPI |
| HC-SR04-style ultrasonic transmitter | 1 | Driven by the transmitter station |
| Ultrasonic receivers | 6 | Echo lines feed the base station's `A0`–`A5` |
| Status LEDs (+ resistors, optional) | 10 | Five per station |

!!! note "Power the radio properly"
    The nRF24L01+ draws current in bursts that the Uno's 3.3 V regulator handles
    poorly. A 10 µF capacitor across the module's `VCC`/`GND` is the usual fix
    for `radio.begin()` succeeding but packets failing intermittently.

## Pin map — base station

Defined in `base-station/src/main.cpp`, `base-station/src/timers.cpp` and, for
the echo pins, `base-station/src/settings-eeprom.h`.

| Pin | Direction | Signal | Set in |
|-----|-----------|--------|--------|
| `2` | — | Radio IRQ — **declared but unused** | `main.cpp` (`radioIRQPin`) |
| `3` | OUT | `STAT_1` LED | `LedControl leds(3, 4, 6, 7, 8)` |
| `4` | OUT | `STAT_2` LED | " |
| `5` | OUT | Trigger pulse (50 µs, fired by the Timer1 ISR) | `timers.cpp` (`triggerPin`) |
| `6` | OUT | `STAT_3` LED | `LedControl` |
| `7` | OUT | `PULSE_TRIG` LED — lit while a pulse is in flight | " |
| `8` | OUT | `RADIO_STATUS` LED — solid once the radio initialises | " |
| `9` | OUT | Radio `CE` | `RadioController radioCtrl(9, 10)` |
| `10` | OUT | Radio `CSN` | " |
| `11` | OUT | SPI `MOSI` → radio | Hardware SPI |
| `12` | IN | SPI `MISO` ← radio | " |
| `13` | OUT | SPI `SCK` → radio | " |
| `A0`–`A5` | IN (pull-up) | Echo lines from receivers 0–5 | `ECHO_PIN[]` (`settings-eeprom.h`), stored in EEPROM |

The six echo lines **must** stay on `A0`–`A5`: `UltrasonicSampling::measureEchoTimes()`
samples them all with a single read of the `PINC` register (`PORTC`'s low six
bits). Within `A0`–`A5` the receiver-to-pin mapping is a setting: `ECHO_PIN[]`
gives each receiver's pin, and each receiver's result follows its pin. Any other
pin is rejected, at compile time for the `ECHO_PIN[]` defaults and by
`SettingsEeprom::setEchoPin()` at run time. No serial command changes the
mapping yet: edit `ECHO_PIN[]`, reflash, then send `M`, which resets every
stored setting to its default (the stored settings survive reflashing).

## Pin map — transmitter station

Defined in `transmitter-station/src/main.cpp` and
`transmitter-station/src/timers.cpp`.

| Pin | Direction | Signal |
|-----|-----------|--------|
| `2` | IN | Radio `IRQ` — `attachInterrupt(..., FALLING)` drives `onRadioIRQ()` |
| `3` | OUT | `STAT_1` LED |
| `4` | OUT | `STAT_2` LED |
| `5` | OUT | Trigger to the ultrasonic transmitter (50 µs pulse) |
| `6` | OUT | `STAT_3` LED |
| `7` | OUT | `PULSE_TRIG` LED |
| `8` | OUT | `RADIO_STATUS` LED |
| `9` | OUT | Radio `CE` |
| `10` | OUT | Radio `CSN` |
| `11`–`13` | IN/OUT | Hardware SPI to the radio |

Pin 2 must be interrupt-capable.

## Radio link

Both stations configure the nRF24L01+ identically in `RadioController::init()`,
except for which pipe they write to:

| Setting | Value |
|---------|-------|
| Channel | `125` (`RADIO_CHANNEL` in `shared/radio/radio.h`) |
| Data rate | `RF24_1MBPS` |
| PA level | `RF24_PA_HIGH` |
| Payload | 8 bytes, fixed (dynamic payloads disabled) |
| Auto-ack | Off |
| Retries | `setRetries(0, 0)` — none |
| Base station writes to | `PIPE_BASE_TO_TRANSMITTER` (`0xF0F0F0F0E1`) |
| Transmitter listens on | `PIPE_BASE_TO_TRANSMITTER`, writes to `PIPE_TRANSMITTER_TO_BASE` (`0xF0F0F0F0E2`) |

Auto-ack and retries are deliberately off: a late trigger packet is worse than a
lost one, so the link is fire-and-forget.

The 8-byte payload is `CommsPacket` (`shared/radio/radio.h`):

| Field | Type | Meaning |
|-------|------|---------|
| `type` | `PacketType` | `PKT_NO_DATA` (1), `PKT_TRIGGER` (2), `PKT_RESET` (3), `PKT_SYNC` (4) or `PKT_RATE` (5) |
| `_pad[3]` | `uint8_t` | Alignment padding to keep the struct at 8 bytes |
| `timestamp` | `uint16_t` | The base station's `TCNT1` at send time; `RESET_MAGIC` (`0xA55A`) in a reset packet; the new Timer1 TOP in a `PKT_RATE` |
| `tick_delay` | `uint16_t` | Microseconds the transmitter waits before firing. Only meaningful in a `PKT_TRIGGER`, where it must be > 0; a zero-delay trigger is ignored. In a `PKT_RATE` it holds ~TOP as a check |

## Status LEDs

`LedControl` drives all five LEDs on each board and holds a shadow copy of their
state, so `getStat1()` and friends never need a `digitalRead` on an output pin.

| LED | Meaning |
|-----|---------|
| `RADIO_STATUS` (pin 8) | Solid once the radio initialises. **Blinking on a two-second cycle means radio init failed** and the sketch has halted. |
| `PULSE_TRIG` (pin 7) | Lit while a trigger is in active |
| `STAT_1`–`STAT_3` (pins 3, 4, 6) | Reserved — wired and controllable, but nothing currently drives them |

Both sketches call `leds.init(0)`, which sets the pins to output and drives them
low **without** the startup animation. Pass `leds.init(1)` (or omit the
argument) to run `startupAnimation()` — a chase pattern across all five LEDs,
useful as a quick "the board rebooted" indicator.
