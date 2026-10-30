# Usage

Only the **base station** has a user interface. It listens on USB serial at
38400 bps by default and responds to single-letter commands. Its settings — the
burst delay, the sampling time, the trigger rate and the rest — are kept in its EEPROM, so they
survive resets, power cycles and reflashing. The transmitter station has no
command interface — once flashed it runs autonomously, reacting to radio packets
and its own timer; it keeps only the trigger rate sent by `F` in its EEPROM.

## Connecting

Any serial terminal at 38400 bps (the default baud rate) works:

```bash
pio device monitor -p /dev/ttyACM0 -b 38400
```

```bash
screen /dev/ttyACM0 38400        # exit with Ctrl-A then k
```

Opening the port asserts DTR, which resets the Uno, and the bootloader runs
briefly before the sketch starts, so give it a moment before sending the first
command. `setup()` itself does not wait for the host: on an Uno, whose
USB-serial chip is separate from the ATmega328P, `Serial` is always ready, so
its `while (!Serial && millis() < 3000);` line returns at once.

In normal operation the port is held by the
[ground-station program](https://github.com/Imperious22M/UltraGPS-Ground)
(`UltraGpsXY`), which issues these same commands on behalf of network clients.
Only one program can hold the port at a time.

## Commands

Send one letter followed by a newline or carriage return; `H` also takes a
number between the letter and the line ending. A line that cannot be decoded is
silently discarded (see [Invalid input](#invalid-input)).

| Command | Effect | Response |
|---------|--------|----------|
| `P` | Fire one pulse and measure the echoes. Leaves (or returns to) polling mode. | One `N:` line |
| `C` | Enter continuous mode — measure and report repeatedly until another command arrives | A stream of `C:` lines |
| `S` | Fire one pulse but report **simulated** counts. Returns to polling mode. | One `N:` line |
| `R` | Reset the transmitter station over the radio. The base station resets too only if `RESET_SELF_AFTER_REMOTE_RESET` is set (default `false`). Returns to polling mode. | `N: R` |
| `D` | Sweep the transmitter's burst delay (0 to 500 µs by default), then store in EEPROM and apply the delay with the most valid readings (ties broken by the lowest mean tick count). Returns to polling mode. | One `D:` line per step (by default), then the result `D:` line; ~15 s in all |
| `E` | Store the default burst delay, `SONIC_BURST_DELAY` (2000 µs), as the delay in force. Returns to polling mode. | One `E:` line |
| `H <ms>` | Set the echo sampling time to `<ms>` milliseconds (1 ms up to 80 % of the Timer1 period at the current trigger rate — 80 at 10 Hz, 40 at 20 Hz; default 80), store it in EEPROM and apply it. Returns to polling mode. | One `H:` line |
| `F <Hz>` | Set the trigger rate (1–50 Hz, default 10) on both stations: store it in EEPROM, restart Timer1, send it to the transmitter (which stores it and resets) and cap the sampling time to the new period. Returns to polling mode. | One `F:` line |
| `M` | Reset every stored setting (burst delay, sampling time, trigger rate and the rest) to its default and apply it (the default rate is also sent to the transmitter). Returns to polling mode. | `M: OK` |

!!! note "Line endings matter"
    `CommandHandler::poll()` (which `CommandControl` inherits) only dispatches
    a command when it sees `\n` or `\r`. In the Arduino IDE's Serial Monitor,
    set the line-ending dropdown to *Newline* or *Carriage return* — with *No
    line ending* nothing will ever happen.

### Leaving continuous mode

There is no dedicated stop command. Any other command ends the stream: `P`,
`S`, `R`, `D`, `E`, `H` and `M` all set the polling state back to `POLLING`
before doing their own work. An `H` without a number is ignored entirely (see
[Invalid input](#invalid-input)), so it leaves the stream running.

### Searching for the best burst delay

Every trigger packet carries a *burst delay*: the number of microseconds the
transmitter station waits after its Timer1 compare match before firing the
ultrasonic burst. The delay in force is a stored setting; its default is
`SONIC_BURST_DELAY`, 2000 µs. With the default settings `D` sweeps that delay
from 0 to 500 µs in 10 µs steps, fires three pulses at each step, and reports
the step with the most valid readings, using the lowest mean tick count to break
ties:

```
D: 120, 3987, 18/18
```

The fields are the best delay in microseconds, the mean tick count over all
valid readings at that delay, and how many of the readings were valid (a
receiver that reports `0` saw no echo and is excluded). A delay of exactly 0 is
reserved by the radio protocol, so the `0` step is measured with a 1 µs delay,
and 1 µs is what gets stored if it wins (the result line still shows `0`). A delay with more valid readings always
beats one with fewer, so a delay that makes receivers miss the burst cannot win
by averaging zeros.

The sweep is blocking: 51 steps × 3 pulses at 10 Hz takes about 15 seconds,
during which no other command is processed. By default (`SEARCH_PRINT_STEPS` is
`true`) it also prints one `D:` line per step as it goes, in the same format, so
the whole curve can be plotted; the result is the final line, which repeats the
winning step's line. The range, step, sample count and `SEARCH_PRINT_STEPS` are
stored settings with no command of their own. Their defaults are the `SEARCH_*`
constants in `base-station/src/settings-eeprom.h`; see
[Restoring the default settings](#restoring-the-default-settings) for how a
changed default reaches the board.

**The winner is saved and used.** The best delay is written to the base
station's EEPROM and becomes the delay sent with every `P`, `C` and `S`
measurement from then on, including after a reset, power cycle or reflash, until
`E` or `M` restores the default. If no step produced a single valid reading,
nothing is stored and the delay in force is unchanged.

### Restoring the default burst delay

`E` stores the compiled-in default, `SONIC_BURST_DELAY` (2000 µs), as the burst
delay in force, now and after every reset. It replies with the delay now in
force:

```
E: 2000
```

Reflashing the firmware does **not** clear a delay stored by `D`; EEPROM
survives uploads. Send `E` if you want to be sure the default is in use. `E`
touches no other setting, and it always stores the `SONIC_BURST_DELAY` compiled
into the running firmware, so it also applies an edited default. `M` restores
the default delay too, along with everything else.

### Setting the sampling time

`H` sets how long the echo lines are sampled after each trigger, in whole
milliseconds from 1 up to 80 % of the Timer1 period at the current trigger rate
(80 ms at 10 Hz, 40 ms at 20 Hz, 800 ms at 1 Hz; never above 1000 ms); the space
after the letter is optional (`H 80` or `H80`). The value is stored in EEPROM and
used from the next measurement on. The reply gives the sampling time now in
force and the same window in Timer1 counts (16 µs each, ms × 1000 / 16):

```
H 80
H: 80, 5000
```

A number outside that range changes nothing, and the reply shows the values
still in force, so compare it with what you sent.

As in the original firmware, sampling is bounded by Timer1: the loop stops once
`TCNT1` has advanced the sampling time, and never later than 80 % of the Timer1
period, so a window can never run into the next trigger pulse. Changing the
rate with `F` sets the sampling time to the new cap; use `H` afterwards for a
shorter window.

### Setting the trigger rate

`F` sets the trigger (pulse) rate of **both** stations in whole hertz, 1 to 50
(`F 20` or `F20`; default 10). The base station stores it in EEPROM, restarts
its Timer1 with the new period, sends the rate to the transmitter station in a
`PKT_RATE` packet (three copies) and sets the stored sampling time to 80 % of the new period. The reply gives
the rate, the Timer1 TOP and the sampling time now in force:

```
F 20
F: 20, 3124, 40
```

Here the sampling time becomes 40 ms, 80 % of the 50 ms period, and is
stored; `F 10` would set it back to 80 ms. An out-of-range value changes nothing. The transmitter station stores
the new rate in its own EEPROM and resets itself to apply it, so the first
measurements after `F` may come back empty while it restarts and re-locks.

!!! warning "Both boards need this firmware"
    A transmitter running older firmware does not know `PKT_RATE` and stays at
    10 Hz, and two boards at different rates give unusable timing. Reflash both
    boards together.

### Restoring the default settings

`M` writes the default of every stored setting back to EEPROM and applies it at
once: the burst delay, the sampling time, the trigger rate (10 Hz, also sent to
the transmitter), the `D` sweep parameters,
`RESET_SELF_AFTER_REMOTE_RESET`, the baud rate and the rest. The defaults are
the constants in `base-station/src/settings-eeprom.h` (see
[Configuration](configuration.md)). It replies:

```
M: OK
```

The reply is sent before the defaults are applied, because the baud rate is one
of them: if the stored baud rate is not `BAUD_RATE` (because `BAUD_RATE` was
edited after the settings were stored), the port switches to `BAUD_RATE` right
after the `M: OK` line and the terminal has to reconnect at that rate.

**Settings survive reflashing.** They live in the base station's EEPROM, which
an upload does not erase, so editing a default in `settings-eeprom.h` and
reflashing changes nothing on a board that already holds settings. To load new
defaults, send `M` after flashing — it resets every setting, including a delay
found by `D` — or change `SETTINGS_LAYOUT_VERSION` in the same file so the next
boot writes the defaults by itself. This is also the only way to change the
settings that have no command of their own, such as the `D` sweep range,
`SEARCH_PRINT_STEPS`, `RESET_SELF_AFTER_REMOTE_RESET` and the baud rate.

### Resetting the system

`R` sends a reset packet to the transmitter station and replies with the line
`N: R`, which the ground-station program should treat as the acknowledgement.
By default that is all it does: the base station keeps running, in polling mode,
and the transmitter station reboots and re-locks its PLL on the first sync
packet.

With `RESET_SELF_AFTER_REMOTE_RESET` set to `true` (the default is `false`), `R`
resets the base station too: `RESET_SELF_DELAY_MS` (100 ms) after the `N: R`
line it resets itself, so both boards reboot. Expect the serial port to be
silent for about a second while the base station comes back, then re-sync. Both
are stored settings with no command of their own; see
[Restoring the default settings](#restoring-the-default-settings) for how to
change them.

The reset packet is sent three times because the radio link has no
acknowledgements. If the transmitter station is powered off or out of range it
is simply missed; the base station still replies `N: R` (and still resets itself
if set to).

### Invalid input

Unknown letters (commands are case-sensitive) and empty lines are dropped
without any response, and so is an `H` with no number after it (`H` on its own,
or `H x`). The base station never echoes input and never reports an error for a
bad command, so silence after a command means it was not recognised. An `H`
with an out-of-range number is recognised: it changes nothing and replies with
the sampling time still in force.

Only the first character selects the command, and anything after it that the
command does not use is ignored, so `PX` still fires a pulse and `H 80 ms` sets
80 ms.

A line longer than 63 characters is not simply dropped.
`CommandHandler::poll()` discards its first 64 characters and keeps buffering,
so whatever follows the 64th character is decoded as a new line when the line
ends — and if it starts with a command letter, that command runs.

## Output format

Every measurement is one line: a two-character mode marker, then six
comma-separated ticks, one per receiver, in `A0`–`A5` order (the default
`ECHO_PIN`).

```
N: 4404, 3226, 4063, 4041, 3133, 4109
C: 4398, 3231, 4059, 4044, 3130, 4112
```

| Marker | Meaning |
|--------|---------|
| `N: ` | Normal — a single measurement, from `P` or `S` |
| `C: ` | Continuous — part of the stream started by `C` |

Replies to the other commands carry their own letter as the marker (`D:`, `E:`,
`H:`, `F:`, `M:`), except `R`, which answers `N: R`. The marker exists so a consuming
program can tell solicited replies from streamed data on the same port.

### What the numbers mean

Each value is the number of Timer1 counts (`TCNT1`, 16 µs each) between the
start of sampling, right after the trigger, and that receiver's echo going low
— a **tick**. Multiply by 16 for microseconds. The original firmware printed
sampling-loop iterations (about 29 µs each); ticks are now independent of how
fast the loop runs, including in a `DEBUG` build.

Converting to distance takes the form `Ax + B`, where `A` scales counts to time
or distance and `B` absorbs the fact that counting does not start at exactly
zero. Both constants are determined by calibration downstream; the firmware does
no conversion.

!!! warning "Check the calibration after upgrading"
    The tick duration depends on the compiled loop, so check any `A` and `B`
    against known distances whenever new firmware is flashed.

A value of `0` means that receiver never produced a usable echo transition
within the sampling window (the sampling time set with `H`) — out of range, not
wired, or the pulse never fired.

## Example session

```
$ pio device monitor -p /dev/ttyACM0 -b 38400
P
N: 4404, 3226, 4063, 4041, 3133, 4109
P
N: 4401, 3229, 4066, 4038, 3135, 4106
C
C: 4400, 3227, 4064, 4040, 3134, 4108
C: 4399, 3230, 4062, 4043, 3131, 4110
C: 4402, 3228, 4065, 4039, 3136, 4107
P
N: 4403, 3227, 4063, 4041, 3133, 4109
```

## Rates

In continuous mode `loop()` checks `continuousPollingDelay` (20 ms) before
starting the next measurement, but each measurement blocks in
`handleUltrasonics()` waiting for the next Timer1 compare match. The trigger
rate is therefore the real limit: at the default 10 Hz you get about ten lines
per second regardless of the 20 ms constant.

That holds while the sampling time fits in the trigger period. Sampling starts
at the compare match, and the next trigger packet goes out only after it ends,
never within 2 ms of a compare match (`TRIGGER_SEND_GUARD_TICKS`, 125 Timer1
ticks by default). The default 80 ms window leaves about 20 ms of the 100 ms
period at 10 Hz. If sampling ends too close to the next compare match, or after
it, the next measurement waits a further period and the stream drops to one line
every two or more periods.

Raising the output rate means raising the trigger rate with `F` — see
[Setting the trigger rate](#setting-the-trigger-rate). The sampling time is
capped at 80 % of the shorter period automatically, which also shortens the
maximum measurable range.

## Startup behaviour

On reset the base station:

1. Loads its settings from EEPROM — before opening the serial port, because the
   baud rate is one of them. If there is no valid settings record (a blank
   chip, a record written by other firmware, a different
   `SETTINGS_LAYOUT_VERSION`, a corrupted record or one holding an out-of-range
   value), it first writes the defaults ("first boot") and uses them. A burst
   delay stored by the `D` command of earlier firmware is not carried over: the
   first boot overwrites it.
2. Opens the serial port at the stored baud rate (38400 by default). It does
   not wait for the host: on an Uno `Serial` is always ready, so the
   `while (!Serial && millis() < 3000);` that follows returns at once.
3. Sets the echo pins (`A0`–`A5`) to `INPUT_PULLUP` and applies the stored
   sampling time.
4. Initialises the LEDs (animation off by default).
5. Starts Timer1 at the stored trigger rate (10 Hz by default).
6. Initialises the radio. **If this fails the sketch halts**, blinking
   `RADIO_STATUS` on a two-second cycle (1 s on, 1 s off) and printing
   `[ERROR] Still halted.` on each cycle. Nothing recovers this except fixing
   the wiring and resetting the board.
7. Sends its trigger rate to the transmitter station once (a `PKT_RATE`
   packet), so a transmitter left at another rate follows it.
8. Leaves `RADIO_STATUS` solid and waits for commands.

There is no banner or ready message on a successful start — the first output you
see is the response to your first command. With `INFO_DEBUG` defined (see
`debug_macros.h`), `setup()` also prints `[SETUP] Settings loaded from EEPROM`
or `[SETUP] No valid settings in EEPROM, defaults written`, and later the burst
delay in force (`[SETUP] Burst delay: 2000 us`).
