# CLAUDE.md — UltraGPS Arduino firmware

Context for Claude Code sessions in this repository. Read this first; it records
decisions and open items that are not obvious from the code or git history.

## What this repo is

Two PlatformIO sketches for Arduino Unos in the St. Mary's University UltraGPS
ultrasonic positioning system, plus a `shared/` directory of libraries compiled
into both:

- `base-station/` — radio master. Takes single-letter serial commands (38400
  bps by default, stored in EEPROM), sends trigger packets over nRF24L01+, times
  echoes from six receivers on A0–A5, prints tick counts. This is the board the
  ground-station program (UltraGpsXY, separate repo) talks to. Its sources:
  - `main.cpp` — global objects, `setup()`, `syncPLL()`, `loop()` only.
  - `settings-eeprom.*` — class `SettingsEeprom`: every tunable's default (in
    the header), range checks, EEPROM record, RAM copy, getters/setters.
  - `ultrasonic-sampling.*` — class `UltrasonicSampling`: the echo sampling loop
    (`measureEchoTimes()`), simulated timings, sampling time / transition cap /
    echo-pin mapping.
  - `command_handler.*` — class `CommandHandler` (formerly `CommandControl`):
    serial line buffering, decoding, dispatch, `PollingState`.
  - `command_control.*` — class `CommandControl : public CommandHandler`: the
    `CommandType` list, the command table (built in the constructor), private
    handlers, `handleUltrasonics()`, `runContinuous()`.
  - `timers.*`, `led_control.*`, `debug_macros.h`.
- `transmitter-station/` — radio **receiver** (the name follows the ultrasonic
  role). Phase-locks its Timer1 to the base station and pulses the ultrasonic
  transmitter trigger pin.
- `shared/radio/` — the radio layer (pipe addresses, `CommsPacket`, all
  send/receive). `shared/sysreset/` — watchdog software reset. Pulled in by
  `lib_extra_dirs = ../shared` in each `platformio.ini`; `build_flags = -I src`
  lets shared code include each project's own `debug_macros.h`.
- `timers.*`, `led_control.*`, `debug_macros.h` are still **per-project copies**
  (they have drifted; the base-station copies now also have Doxygen comments).

Full documentation lives in `docs/` (MkDocs, `strict: true`). Keep it in sync
when changing commands, packet types, or settings.

Comments in `base-station/src` and `shared/` are Doxygen (Qt style): `//!`
blocks with `@brief`/`@param`/`@return` on declarations (API docs live in the
headers), `//!<` for trailing member docs, plain `//` inside function bodies.
Leave a blank line between `//! @{`/`//! @}` and a neighbouring doc comment, or
Doxygen merges them.

## Building

`pio` is not on PATH on this machine:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
./build.sh build                 # both stations
./build.sh build -s base         # one station
mkdocs build --strict --site-dir /tmp/site   # docs check (mkdocs is /usr/bin/mkdocs)
```

Both build in under a second once cached. Always build both stations after
touching `shared/`. Ports in `platformio.ini` (uncommitted local edit, Oct 2026):
base `/dev/ultragps-base` (udev symlink made by `udev-setup.sh`), transmitter
`/dev/ttyUSB0`. Doxygen is not installed (an attempt to run a downloaded copy
was blocked); `apt install doxygen` if it is wanted.

## Serial protocol (base station)

One letter (+ argument for `H`) + newline. Replies start with a marker: `N:` for
`P`, `S`, `R` (`C:` in continuous mode), otherwise the command letter (`D:`,
`E:`, `H:`, `M:`). Invalid lines (unknown letter, missing/non-numeric `H`
argument) get no reply.

| Cmd | Effect | Reply |
|-----|--------|-------|
| `P` | One pulse, measure | `N: t0, ..., t5` |
| `C` | Continuous mode | stream of `C: ...` |
| `S` | One pulse, **simulated** counts | `N: ...` |
| `R` | Send `PKT_RESET` to transmitter (base resets too only if `RESET_SELF_AFTER_REMOTE_RESET`, default **false**) | `N: R` |
| `D` | Sweep burst delay 0–500 µs (10 µs steps, 3 pulses each), store + apply best | `D: <delay>, <mean>, <valid>/<total>` (one line per step too, `SEARCH_PRINT_STEPS` = true) |
| `E` | Store `SONIC_BURST_DELAY` as the burst delay again | `E: <delay>` |
| `H <ms>` | Sampling time 1 ms – 80 % of trigger period (80 ms at 10 Hz), stored + applied | `H: <ms>, <max ticks>` e.g. `H: 80, 71111` |
| `M` | Every setting back to its default (EEPROM + RAM) | `M: OK` (sent before the defaults, incl. baud rate, are applied) |

All of `D E H M R P S` return to polling mode (end continuous mode).

## Radio protocol (`shared/radio/radio.h`)

Packet types: `PKT_TRIGGER` (PLL phase + stores the burst delay on the
transmitter; carries TCNT1 + `tick_delay` > 0), `PKT_SYNC` (PLL phase only,
never touches delay), `PKT_RESET` (must carry `RESET_MAGIC` 0xA55A, sent 3×).
The transmitter fires on every compare match, delayed by the stored delay.
Channel 125, 1 Mbps, no auto-ack by design.

**`tick_delay == 0` is reserved.** The transmitter drops such triggers and
`sendTrigger()` refuses to send them. This is what fixes the stale-delay race
(sync packets used to be zero-delay triggers that clobbered the delay).
`handleUltrasonics()` also won't send a trigger within 2 ms of the compare match
(`TRIGGER_SEND_GUARD_TICKS`) as belt and braces.

## Settings in EEPROM (`settings-eeprom.*`)

- Defaults are the constants in `settings-eeprom.h` (the user's list: trigger
  high duration, echo pins, receiver count (read-only), baud rate, sync
  interval, burst delay, send guard, search parameters, reset behaviour) plus
  `SAMPLING_TIME_MS` (80) and `MAX_SAVED_TICKS` (100). `static_assert`s reject
  invalid defaults.
- Record at EEPROM address 0: header (magic 0x5E77, `SETTINGS_LAYOUT_VERSION`,
  size) + data + CRC-16. `begin()` (called in `setup()` before `Serial.begin()`)
  loads it, or writes the defaults if it is missing/old/corrupt ("first boot").
  The old `delay_store` record is not migrated.
- Getters return the RAM copy; setters validate, then write EEPROM + RAM
  together (`EEPROM.put` only writes changed bytes). Only commands call setters.
  `setBaudRate()` switches the port immediately; valid rates are listed in
  `baudRateIsValid()` (no table, so no RAM).
- **EEPROM survives reflashing.** Changing a default in the header does nothing
  on a board that has booted this firmware until `M` is sent or
  `SETTINGS_LAYOUT_VERSION` changes. A power cut during a write → next boot uses
  all defaults (never a mixed record).
- Sampling time, echo pins and the transition cap are held twice
  (`SettingsEeprom` and `UltrasonicSampling`); call
  `UltrasonicSampling::applySettings()` after changing them (`H`, `M`, `setup()` do).
- Only `D`, `E`, `H`, `M` reach setters. No command sets the baud rate etc. yet.

## Sampling (`ultrasonic-sampling.*`) — read before touching `measureEchoTimes()`

- A tick = one iteration of the sampling loop. `SAMPLING_LOOP_CYCLES` = **18**
  (verified by static count of the ELF and by running it in `avr-gdb`'s AVR
  simulator: `target sim`, 18 cycles without a transition, 44 with one), so
  1 tick = 1.125 µs; `ticks = ms × 16000 / 18`. A `DEBUG` build compiles the
  loop to 30 cycles, so `SAMPLING_LOOP_CYCLES` is 30 there (`#ifdef DEBUG`).
- The count depends on register allocation. `measureEchoTimes()` is
  `__attribute__((noinline, noclone, externally_visible))` so LTO cannot
  inline/clone/specialise it (without these it was 16, 17 or 18 depending on
  unrelated code). Re-count after editing that function or changing compiler
  flags.
- The TCNT1 80 % timeout is commented out (as the user asked). With it, every
  iteration did a 16-bit modulo + soft-float multiply (~470 cycles ≈ 29 µs), so
  the old firmware's ticks were ~26× larger and sampling really ended at ~80 ms;
  the old "about 60 ms" comment was stale. Hence the 80 ms default.
  **Tick counts are now ~26× larger: downstream calibration must be redone.**
- The sampling time is capped (user's request, 2026-10-29) at
  `SAMPLING_WINDOW_MAX_PERCENT` = 80 % of the trigger period, computed from
  `timer1_top` in `UltrasonicSampling::maxSamplingTimeMs()`; `H` rejects more,
  `applySettings()` clamps a stored value, so `setup()` calls it after
  `timer1_init_*()`. `SAMPLING_TIME_MAX_MS` (1000) is now only the outer bound.
- Echo pins may be remapped within A0–A5 (one PINC read); defaults reproduce
  the old `PINC & 63` exactly. Two quirks of the original reduction are kept on
  purpose: the fallen line is found by subtracting consecutive PINC values, and
  "all lines high" is the numerically largest PINC value.

## Decisions made

Session of 2026-10-07:
- Radio code is shared as a PlatformIO library under `shared/`, kept as C++.
- `D` letter chosen by the user; `E` was my pick for restore.
- Search scoring: zeros excluded; more valid readings wins; ties → lower mean.
- `syncPLL()` still fires every loop iteration (interval vars are not `static`);
  known, documented in `docs/notes.md`, left alone deliberately.

Session of 2026-10-28/29 (four tasks the user listed, done in order):
1. `settings-eeprom` module + `M`. File names follow the user's hyphenated
   names; classes are CamelCase. `BAUD_RATE` widened to `uint32_t`.
   `burst_delay_us` became the class's RAM copy (a global in a header would
   break the ODR); `SONIC_BURST_DELAY` stays its compile-time default rather
   than a second stored value. `delay_store.*` removed.
2. `ultrasonic-sampling` class + `H`. "Maximum number of ticks saved" read as
   the cap on saved transitions (`setMaxSavedTicks`); `setMaxTicks` sets the
   window directly. The 66000*4 cap TODO now uses the configured max ticks.
3. `command_control.*` → `command_handler.*` (`CommandHandler`); new
   `CommandControl` derives from it. Callbacks take the handler
   (`void (*)(CommandHandler&, const CmdArg*)`) so handlers are private members
   reached through a `dispatch<&CommandControl::onX>` thunk. `CommandType` is
   defined in `command_control.h` (opaque-declared in `command_handler.h`).
   The stale `onSimulate` forward declaration was dropped.
4. Doxygen comments everywhere in `base-station/src` and `shared/`; identifier
   typos fixed (`CONTINOUS` → `CONTINUOUS`, `onSimulateContinous` →
   `onSimulateContinuous`). Comment-only changes were verified by comparing
   firmware hex files byte for byte.

## Not yet verified on hardware

`1e73dee`/`3a561c9` (R, PKT_SYNC, D/E): clean builds and code tracing only.
The 2026-10-28/29 work: builds, host tests (differential test of the sampler
against the original code over thousands of PINC sequences, EEPROM record
tests incl. corruption and power cuts) and the avr-gdb AVR simulator. Bench
checks still owed:

1. `R` brings the transmitter back up (Optiboot + WDT reset).
2. `D` sweep produces a sensible curve.
3. First boot of this firmware writes the defaults; `H`/`M`/`D`/`E` persist
   across a power cycle (`INFO_DEBUG` prints which happened at boot).
4. Sampling time vs. wall clock (expect ~0.6 % longer from the millis() ISR).
5. Both boards **must be reflashed together** — old transmitter firmware does
   not know `PKT_SYNC` and would lose PLL correction.

## Possible next steps (not requested yet)

- Serial commands for the other setters (baud rate, search range, …).
- Decide what `H` > trigger period should do (cap / stop at compare match).
- A script that re-counts `SAMPLING_LOOP_CYCLES` from the ELF (objdump or the
  avr-gdb simulator), maybe hooked into `build.sh`.
- Move `timers`, `led_control`, `debug_macros` into `shared/` too.
- Make `lastSyncTime`/`lastPulseTime` static so the intervals mean something.
- Have the ground-station program send `D`/`E`/`H`/`M` and parse the replies.

## Conventions

- Git: work on `Development`; `release` is the stable branch, releases are
  `vX.Y` tags that must match the `VERSION` file. Commit only when asked.
- Commit messages explain *why*; co-author trailer for Claude.
- Docs are part of the deliverable: update `docs/*.md` and `README.md` with
  every command/setting/packet change, then run the strict mkdocs build.
