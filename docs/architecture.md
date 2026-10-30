# Architecture

The repository contains **two independent PlatformIO projects**, one per
Arduino, plus a `shared/` directory of libraries that both projects compile in.
The radio layer (`shared/radio/`) and the software-reset helper
(`shared/sysreset/`) live there, so pipe addresses, radio settings, packet
framing and every send/receive function are defined exactly once.
`led_control`, `timers` and `debug_macros` are still copied into each project;
the command, settings and sampling modules exist only in the base station.

## Projects

| Project | Runs on | Responsibility |
|---------|---------|----------------|
| `base-station/` | Arduino Uno wired to the six receivers | Serial command interface, settings stored in EEPROM, radio master, echo timing, serial output |
| `transmitter-station/` | Arduino Uno wired to the ultrasonic transmitter | Radio slave, PLL phase correction, fires the ultrasonic trigger |
| `shared/` | both | PlatformIO libraries pulled in by each project via `lib_extra_dirs = ../shared` |

!!! note "The transmitter station is the radio *receiver*"

## Modules

Both sketches are organised the same way: a `setup()`/`loop()` control file plus
a small set of single-purpose modules.

| Module | Files | Responsibility |
|--------|-------|----------------|
| Control | `main.cpp` | Global objects, pin/module setup, main loop; `syncPLL()` on the base station, the radio ISR on the transmitter |
| Radio | `shared/radio/radio.{h,cpp}` | nRF24L01+ setup, pipe addresses, `CommsPacket` framing, send/receive (**shared**) |
| Reset | `shared/sysreset/sysreset.h` | Watchdog-based software reset used by the `R` command (**shared**) |
| Timers | `timers.{h,cpp}` | Timer1 CTC setup, the `TIMER1_COMPA` ISR, PLL correction |
| LEDs | `led_control.{h,cpp}` | Status LED state, startup animation |
| Debug | `debug_macros.h` | Compile-time-gated `DEBUG_*` / `DEBUG_INFO_*` printing |
| Command handler | `command_handler.{h,cpp}` | `CommandHandler`: serial line buffering, argument decoding, dispatch through a letter → command table (**base station only**) |
| Commands | `command_control.{h,cpp}` | `CommandControl`, derived from `CommandHandler`: the command list and letter table, the command handlers and `handleUltrasonics()` (**base station only**) |
| Settings | `settings-eeprom.{h,cpp}` | `SettingsEeprom`: default and accepted range of every setting, the EEPROM record and its RAM copy (**base station only**) |
| Sampling | `ultrasonic-sampling.{h,cpp}` | `UltrasonicSampling`: the echo sampling loop, the reduction to one tick count per receiver, simulated counts (**base station only**) |

### Third-party library

[`nRF24/RF24`](https://github.com/nRF24/RF24) is declared in each
`platformio.ini` as `lib_deps` and is downloaded into `.pio/libdeps/` on the
first build.

## Data flow

```
   Ground station PC  (UltraGpsXY)
          │  (USB serial, /dev/ttyACM0 @ 38400)
          │  commands: P / S / C / R / D / E / H / M
          │  data: "N: t0, ..., t5"
          ▼
   ┌──────────────────────────────────────────┐
   │              BASE STATION                │
   │  CommandControl ──► handleUltrasonics()  │
   │  (a CommandHandler)           ▲          │
   │        │                      │          │
   │  RadioController    UltrasonicSampling:: │
   │                      measureEchoTimes()  │
   └────────┬─────────────────────────────────┘
            │ nRF24 CommsPacket    ▲ echoes on A0–A5
            │ (TCNT1, tick_delay)  │
            │ PKT_SYNC, PKT_RESET, │
            │ PKT_RATE             │
            ▼                      │
   ┌────────────────────┐          │
   │ TRANSMITTER STATION│          │
   │  IRQ ► pllCorrect()│          │
   │  Timer1 ISR ► pin 5│          │
   └────────┬───────────┘          │
            │ trigger pulse        │
            ▼                      │
     Ultrasonic transmitter ═══► 6 receivers
                (sound)
```

## How one measurement happens

1. The base station receives a command (`P`, `S`, or `C`) over serial.
   `cmdCtrl.poll()` (`CommandHandler::poll()`, inherited by `CommandControl`)
   buffers the line, looks the letter up in the command table that
   `CommandControl` builds in its constructor, and calls that command's
   handler. In continuous mode `loop()` repeats the `C` handler through
   `cmdCtrl.runContinuous()`.
2. The handler calls `CommandControl::handleUltrasonics()`, which clears
   `trigger_fired` and calls `RadioController::sendTrigger(TCNT1, delay)` with
   the burst delay in force (`SettingsEeprom::getBurstDelayUs()`). The packet
   carries the base station's current Timer1 count and a µs delay.
3. The transmitter station's radio raises its IRQ line. `onRadioIRQ()` reads the
   packet and calls `pllCorrect(timer_val - TCNT1)`, nudging the local Timer1 so
   the two boards stay in phase, and stores the tick delay. Between measurements
   the base station keeps the PLL locked with `PKT_SYNC` packets, which carry
   the same timer value but leave the stored delay alone.
4. Both boards' Timer1 reach compare-match. Each `TIMER1_COMPA_vect` ISR pulses
   its trigger pin (pin 5) for 50 µs and sets `trigger_fired`. On the
   transmitter that pulse fires the ultrasonic burst, while on the receiver it triggers
   the receive functionality of the receivers.
5. The base station, released from its `while(!trigger_fired)` wait, runs
   `UltrasonicSampling::measureEchoTimes()`. It reads all the echo lines at
   once (`PINC`, masked to the receivers' echo pins) in a tight loop for the
   sampling time (default 80 ms, set with `H`), recording every change of the
   echo lines along with the Timer1 counts elapsed since sampling started
   (the *tick*, 16 µs each) at which it changed. The window is timed with `TCNT1` and never exceeds 80 % of the
   Timer1 period. Sampling stops early once the transition cap
   (`MAX_SAVED_TICKS`, 100 by default) is reached.
6. The recorded transitions are reduced to one tick count per receiver (the
   tick at which its echo line fell after all the lines were high, or `0` if it
   never did), and the six values are printed over serial as one
   comma-separated line.

## Settings

Most base-station settings (burst delay, sampling time, delay-search range,
`R` behaviour, baud rate, echo pins and more) are stored in the ATmega328P's
EEPROM by `SettingsEeprom`. `settings-eeprom.h` holds each one's default and
accepted range; the rest are compile-time (see
[Configuration](configuration.md)).

- `setup()` calls `settings.begin()` before `Serial.begin()`, because the baud
  rate is itself a setting. `begin()` reads the record at EEPROM address 0: a
  header (magic `0x5E77`, `SETTINGS_LAYOUT_VERSION`, data size), the settings
  and a CRC-16. If the record is missing (a blank chip, or one written by other
  firmware), has another layout version or size, fails its CRC or holds an
  out-of-range value, the defaults are written and used: the *first boot*.
  Otherwise the stored values are loaded into a RAM copy.
- Getters return the RAM copy, which always equals what is stored, so reading a
  setting never touches the EEPROM. A setter checks the range, then updates
  EEPROM and RAM together (only bytes that changed are written); an
  out-of-range value is rejected and changes nothing. Only commands change
  stored settings: `D` and `E` (burst delay), `H` (sampling time) and `M`
  (every setting back to its default).
- **EEPROM survives reflashing.** After the first boot, editing a default in
  `settings-eeprom.h` has no effect on that board until `M` is sent or
  `SETTINGS_LAYOUT_VERSION` is changed, which makes the next boot rewrite the
  defaults.

`UltrasonicSampling` keeps its own copy of the sampling time, echo pins and
transition cap. `sampler.applySettings(settings)` loads them, and sets the echo
pins to `INPUT_PULLUP`, in `setup()`; `H` and `M` call it again after changing
the stored values.

## The burst delay

Every `PKT_TRIGGER` carries `tick_delay`, the number of microseconds the
transmitter station waits after its own Timer1 compare match before pulsing the
trigger pin (`delayMicroseconds(tick_delay)` at the top of its ISR). The value
is stored on the transmitter and stays in force until the next trigger replaces
it. Two rules keep it from being clobbered:

- PLL corrections travel in their own `PKT_SYNC` packets, which carry only the
  Timer1 value and never touch `tick_delay`.
- A `PKT_TRIGGER` with a delay of `0` is dropped by the transmitter without
  being applied, so a stray or malformed packet cannot clear the delay. The
  base station never sends one: `sendTrigger()` refuses, `SONIC_BURST_DELAY`
  is checked at compile time, and the stored burst delay cannot be set below
  1 µs (`BURST_DELAY_MIN_US`).

As belt and braces, `handleUltrasonics()` also refuses to send a trigger within
the send guard of the compare match (a setting, default
`TRIGGER_SEND_GUARD_TICKS` = 125 Timer1 ticks, 2 ms) and lets that compare
match pass first, so the packet always has a near-full cycle to arrive before
the pulse that should use it.

The delay sent with each measurement is the stored burst delay,
`SettingsEeprom::getBurstDelayUs()`: one of the [settings](#settings), not a
separate record. The first boot stores `SONIC_BURST_DELAY` (2000 µs). `D`
sweeps the delay over the range given by the `SEARCH_*` settings (see
[Usage](usage.md)), reports the value with the most valid readings (ties broken by
the lowest mean tick count across the receivers), and stores it, which applies it at once and keeps it
across resets. `E` stores `SONIC_BURST_DELAY` again, and `M` restores it along
with every other setting. A stored delay must be 1–16383 µs
(`BURST_DELAY_MIN_US`/`BURST_DELAY_MAX_US`). 16383 µs is the longest delay the
Arduino core's `delayMicroseconds()` produces correctly on the transmitter: a
software limit of the core, which multiplies the value by 4 in 16-bit
arithmetic, not of the hardware.

## Resetting the boards

The `R` command resets the transmitter station, and optionally the base station
as well, without touching either board:

1. The base station sends a `PKT_RESET` packet (`RadioController::sendReset()`),
   repeated three times because the link has no acknowledgements.
2. It prints `N: R` over serial as the acknowledgement and flushes the UART.
3. The transmitter station's radio ISR sees the packet, verifies the
   `RESET_MAGIC` value in its `timestamp` field, and sets a flag that `loop()`
   turns into a watchdog reset (`systemReset()` in `shared/sysreset/sysreset.h`).
4. By default the base station stops there: it returns to polling mode and
   keeps running, and the rebooted transmitter re-locks its PLL on the next sync
   packet. Only if the `RESET_SELF_AFTER_REMOTE_RESET` setting is `true`
   (default `false`) does the base station wait `RESET_SELF_DELAY_MS` (100 ms)
   and then reset itself the same way, so both boards come up fresh. No command
   sets it: change its default in `settings-eeprom.h`, reflash, and send `M`.

Both sketches call `systemResetGuard()` first thing in `setup()` to disarm the
watchdog that a software reset leaves running.

## Timing model

Everything is driven by **Timer1 in CTC mode with a prescaler of 256**, so one
Timer1 count is 16 µs. Both boards start it with `timer1_init(top)`; the TOP
comes from the trigger rate, `top = round(62500 / Hz) - 1`
(`triggerRateToTop()` in `shared/radio/radio.h`):

| Rate | `OCR1A` (TOP) |
|------|---------------|
| 1 Hz | 62499 |
| 5 Hz | 12499 |
| 10 Hz (default) | 6249 |
| 20 Hz | 3124 |
| 50 Hz | 1249 |

The rate is a runtime setting (`F <Hz>`, 1–50 Hz), not a function to pick in
`setup()`. The base station stores it in its EEPROM settings and sends it in a
`PKT_RATE` packet (type 5; `timestamp` = TOP, `tick_delay` = ~TOP as a check;
sent three times) after `F`, after `M` and once at every boot. The transmitter
station accepts the packet only if the check matches and the TOP is in range;
if the TOP differs from its own, it stores it in its EEPROM (address 0: magic
`0x7A7E`, TOP, ~TOP) and resets through the watchdog, then starts Timer1 with
the stored TOP (10 Hz if there is none). Both boards must run firmware that
knows `PKT_RATE`, so reflash them together.

The two boards free-run at the same nominal rate; the radio packet is what keeps
them aligned. Because the packet carries the base station's raw `TCNT1`, the
transmitter can compute its phase error directly and add it to its own `TCNT1` —
a one-step software PLL, implemented as `pllCorrect()` in `timers.h`.

The base station's sampling window opens at its own compare match. As in the
original firmware it is bounded by `TCNT1`: `measureEchoTimes()` reads `TCNT1`
at the start, and on each pass computes the elapsed Timer1 counts (adding one
period if `TCNT1` wrapped) and stops once they reach the limit computed before
the loop, the smaller of the sampling time in counts and 80 % of
`timer1_top + 1`. The window therefore never runs into the next trigger pulse,
and `H` accepts at most 80 % of the period (80 ms at 10 Hz, 40 ms at 20 Hz).

!!! note "Ticks are Timer1 counts"
    The values printed over serial are **Timer1 counts** (16 µs each) elapsed
    between the start of sampling and each receiver's echo falling, so they do
    not depend on how fast the sampling loop runs. (The original firmware
    printed loop iterations, about 29 µs each.) The counts carry an `Ax + B`
    type relationship to distance: `A` converts counts to time/distance and `B`
    absorbs the fact that counting does not begin exactly at zero. Calibration
    is done downstream, not on the Arduino; check it against known distances
    after reflashing.

## Logging

Serial printing is gated at compile time by two macros in `debug_macros.h`:

- **`INFO_DEBUG`** — activates the `DEBUG_INFO_*` macros (status and error
  messages: settings loaded at boot, trigger and reset packets sent, commands
  received, trigger fired, packet failures).
- **`DEBUG`** — activates both `DEBUG_INFO_*` and the verbose `DEBUG_*` macros
  (radio init progress, tick counts, `PINC` values, sampling diagnostics). It
  also changes how the sampling loop compiles, so tick counts from a `DEBUG` build are not comparable with a normal
  build's.

Neither is defined by default, so both sets compile away to nothing. Plain
`Serial` prints — the command replies, the reason a radio failed to initialise
(on both boards) and the base station's `[ERROR]` halt messages — are always
present. See [Configuration](configuration.md#debug-output).
