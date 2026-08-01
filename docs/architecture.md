# Architecture

The repository contains **two independent PlatformIO projects**, one per
microcontroller. They share four source files by copy (`radio`, `led_control`,
`timers`, `debug_macros`) — there is no shared library directory, so a change to
one station's copy does not propagate to the other.

## Projects

| Project | Runs on | Responsibility |
|---------|---------|----------------|
| `base-station/` | Arduino Uno wired to the six receivers | Serial command interface, radio master, echo timing, serial output |
| `transmitter-station/` | Arduino Uno wired to the ultrasonic transmitter | Radio slave, PLL phase correction, fires the ultrasonic trigger |

!!! note "The transmitter station is the radio *receiver*"
    `transmitter-station/src/main.cpp` opens with the comment
    *"Receiver program for the UltraGPS system"*. That refers to its role on the
    **radio** link — it receives packets from the base station. On the
    **ultrasonic** side it is the transmitter. See [Notes](notes.md).

## Modules

Both sketches are organised the same way: a `setup()`/`loop()` control file plus
a small set of single-purpose modules.

| Module | Files | Responsibility |
|--------|-------|----------------|
| Control | `main.cpp` | Pin/module setup, command callbacks, main loop |
| Radio | `radio.{h,cpp}` | nRF24L01+ setup, `CommsPacket` framing, send/receive |
| Timers | `timers.{h,cpp}` | Timer1 CTC setup, the `TIMER1_COMPA` ISR, PLL correction |
| LEDs | `led_control.{h,cpp}` | Status LED state, startup animation |
| Debug | `debug_macros.h` | Compile-time-gated `DEBUG_*` / `DEBUG_INFO_*` printing |
| Commands | `command_control.{h,cpp}` | Serial line buffering and command dispatch (**base station only**) |

### Third-party library

[`nRF24/RF24`](https://github.com/nRF24/RF24) is declared in each
`platformio.ini` as `lib_deps` and is downloaded into `.pio/libdeps/` on the
first build — it is not vendored in the repository.

## Data flow

```
   Ground station PC  (UltraGpsXY)
          │  (USB serial, /dev/ttyACM0 @ 38400)
          │  commands: P / S / C        data: "N: t0, ..., t5"
          ▼
   ┌──────────────────────────────────────────┐
   │            BASE STATION                  │
   │  CommandControl ──► handleUltrasonics()  │
   │        │                    ▲            │
   │  RadioController      measure_echo_times │
   └────────┬──────────────────────┬──────────┘
            │ nRF24 CommsPacket    │ echoes on A0–A5
            │ (TCNT1, tick_delay)  │
            ▼                      │
   ┌────────────────────┐          │
   │ TRANSMITTER STATION│          │
   │  IRQ ► pllCorrect()│          │
   │  Timer1 ISR ► pin 5│          │
   └────────┬───────────┘          │
            │ trigger pulse        │
            ▼                      │
     Ultrasonic transmitter ═══► 6 receivers ──┘
                (sound)
```

## How one measurement happens

1. The base station receives a command byte (`P`, `S`, or `C`) over serial.
   `CommandControl::poll()` buffers the line and dispatches to the callback in
   the global command table.
2. `handleUltrasonics()` clears `trigger_fired` and calls
   `RadioController::sendTrigger(TCNT1, SONIC_BURST_DELAY)`. The packet carries
   the base station's current Timer1 count and a 2000 µs delay.
3. The transmitter station's radio raises its IRQ line. `onRadioIRQ()` reads the
   packet and calls `pllCorrect(timer_val - TCNT1)`, nudging the local Timer1 so
   the two boards stay in phase, and stores the tick delay.
4. Both boards' Timer1 reach compare-match. Each `TIMER1_COMPA_vect` ISR pulses
   its trigger pin (pin 5) for 50 µs and sets `trigger_fired`. On the
   transmitter that pulse fires the ultrasonic burst.
5. The base station, released from its `while(!trigger_fired)` wait, runs
   `measure_echo_times()`. It polls `PINC & 63` in a tight loop for up to
   ~60 ms, recording every change of the six echo lines along with the loop
   iteration count at which it changed.
6. The recorded transitions are reduced to one tick count per receiver, and the
   six values are printed over serial as one comma-separated line.

## Timing model

Everything is driven by **Timer1 in CTC mode with a prescaler of 256**, so one
timer tick is 16 µs. `timers.cpp` provides four rates:

| Function | `OCR1A` | Rate |
|----------|---------|------|
| `timer1_init_5Hz()` | 12498 | 5 Hz |
| `timer1_init_10Hz()` | 6249 | 10 Hz (default on both boards) |
| `timer1_init_15Hz()` | 4166 | ≈ 14.999 Hz |
| `timer1_init_20Hz()` | 3124 | 20 Hz |

The two boards free-run at the same nominal rate; the radio packet is what keeps
them aligned. Because the packet carries the base station's raw `TCNT1`, the
transmitter can compute its phase error directly and add it to its own `TCNT1` —
a one-step software PLL, implemented as `pllCorrect()` in `timers.h`.

!!! note "The echo counts are not the timer ticks"
    The values printed over serial are **loop iterations** of the sampling loop
    in `measure_echo_times()`, not Timer1 ticks. They carry an `Ax + B` type
    relationship to distance: `A` converts counts to time/distance and `B`
    absorbs the fact that counting does not begin exactly at zero. Calibration
    is done downstream, not on the Arduino.

## Logging

Serial printing is gated at compile time by two macros in `debug_macros.h`:

- **`INFO_DEBUG`** — activates the `DEBUG_INFO_*` macros (status and error
  messages: radio init, trigger fired, packet failures).
- **`DEBUG`** — activates both `DEBUG_INFO_*` and the verbose `DEBUG_*` macros
  (tick counts, `PINC` values, sampling diagnostics).

Neither is defined by default, so both sets compile away to nothing. Plain
`Serial.println()` calls — the measurement output and the base station's fatal
radio error — are always present. See [Configuration](configuration.md).
