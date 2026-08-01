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
| Status LEDs (+ resistors) | 10 | Five per station |

!!! note "Power the radio properly"
    The nRF24L01+ draws current in bursts that the Uno's 3.3 V regulator handles
    poorly. A 10 µF capacitor across the module's `VCC`/`GND` is the usual fix
    for `radio.begin()` succeeding but packets failing intermittently.

## Pin map — base station

Defined in `base-station/src/main.cpp` and `base-station/src/timers.cpp`.

| Pin | Direction | Signal | Set in |
|-----|-----------|--------|--------|
| `2` | — | Radio IRQ — **declared but unused** on this board | `main.cpp` (`radioIRQPin`) |
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
| `A0`–`A5` | IN (pull-up) | Echo lines from receivers 0–5 | `ECHO_PIN[]` |

The six echo lines **must** stay on `A0`–`A5`: `measure_echo_times()` reads them
as a group with `PINC & 63`, which is a direct read of `PORTC`'s low six bits.
Moving a receiver to a digital pin would silently drop it from the measurement.

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
| `11`–`13` | — | Hardware SPI to the radio |

Pin 2 must be interrupt-capable — on the Uno that means pin 2 or 3. If you move
it, update `radioIRQPin` in `main.cpp`.

## Radio link

Both stations configure the nRF24L01+ identically in `RadioController::init()`,
except for which pipe they write to:

| Setting | Value |
|---------|-------|
| Channel | `100` |
| Data rate | `RF24_1MBPS` |
| PA level | `RF24_PA_HIGH` |
| Payload | 8 bytes, fixed (dynamic payloads disabled) |
| Auto-ack | Off |
| Retries | `setRetries(0, 0)` — none |
| Base station writes to | `PIPE_TX_TO_RX` (`0xF0F0F0F0E1`) |
| Transmitter listens on | `PIPE_TX_TO_RX`, writes to `PIPE_RX_TO_TX` (`0xF0F0F0F0E2`) |

Auto-ack and retries are deliberately off: a late trigger packet is worse than a
lost one, so the link is fire-and-forget. The `RX_TO_TX` pipe is opened on both
sides but nothing currently sends on it.

The 8-byte payload is `CommsPacket` (`radio.h`):

| Field | Type | Meaning |
|-------|------|---------|
| `type` | `PacketType` | `PKT_NO_DATA` (1) or `PKT_TRIGGER` (2) |
| `_pad[3]` | `uint8_t` | Alignment padding to keep the struct at 8 bytes |
| `timestamp` | `uint16_t` | The base station's `TCNT1` at send time |
| `tick_delay` | `uint16_t` | Microseconds the transmitter waits before firing |

## Status LEDs

`LedControl` drives all five LEDs on each board and holds a shadow copy of their
state, so `getStat1()` and friends never need a `digitalRead` on an output pin.

| LED | Meaning |
|-----|---------|
| `RADIO_STATUS` (pin 8) | Solid once the radio initialises. **Blinking on a two-second cycle means radio init failed** and the sketch has halted. |
| `PULSE_TRIG` (pin 7) | Lit while a trigger is in flight |
| `STAT_1`–`STAT_3` (pins 3, 4, 6) | Reserved — wired and controllable, but nothing currently drives them |

Both sketches call `leds.init(0)`, which sets the pins to output and drives them
low **without** the startup animation. Pass `leds.init(1)` (or omit the
argument) to run `startupAnimation()` — a chase pattern across all five LEDs,
useful as a quick "the board rebooted" indicator. It takes about 2.5 seconds.
