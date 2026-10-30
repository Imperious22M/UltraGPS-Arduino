# Notes

Design notes, gotchas, and open items.

## "Transmitter station" is the radio receiver

The naming is inverted depending on which link you look at. The transmitter
station initialises its radio with `RADIO_ROLE_TRANSMITTER`, which in
`shared/radio/radio.cpp` means *listen*. Both are correct: the board
**receives** on the radio link and **transmits** on the ultrasonic one. The
directory name follows the ultrasonic role; the radio code names things after
which station they belong to (`PIPE_BASE_TO_TRANSMITTER`).

## The radio layer is shared; the other modules are still copies

`radio.*` now lives once in `shared/radio/` and is compiled into both projects
through `lib_extra_dirs = ../shared`. The one behavioural difference the two old
copies had — only the base station called `maskIRQ(true, true, false)` — is
resolved by applying it on both ends; the transmitter station's ISR already
ignored everything but `rx_ready`, so this only removes spurious IRQ edges.

`timers.*`, `led_control.*`, and `debug_macros.h` are still per-project copies.
`led_control.*` and `debug_macros.h` are no longer byte-identical — the base
station's copies now carry Doxygen comments and the transmitter station's do
not — but their code is still the same. `timers.*` differs in code too:

| File | Difference |
|------|-----------|
| `timers.h/.cpp` | `pllCorrect()` takes a second `tick_delay` argument on the base station and one argument on the transmitter, and only the base station's copy has `resetTriggerFlag()`. Only the transmitter's ISR applies `tick_delay` before pulsing. |

A fix applied to one copy does **not** reach the other. The same `shared/`
mechanism would work for these too; `debug_macros.h` is the awkward one because
the shared radio code includes it from the project's `src/` (via
`build_flags = -I src`) so that the debug level stays per-project.

## Interval timers in `loop()` are reset every iteration

`base-station/src/main.cpp` declares its interval bookkeeping inside `loop()`:

```cpp
void loop()
{
    long currentTime = millis();
    long lastSyncTime = 0;
    long lastPulseTime = 0;
    ...
    if (millis() - lastSyncTime >= (unsigned int)settings.getSyncPllInterval()) { ... }
```

Because `lastSyncTime` and `lastPulseTime` are re-initialised to `0` on every
pass, both comparisons are true on every pass once the board has been up for a
few milliseconds. In practice:

- `syncPLL()` sends a `PKT_SYNC` on **every loop iteration**, not every 10 ms
  as the `SYNC_PLL_INTERVAL` setting suggests; the stored interval has no
  effect. Harmless to the burst delay since sync packets no longer carry one,
  but it is a lot of radio traffic.
- In continuous mode, `continuousPollingDelay` (20 ms, `command_handler.h`) has
  no effect. The real rate limit is the blocking `while(!trigger_fired){}` wait
  inside `CommandControl::handleUltrasonics()`, which ties output to the Timer1
  rate — about 10 lines per second at the default 10 Hz, as long as the
  sampling time is shorter than the trigger period.

Making the interval and the delay meaningful means declaring the two variables
`static` (or moving them to file scope). Doing so would change the observed
radio traffic and the continuous-mode rate, so it is a behavioural change, not a
pure cleanup.

## `measureEchoTimes()` is called with interrupts enabled

`CommandControl::handleUltrasonics()` calls:

```cpp
_sampler.measureEchoTimes(false);
```

`false` means `noInterrupts()` is **not** called, so interrupts stay enabled
during sampling, as the comment above the call says. The function supports
both. Passing `true` would block the Timer1 ISR for the duration of the
sampling window, which is why the enabled path is the one in use.

The window is timed with `TCNT1`, so interrupts do not stretch it, but time
spent in an interrupt is not counted in ticks (the `millis()` interrupt takes
about 0.6 % of the time). The base station attaches no
radio interrupt.

## Sampling is bounded by TCNT1 again

The sampling loop is timed with Timer1, as in the original firmware: it reads
`TCNT1` at the start, computes the elapsed Timer1 counts on every pass (adding
one period when `TCNT1` wrapped at the compare match) and stops once they reach
a limit computed once before the loop, the smaller of the sampling time in
counts (`ms × 1000 / 16`) and 80 % (`SAMPLING_WINDOW_MAX_PERCENT`) of
`timer1_top + 1`. The original firmware recomputed its cut-off with a modulo and
a float multiply on every pass; this one only subtracts and compares.

An intermediate version counted CPU cycles instead (a fixed cycles-per-iteration
constant converting milliseconds to ticks). That proved unreliable and was
removed.

- The printed values are Timer1 counts (16 µs ticks) at which each echo fell,
  not loop iterations, so they no longer depend on the loop speed. Loop-iteration
  ticks came out about 15× larger than the original's once the slow modulo/float
  check was gone. **Redo the calibration (`A` and `B` in `Ax + B`)** after
  reflashing.
- The window follows the trigger rate again: `H` accepts 1 ms up to 80 % of the
  Timer1 period (80 ms at 10 Hz, 40 ms at 20 Hz, 800 ms at 1 Hz; never above
  1000 ms) and replies `H: <ms>, <Timer1 counts>`, e.g. `H: 80, 5000`. Every `F`
  sets the stored sampling time to that cap (the longest window the new rate
  allows); `H` can shorten it afterwards. A stored value above the cap is
  clamped at boot. `setup()` therefore applies the sampling settings after
  starting Timer1.
- The simulated counts from `S` (around 401) are not calibrated against real
  ones.

## The trigger rate is a runtime setting

`F <Hz>` (1–50 Hz, default 10) replaces editing a `timer1_init_*Hz()` call on
both boards. The base station stores the rate in EEPROM (which is why
`SETTINGS_LAYOUT_VERSION` became `2`: boards with older firmware rewrite their
defaults on the next boot), restarts Timer1 with `timer1_init(top)`, sends a
`PKT_RATE` packet (three copies) and re-caps the sampling time; it replies
`F: <Hz>, <TOP>, <sampling ms>`. `M` re-applies and re-sends the default rate,
and the base sends its rate once at every boot.

The transmitter station stores a new TOP in its EEPROM (address 0: magic
`0x7A7E`, TOP, ~TOP) and resets through the watchdog, then boots at that rate.
The link has no acknowledgements, so if all three copies are lost the
transmitter stays at its old rate until the next `F`, `M` or base-station boot.
**Both boards must be reflashed together**: an old transmitter ignores
`PKT_RATE`, and boards at different rates give unusable timing.

## Echo pins must be on `PORTC` (`A0`–`A5`)

The sampling loop reads every echo line in one read of `PINC`, masked to the
receivers' echo pins. That single read is what makes it fast enough to sample
all the channels in one tight loop. The consequences:

- Each receiver's echo pin can be remapped (`ECHO_PIN[]` in `settings-eeprom.h`
  holds the defaults, `SettingsEeprom::setEchoPin()` stores a new one), but only
  within `A0`–`A5`; anything else is rejected by the setter and, for the
  defaults, by a `static_assert`. Each receiver's result follows its own pin's
  `PORTC` bit, so the default pins behave exactly like the old `PINC & 63`.
- `RECV_CNT` is the length of `ECHO_PIN[]`, has no setter, and is checked at
  compile time to be 1–6.
- The output line still has six values: `handleUltrasonics()` prints
  `getTicks(0)` to `getTicks(5)`, and `getTicks()` returns `0` for a receiver
  at or beyond `RECV_CNT`. Fewer than six receivers works (unwired pins stay
  pulled up and also report `0`), but more than six, or receivers on digital
  pins, needs the sampling and printing code changed.

The transition buffers `_value[100]` and `_ticks[100]` (`SAMPLE_BUFFER_SIZE`,
compile-time only) are members of `UltrasonicSampling` and account for 500 of
the 1048 bytes of RAM (51 % of the 2 KB) the base-station build uses. Sampling
stops early once the transition cap (the `MAX_SAVED_TICKS` setting, 1–100,
default 100) is reached.

## Half-built features

Several constants and code paths are present but inert. They are not dead code
so much as unfinished work:

- **Settings without a command.** Only the burst delay (`D`, `E`), the sampling
  time (`H`) and the reset to defaults (`M`) are reachable over serial. Every
  other setting (baud rate, echo pins, sync interval, send guard, the `SEARCH_*`
  parameters, the `R` behaviour, the transition cap, the trigger high duration)
  has a setter in `SettingsEeprom` but no command, so it can only be changed by
  editing its default in `settings-eeprom.h`, reflashing and sending `M` (which
  also resets everything else, including a burst delay found by `D`).
- **`tick_delay` on the base station.** The base station's copy of `timers.*`
  still declares `tick_delay` and a two-argument `pllCorrect()` that stores it,
  but the base station never calls `pllCorrect()` and its ISR never applies a
  delay. The delay it sends is the stored burst delay
  (`SettingsEeprom::getBurstDelayUs()`); only the transmitter applies it.
- **`syncPLL()`** now sends a dedicated `PKT_SYNC` packet, so the transmitter
  corrects its phase without touching the stored burst delay. The transmitter
  still fires its burst on every compare match regardless; the sync packet only
  stops the delay from being reset to zero between measurements.
- **`LATENCY_COMPENSATION_US`** (4000 µs, `shared/radio/radio.h`) is declared
  and never read.
- **`TRIGGER_HIGH_DURATION`** (300 µs) is now a stored setting
  (`settings-eeprom.h`, 1–16383 µs, with a getter and a setter), but nothing
  reads it; the actual pulse width is still `TRIGGER_PULSE_US` (50 µs) in
  `timers.cpp`.
- **`radioIRQPin`** is declared on the base station but no interrupt is attached
  — only the transmitter uses its radio IRQ line.
- **`STAT_1`–`STAT_3`** are wired and controllable but nothing drives them.

## The radio link is deliberately unreliable

`setAutoAck(false)` and `setRetries(0, 0)` are intentional. A retried trigger
packet would arrive after the moment it was meant to schedule, so a late packet
is worse than a dropped one: a drop costs one measurement, a retry corrupts the
phase relationship the whole system depends on. Missing measurements are handled
downstream, not on the wire.

## `base-station/conversation-upload-script.md` is a historical log

`base-station/conversation-upload-script.md` is the development log from writing
`base-station/upload`, a Python helper around `arduino-cli` that has since been
removed from the repository. It is not documentation of the current layout. Use
`./build.sh upload -s base`, which uses PlatformIO directly.

## No stop command for continuous mode

`C` sets the polling state to `CONTINUOUS` and nothing clears it except another
recognised command (`P`, `S`, `R`, `D`, `E`, `H` and `M` all set it back to
`POLLING`) or a reset. There is no dedicated stop letter, and no timeout: a
client that starts continuous mode and disconnects leaves the board streaming
into a closed port.

## Stored settings survive reflashing

The base station's settings live in EEPROM, which `avrdude` does not touch on a
normal upload, so a board keeps them (the burst delay found by `D`, the sampling
time set with `H`, the trigger rate set with `F`, and all the others) across firmware versions. The defaults in
`settings-eeprom.h` are only written on the first boot, when the stored record
is missing, from other firmware, of another layout version or size, corrupted,
or out of range. After that, editing a default and
reflashing has **no effect** on that board: send `M`, or change
`SETTINGS_LAYOUT_VERSION` so the next boot rewrites the defaults.
`SETTINGS_LAYOUT_VERSION` must also change whenever the stored fields change.
If a measurement looks off after a reflash, send `M` (or `E` for just the burst
delay) to be sure the defaults are in use.

The record is self-validating (magic, layout version, data size and a CRC-16,
and every value is range-checked on load), so a blank or corrupted EEPROM falls
back to the defaults rather than loading garbage.

The previous firmware's burst-delay record (written by the removed `delay_store`
module, magic `0xB0D5`) is **not migrated**. The new firmware does not recognise
it, so its first boot writes the defaults over it and a delay found with the old
`D` is lost; run `D` again after upgrading.

## Sampling settings are held twice

`SettingsEeprom` and `UltrasonicSampling` each hold the sampling time, the echo
pins and the transition cap; the sampler keeps its own copy, with the time
already converted to ticks and the pins to `PINC` bit masks. After changing one
of these through `SettingsEeprom` (`setSamplingTimeMs()`, `setEchoPin()`,
`setMaxSavedTicks()`), call `sampler.applySettings(settings)`, as `setup()`,
`H` and `M` do. A command that forgets stores the new value but keeps sampling
with the old one until the next reset. The reverse holds too:
`UltrasonicSampling`'s own setters change only the sampler, not what is stored,
so the change is lost at the next `applySettings()` or reset.

## A stored baud rate can make the board unreachable

The baud rate is a stored setting (`BAUD_RATE`, default 38400, a rate
`baudRateIsValid()` accepts), and `setup()` opens the serial port at the stored
rate on every boot. No command sets it yet, but `SettingsEeprom::setBaudRate()`
switches the port immediately. If a future command stores a rate the host does not use,
the board answers only at that rate, after every reset. To recover, connect at
the stored rate and send `M` (it replies `M: OK` at that rate, then switches to
`BAUD_RATE`), or flash firmware with a different `SETTINGS_LAYOUT_VERSION`,
whose first boot rewrites the defaults. `monitor_speed` in `platformio.ini` and
the ground-station program's baud rate do not follow the stored value.

## `R` resets only the transmitter station by default

`R` sends `PKT_RESET` to the transmitter station, prints `N: R`, and returns to
polling mode; the base station keeps running. Only when the
`RESET_SELF_AFTER_REMOTE_RESET` setting is `true` (default `false`) does it then
wait `RESET_SELF_DELAY_MS` (100 ms) and watchdog-reset itself as well. Earlier
versions of these docs said the base station resets too; the code has defaulted
to `false` since the command was added. The setting has no serial command, so
turning it on means changing its default in `base-station/src/settings-eeprom.h`,
reflashing, and sending `M`. The reset packet carries `RESET_MAGIC` so that a
corrupted trigger packet cannot be mistaken for a reset; the link's 16-bit CRC
already makes that unlikely.

## Possible next steps

- Move the remaining duplicated modules (`timers`, `led_control`,
  `debug_macros`) into `shared/` alongside the radio layer so the two projects
  cannot drift apart, dropping the base station's unused `tick_delay` on the
  way.
- Make `lastSyncTime` / `lastPulseTime` `static` so the `SYNC_PLL_INTERVAL`
  setting and `continuousPollingDelay` actually govern the rates.
- Add an explicit stop command for continuous mode, and consider a timeout that
  drops back to polling if no command has been seen for a while.
- Add serial commands for the settings that only have setters (baud rate, echo
  pins, sync interval, send guard, delay-search parameters, `R` behaviour,
  transition cap), and one that prints every stored setting, since there is
  currently no way to see most of what a board has stored.
- Upgrade the docs theme to
  [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/) by adding
  `mkdocs-material` to `docs/requirements.txt` and setting `theme.name: material`
  in `mkdocs.yml` (search, dark mode, better nav).
- Document the calibration constants (`A` and `B`) once they are re-established
  for the current firmware, so the count-to-distance conversion lives alongside the
  firmware that produces the counts.
