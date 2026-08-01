# Notes

Design notes, gotchas, and open items.

## "Transmitter station" is the radio receiver

The naming is inverted depending on which link you look at.
`transmitter-station/src/main.cpp` opens with *"Receiver program for the
UltraGPS system"* and its `RadioController::init()` defaults to
`as_receiver = true`. Both are correct: the board **receives** on the radio link
and **transmits** on the ultrasonic one. The directory name follows the
ultrasonic role; the source comments follow the radio role.

## The shared modules are copies, and they have drifted

`radio.*`, `timers.*`, `led_control.*`, and `debug_macros.h` exist separately in
each project rather than in a shared library directory. `led_control.*` and
`debug_macros.h` are still byte-identical, but the other two have diverged:

| File | Difference |
|------|-----------|
| `radio.cpp` | The base station calls `maskIRQ(true, true, false)` (IRQ on RX only); the transmitter does not. The pipes are opened in opposite directions, as they must be. |
| `timers.h/.cpp` | `pllCorrect()` takes a second `tick_delay` argument on the base station and one argument on the transmitter. Only the transmitter's ISR applies `tick_delay` before pulsing. |

A fix applied to one copy does **not** reach the other. Consolidating them into
a PlatformIO `lib/` directory shared by both projects would remove the hazard,
but it is not done today.

## Interval timers in `loop()` are reset every iteration

`base-station/src/main.cpp` declares its interval bookkeeping inside `loop()`:

```cpp
void loop()
{
    long currentTime = millis();
    long lastSyncTime = 0;
    long lastPulseTime = 0;
    ...
    if (millis() - lastSyncTime >= (unsigned int)SYNC_PLL_INTERVAL) { ... }
```

Because `lastSyncTime` and `lastPulseTime` are re-initialised to `0` on every
pass, both comparisons are true on every pass once the board has been up for a
few milliseconds. In practice:

- `syncPLL()` sends a packet on **every loop iteration**, not every 10 ms as
  `SYNC_PLL_INTERVAL` suggests.
- In continuous mode, `continuousPollingDelay` (20 ms) has no effect. The real
  rate limit is the blocking `while(!trigger_fired){}` wait inside
  `handleUltrasonics()`, which ties output to the Timer1 rate — about 10 lines
  per second at the default 10 Hz.

Making the constants meaningful means declaring the two variables `static` (or
moving them to file scope). Doing so would change the observed radio traffic and
the continuous-mode rate, so it is a behavioural change, not a pure cleanup.

## `measure_echo_times()` is called with interrupts enabled

The call site reads:

```cpp
//! Measure the echo times without interrupts to avoid timing issues with the radio IRQ
measure_echo_times(false);
```

The comment describes the opposite of what the argument does — `false` means
`noInterrupts()` is **not** called, so interrupts stay enabled during sampling.
The function supports both; only the comment is stale. Passing `true` would
block the Timer1 ISR for the duration of the sampling window, which is why the
enabled path is the one in use.

## The measurement is hard-wired to six receivers on `PORTC`

`echo_value()` is `PINC & 63` — a single read of the low six bits of `PORTC`,
which is what makes it fast enough to sample all six channels in one tight loop.
The consequence is that `ECHO_PIN[]`, the `this_ticks[6]` arrays, and the print
statement in `handleUltrasonics()` cannot be re-pointed by editing the array
alone. Fewer than six receivers works — unused pins stay pulled up and report
`0` — but more than six, or receivers on digital pins, needs the measurement and
printing code changed too.

The transition buffers `value[100]` and `ticks[100]` account for 500 of the
923 bytes of RAM the base station uses. Sampling stops early if they fill.

## Half-built features

Several constants and code paths are present but inert. They are not dead code
so much as unfinished work:

- **`tick_delay` on the base station.** `TRIGGER_TICK_DELAY` (100 ticks) is
  declared but never sent; `handleUltrasonics()` passes a hard-coded
  `SONIC_BURST_DELAY` of 2000 µs instead. The transmitter honours whatever
  arrives.
- **`syncPLL()`.** It sends a trigger packet with `tick_delay = 0`, with the
  intent of nudging the transmitter's PLL without firing the ultrasonic burst.
  The transmitter has no way to distinguish this from a real trigger, so it
  fires anyway. The source marks it `TODO … NOT YET IMPLEMENTED` and adds *"MAY
  NOT EVER BE IMPLEMENTED"*.
- **`LATENCY_COMPENSATION_US`** (4000 µs, `radio.h`, both projects) is declared
  and never read.
- **`TRIGGER_HIGH_DURATION`** (300 µs, base `main.cpp`) is declared and never
  read; the actual pulse width is `TRIGGER_PULSE_US` in `timers.cpp`.
- **`radioIRQPin`** is declared on the base station but no interrupt is attached
  — only the transmitter uses its radio IRQ line.
- **`STAT_1`–`STAT_3`** are wired and controllable but nothing drives them.

## The radio link is deliberately unreliable

`setAutoAck(false)` and `setRetries(0, 0)` are intentional. A retried trigger
packet would arrive after the moment it was meant to schedule, so a late packet
is worse than a dropped one: a drop costs one measurement, a retry corrupts the
phase relationship the whole system depends on. Missing measurements are handled
downstream, not on the wire.

## `base-station/upload` is out of date

The Python helper at `base-station/upload` wraps `arduino-cli` with board
selection (`--list`, `-p`, `-i`, `-b`). It defaults to a sketch named
`base-station.ino`, which no longer exists — the project moved to PlatformIO and
the sources now live in `base-station/src/`. It still works if you pass the
source directory explicitly:

```bash
./base-station/upload -p /dev/ttyACM0 base-station/src
```

`base-station/conversation-upload-script.md` is the development log from writing
that script and is not documentation of the current layout. Prefer
`./build.sh upload -s base`, which uses PlatformIO directly.

## No stop command for continuous mode

`C` sets the polling state to `CONTINOUS` and nothing clears it except another
command — `P` or `S`, both of which set it back to `POLLING` — or a board reset.
There is no dedicated stop letter, and no timeout: a client that starts
continuous mode and disconnects leaves the board streaming into a closed port.

## Possible next steps

- Move the four duplicated modules into a shared PlatformIO `lib/` directory so
  the two projects cannot drift apart.
- Make `lastSyncTime` / `lastPulseTime` `static` so `SYNC_PLL_INTERVAL` and
  `continuousPollingDelay` actually govern the rates.
- Add an explicit stop command for continuous mode, and consider a timeout that
  drops back to polling if no command has been seen for a while.
- Finish or remove the `tick_delay` path and `syncPLL()`, so the timing model
  has one mechanism rather than two half-mechanisms.
- Upgrade the docs theme to
  [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/) by adding
  `mkdocs-material` to `docs/requirements.txt` and setting `theme.name: material`
  in `mkdocs.yml` (search, dark mode, better nav).
- Document the calibration constants (`A` and `B`) once they are established, so
  the count-to-distance conversion lives alongside the firmware that produces
  the counts.
