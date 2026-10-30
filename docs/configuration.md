# Configuration

Most base-station settings are **stored in the board's EEPROM**. Their defaults
are compile-time constants in `base-station/src/settings-eeprom.h`, but the
board uses the values stored on it, and some of them can be changed over serial
without re-uploading: the burst delay with `D` and `E`, the sampling time with
`H`, and all of them back to their defaults with `M` (see
[Settings stored in EEPROM](#settings-stored-in-eeprom)). Everything else,
including every transmitter-station setting, is **compile-time**: a change only
takes effect once the firmware is rebuilt and re-uploaded to the board.

## Where each setting lives

| File | Controls |
|------|----------|
| `<project>/platformio.ini` | Board, serial ports, monitor baud, libraries |
| `<project>/src/main.cpp` | Radio and LED pin assignments; the baud rate (transmitter station only) |
| `<project>/src/timers.cpp` | `timer1_init(top)`, trigger pin, trigger pulse width |
| `shared/radio/radio.h` | Trigger rate range and default (`TRIGGER_RATE_MIN_HZ`, `TRIGGER_RATE_MAX_HZ`, `TRIGGER_RATE_HZ`), `triggerRateToTop()` |
| `base-station/src/settings-eeprom.h` | Defaults and accepted ranges of the settings stored in EEPROM (baud rate, echo pins, burst delay, sync interval, delay search, `R` behaviour, sampling time and more), transition buffer size, EEPROM address and layout version of the stored record |
| `base-station/src/ultrasonic-sampling.h` | `SAMPLING_WINDOW_MAX_PERCENT` (80 %), `TIMER1_US_PER_COUNT` (16 µs) |
| `transmitter-station/src/main.cpp` | EEPROM record of the trigger rate received from the base station |
| `base-station/src/command_handler.h` | Command buffer limits, continuous-mode rate |
| `base-station/src/command_control.{h,cpp}` | Command list and letter table, `SIMULATE_REAL_COMMANDS` |
| `shared/radio/radio.{h,cpp}` | Radio channel, data rate, power, pipe addresses, packet types (**shared by both boards**) |
| `<project>/src/debug_macros.h` | Debug output level |

!!! note "Some settings must be changed on both boards"
    The pulse rate exists independently in each project. Change it on one board
    only and the boards stop agreeing. Each entry below says whether it needs to
    be kept in sync. The radio parameters are the exception: they live once in
    `shared/radio/` and are compiled into both images. The baud rate is also set
    separately on each board (see [Serial](#serial)); the settings stored in
    EEPROM belong to the base station alone.

## Settings stored in EEPROM

The base station keeps its settings in the ATmega328P's 1 KB EEPROM, managed by
`SettingsEeprom` (`base-station/src/settings-eeprom.{h,cpp}`). Each setting is
named after the constant in `settings-eeprom.h` that holds its default. The
header also sets the range each one must fall in, and a default outside its
range fails to compile.

| Setting | Default | Accepted range | Changed over serial by |
|---------|---------|----------------|------------------------|
| `ECHO_PIN[]` | `A0`–`A5` | `A0`–`A5` each | — |
| `BAUD_RATE` | `38400` | a rate `baudRateIsValid()` accepts ([below](#baud-rates)) | `B <baud>` |
| `SYNC_PLL_INTERVAL` | `10` ms | 1–60000 ms | — |
| `SONIC_BURST_DELAY` (burst delay) | `2000` µs | 1–16383 µs | `D` stores the delay it finds, `E` stores the default |
| `TRIGGER_SEND_GUARD_TICKS` | `125` Timer1 ticks | 0–3124 | — |
| `SEARCH_DELAY_MIN_US` | `0` µs | 0–16383 µs | — |
| `SEARCH_DELAY_MAX_US` | `500` µs | 0–16383 µs | — |
| `SEARCH_DELAY_STEP_US` | `10` µs | 1–16383 µs | — |
| `SEARCH_SAMPLES_PER_STEP` | `3` | 1–255 | — |
| `SEARCH_PRINT_STEPS` | `true` | `true` / `false` | — |
| `RESET_SELF_AFTER_REMOTE_RESET` | `false` | `true` / `false` | — |
| `RESET_SELF_DELAY_MS` | `100` ms | any (0–65535 ms) | — |
| `SAMPLING_TIME_MS` | `80` ms | 1 ms – 80 % of the Timer1 period at the current rate | `H <ms>` |
| `triggerRateHz` (default `TRIGGER_RATE_HZ`) | `10` Hz | 1–50 Hz | `F <Hz>` |
| `MAX_SAVED_TICKS` | `100` | 1–`SAMPLE_BUFFER_SIZE` (100) | — |
| `TRIGGER_HIGH_DURATION` | `300` µs | 1–16383 µs | — |

`M` writes every default back. The rest of this page describes what each setting
does; two are only covered here:

- `ECHO_PIN[]` is the echo pin of each receiver, and output column *n* reports
  the receiver on `ECHO_PIN[n]`. Only `A0`–`A5` are accepted, because the
  sampling loop reads every echo line in a single read of `PINC`. `RECV_CNT`
  (6), the length of the array, is read-only.
- `TRIGGER_HIGH_DURATION` is stored but not used by any code; the real trigger
  pulse width is `TRIGGER_PULSE_US` (see [Trigger timing](#trigger-timing)).

### How the stored settings are loaded

On every boot `setup()` calls `SettingsEeprom::begin()` before it opens the
serial port. It reads the record at EEPROM address 0 (`SETTINGS_EEPROM_ADDR`): a
header (magic number `0x5E77`, layout version `SETTINGS_LAYOUT_VERSION`, data
size), the settings, and their CRC-16.

- **Valid record:** the stored values are loaded into a RAM copy and used.
- **First boot:** if the record is missing (blank chip), was written by other
  firmware, has another layout version or size, fails its CRC or holds an
  out-of-range value, the defaults are written to EEPROM and used. The burst
  delay that earlier firmware stored with `D` counts as another firmware's
  record, so it is not carried over.

With `INFO_DEBUG` enabled the boot prints which case applied:
`[SETUP] Settings loaded from EEPROM` or
`[SETUP] No valid settings in EEPROM, defaults written`.

A command that changes a setting checks the range first and, if the value is
valid, writes it to EEPROM and the RAM copy together; an out-of-range value
changes nothing. Only bytes that differ are written, so storing an unchanged
value does not wear the EEPROM.

!!! note "EEPROM survives reflashing"
    Uploading new firmware does not touch the EEPROM. After the first boot,
    editing a default in `settings-eeprom.h` has **no effect** on that board
    until either:

    - `M` is sent, which writes every default back and applies it at once; or
    - `SETTINGS_LAYOUT_VERSION` (currently `2`; it became 2 when the trigger rate was added, so boards running older firmware rewrite their defaults on the first boot) is changed, so the next boot
      finds a record of another version and writes the defaults.

    Both reset **every** setting, including a burst delay found by `D` and a
    sampling time set with `H` and a rate set with `F`. Change `SETTINGS_LAYOUT_VERSION` whenever the
    stored fields change.

### Baud rates

`BAUD_RATE`, like any baud rate stored on the board, must be a rate that the
function `baudRateIsValid()` in `settings-eeprom.h` accepts: 300, 600, 1200,
2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600, 115200, 250000, 500000 or
1000000, the standard rates a 16 MHz Uno generates within 2.5 %. 230400 is left
out (3.5 % off). The function is a single expression rather than a table, so it
also works in a `static_assert` and uses no RAM; a `BAUD_RATE` it rejects fails
to compile.

The `B <baud>` command sets it at run time: it accepts only these rates, stores
the new rate and switches the port at once (the reply goes out at the old rate).

## PlatformIO project settings

`base-station/platformio.ini`:

```ini
[env:uno]
platform = atmelavr
board = uno
framework = arduino
lib_deps = nRF24/RF24
; Radio layer shared with the other station (shared/radio, shared/sysreset)
lib_extra_dirs = ../shared
; Let the shared library see this project's debug_macros.h
build_flags = -I src
monitor_speed = 38400

monitor_port = /dev/ttyACM0
upload_port = /dev/ttyACM0
```

| Key | Default (base / transmitter) | Notes |
|-----|------------------------------|-------|
| `board` | `uno` | Change for a different AVR board. The `PINC` reads, Timer1 values, the 16 µs Timer1 count and the accepted baud rates assume an ATmega328P at 16 MHz. |
| `lib_deps` | `nRF24/RF24` | Downloaded on first build |
| `lib_extra_dirs` | `../shared` | Compiles the `shared/` libraries (radio layer, software reset) into the project |
| `build_flags` | `-I src` | Lets the shared radio code include the project's own `debug_macros.h` |
| `monitor_speed` | `38400` | Must match the board's baud rate: the rate stored on the base station (default `BAUD_RATE` in `settings-eeprom.h`), `BAUD_RATE` in the transmitter station's `main.cpp` |
| `monitor_port` | `/dev/ttyACM0` / `/dev/ttyACM1` | Port the serial monitor opens |
| `upload_port` | `/dev/ttyACM0` / `/dev/ttyACM1` | Port `--target upload` flashes |

Override the upload port for a single run instead of editing the file:

```bash
pio run -d base-station --target upload --upload-port /dev/ttyACM2
```

## Serial

| Setting | File | Default | Notes |
|---------|------|---------|-------|
| `BAUD_RATE` | `settings-eeprom.h` (base) | `38400` | Default of the base station's stored baud rate, one of the [accepted rates](#baud-rates). **Keep in sync** with `monitor_speed` in `base-station/platformio.ini` and the ground-station program's `--baud-rate` |
| `BAUD_RATE` | `main.cpp` (transmitter) | `38400` | Debug output only. Keep in sync with `monitor_speed` in `transmitter-station/platformio.ini` |
| `CMD_BUF_SIZE` | `command_handler.h` | `64` | Size of the line buffer, including the NUL, so the longest line decoded is 63 characters. A longer line is not truncated: its first 64 characters are discarded and the rest is buffered as a new line, so its tail can still run a command (see [Invalid input](usage.md#invalid-input)). |
| `CMD_MAX_ARGS` | `command_handler.h` | `4` | Maximum arguments any one command can carry |

The base station opens its port at the baud rate **stored in EEPROM**, so
editing `BAUD_RATE` and reflashing is not enough: the board keeps its old rate.
Either connect at the old rate and send `M` (the `M: OK` reply still goes out at
the old rate, then the port switches to the new `BAUD_RATE`), or change
`SETTINGS_LAYOUT_VERSION` so that the next boot writes the defaults. The same
two ways recover a board that comes up at a rate the host does not use.

## Pulse rate

The trigger (pulse) rate is a **runtime setting**. `F <Hz>` (`F 20` or `F20`)
accepts 1–50 Hz (`TRIGGER_RATE_MIN_HZ` to `TRIGGER_RATE_MAX_HZ` in
`shared/radio/radio.h`; default `TRIGGER_RATE_HZ` = 10). The base station:

1. stores the rate in EEPROM (`triggerRateHz`);
2. restarts its Timer1 with `timer1_init(top)`, `top = round(62500 / Hz) - 1`
   (`triggerRateToTop()`);
3. sends a `PKT_RATE` packet (three copies) carrying that TOP to the
   transmitter station;
4. caps the sampling time at 80 % of the new period (see
   [Sampling window](#sampling-window)).

It replies `F: <Hz>, <TOP>, <sampling ms in force>`, e.g. `F: 20, 3124, 40`. An
out-of-range value changes nothing and the reply shows the values still in
force. `M` re-applies the default rate and sends it as well, and the base
station sends its rate once at every boot.

| Rate | Timer1 TOP (`OCR1A`) | Period | Sampling cap (80 %) |
|------|----------------------|--------|---------------------|
| 1 Hz | 62499 | 1000 ms | 800 ms |
| 5 Hz | 12499 | 200 ms | 160 ms |
| 10 Hz (default) | 6249 | 100 ms | 80 ms |
| 20 Hz | 3124 | 50 ms | 40 ms |
| 50 Hz | 1249 | 20 ms | 16 ms |

Timer1 runs in CTC mode with a prescaler of 256, so one Timer1 count is 16 µs.

The transmitter station accepts a `PKT_RATE` only if its check field holds the
inverted TOP and the TOP is in range. If the TOP differs from its own it stores
it in its EEPROM (address 0: magic `0x7A7E`, TOP, ~TOP) and resets itself
through the watchdog; at boot it starts Timer1 with the stored TOP, or 10 Hz if
there is no valid record. EEPROM survives reflashing on this board too.

!!! warning "Reflash both boards together"
    Firmware older than this change does not know `PKT_RATE` or `timer1_init(top)`.
    An old transmitter ignores `F` and stays at 10 Hz. The PLL corrects phase,
    not frequency, so boards at different rates give unusable timing.

!!! note "Faster is not always better"
    The sampling window is capped at 80 % of the period, so a faster rate also
    shortens the maximum measurable range (40 ms at 20 Hz).

## Trigger timing

Rows in `settings-eeprom.h` are settings stored in EEPROM: the value shown is the
default, and the board uses the value stored on it.

| Setting | File | Default | Meaning |
|---------|------|---------|---------|
| `triggerPin` | `timers.cpp` (both) | `5` | Pin pulsed by the Timer1 ISR |
| `TRIGGER_PULSE_US` | `timers.cpp` (both) | `15` | Trigger pulse width. The HC-SR04 datasheet requires 10 µs; 15 µs leaves a margin. |
| `SONIC_BURST_DELAY` | `settings-eeprom.h` (base) | `2000` | **Default** burst delay: microseconds the transmitter waits after its compare match before firing, giving the receivers time to settle into receive mode. `D` stores the delay it finds in its place; `E` stores this default again. |
| `BURST_DELAY_MIN_US` / `MAX_US` | `settings-eeprom.h` (base) | `1` / `16383` | Range the stored burst delay must fall in: 0 is reserved (see below), and 16383 µs is the longest delay the Arduino core's `delayMicroseconds()` produces correctly, a software limit (see `tick_delay` below) |
| `TRIGGER_SEND_GUARD_TICKS` | `settings-eeprom.h` (base) | `125` | A trigger packet is not sent within this many Timer1 ticks (2 ms) of the compare match; the handler waits for the next cycle instead, so the transmitter never fires with a stale delay. At most 3124 (`TRIGGER_SEND_GUARD_MAX_TICKS`, one period at 20 Hz). |
| `tick_delay == 0` | `sendTrigger()` (shared) | reserved | A trigger with a zero delay is refused by the base station and ignored by the transmitter. `SEARCH_DELAY_MIN_US = 0` is therefore measured as 1 µs. |
| `SYNC_PLL_INTERVAL` | `settings-eeprom.h` (base) | `10` | Stored but not used at present: one PLL sync packet is sent per Timer1 cycle, in the second half of the cycle before the trigger send guard. |
| `tick_delay` | `timers.cpp` (transmitter) | set by radio | Applied as `delayMicroseconds()` at the top of the ISR. Limited to 16383 µs by the Arduino core, not by the hardware: at 16 MHz `delayMicroseconds()` multiplies its argument by 4 in 16-bit arithmetic, so a larger value overflows and gives a wrong, usually far shorter, delay. |

## Sampling window

After each trigger, `UltrasonicSampling::measureEchoTimes()`
(`base-station/src/ultrasonic-sampling.cpp`) samples the echo lines in a tight
loop. One pass of that loop is one **tick**, the unit of the printed counts, as
in the original firmware.

How long it samples is bounded by Timer1, not by the tick count. The loop reads
`TCNT1` when it starts and, on every pass, computes how many Timer1 counts have
elapsed (adding one period when `TCNT1` wrapped at the compare match). It stops
once that reaches the limit, computed once before the loop:

```
limit = min(msToCounts(SAMPLING_TIME_MS), 80 % of (timer1_top + 1))
msToCounts(ms) = ms * 1000 / 16
```

`SAMPLING_TIME_MS` is **80 ms** by default and accepts 1 ms up to
`maxSamplingTimeMs()`: 80 % (`SAMPLING_WINDOW_MAX_PERCENT`) of the Timer1 period
at the current trigger rate, never above `SAMPLING_TIME_MAX_MS` (1000 ms).

| Rate | Maximum sampling time |
|------|-----------------------|
| 1 Hz | 800 ms |
| 10 Hz | 80 ms |
| 20 Hz | 40 ms |

`H <ms>` (`H 60` or `H60`) changes it at runtime, stores it, and replies with the
time now in force and the matching number of Timer1 counts; an out-of-range
value changes nothing (see [Usage](usage.md)):

```
H: 80, 5000
```

`F` sets the stored sampling time to the cap of the new rate (`H` can shorten
it afterwards); a stored time above the cap is clamped at boot. A window can therefore never
run into the next trigger pulse.

!!! warning "Check the calibration"
    Ticks are now Timer1 counts (16 µs each), not loop iterations (about 29 µs
    each in the original firmware). Redo any calibration (`A` and `B` in `Ax + B`)
    against known distances after reflashing.

### Transition buffers

The loop saves each change of the echo lines (the new line state and its tick)
in two buffers of `SAMPLE_BUFFER_SIZE` entries, which is compile-time only (each
entry costs 5 bytes of RAM). `MAX_SAVED_TICKS`, stored in EEPROM, caps how many
transitions one measurement saves (1 to `SAMPLE_BUFFER_SIZE`); sampling stops
early once that many have been saved.

```cpp
constexpr uint8_t  SAMPLE_BUFFER_SIZE = 100;
constexpr uint8_t  MAX_SAVED_TICKS = SAMPLE_BUFFER_SIZE;
```

With six receivers and one rising plus one falling edge each, 100 is generous —
but a noisy echo line can burn through it and truncate the measurement.

## Radio

Set in `shared/radio/radio.h` (constants) and `RadioController::init()` in
`shared/radio/radio.cpp`. Because the file is shared, **a change here reaches
both boards on the next build** — rebuild and flash both.

| Setting | Default | Notes |
|---------|---------|-------|
| `RADIO_CHANNEL` | `125` | 0–125. Change to dodge Wi-Fi interference. |
| `RADIO_DATA_RATE` | `RF24_1MBPS` | `RF24_250KBPS` gives more range at the cost of latency |
| `RADIO_PA_LEVEL` | `RF24_PA_HIGH` | Drop to `RF24_PA_LOW` if the modules are close and the supply is marginal |
| `setPayloadSize(sizeof(CommsPacket))` | 8 | A `static_assert` keeps the struct at 8 bytes |
| `setAutoAck(false)` | off | Deliberate — a retried trigger would arrive too late to be useful |
| `setRetries(0, 0)` | none | Same reason |
| `maskIRQ(true, true, false)` | RX only | The IRQ pin asserts only when a packet arrives |
| `PIPE_BASE_TO_TRANSMITTER` | `0xF0F0F0F0E1` | Base → transmitter station. Change if two UltraGPS systems share a room. |
| `PIPE_TRANSMITTER_TO_BASE` | `0xF0F0F0F0E2` | Opened both ways but currently unused |
| `RESET_MAGIC` | `0xA55A` | Value a `PKT_RESET` packet must carry to be honoured |
| `RESET_PACKET_REPEATS` | `3` | Copies of the reset packet sent per `R` command |

Which end of the link a board is comes from the `RadioRole` passed to
`RadioController::init()` in each `main.cpp`: `RADIO_ROLE_BASE` opens the pipes
for writing to the transmitter station, `RADIO_ROLE_TRANSMITTER` opens them the
other way round and starts listening.

The `D` (delay search) command's parameters are settings stored in EEPROM, with
their defaults in `base-station/src/settings-eeprom.h`. No command changes them
yet; edit the default and send `M`.

| Setting | Default | Meaning |
|---------|---------|---------|
| `SEARCH_DELAY_MIN_US` | `0` | First burst delay tried |
| `SEARCH_DELAY_MAX_US` | `500` | Last burst delay tried (inclusive) |
| `SEARCH_DELAY_STEP_US` | `10` | Step between tries |
| `SEARCH_SAMPLES_PER_STEP` | `3` | Pulses averaged at each delay |
| `SEARCH_PRINT_STEPS` | `true` | Print a `D:` line for every step, not only the winner. The winner's line is always the last one. |

The winner is stored as the burst delay and used for every measurement from
then on, including after a reset or power cycle, until `E` stores
`SONIC_BURST_DELAY` again or `M` resets every setting. If no step produced a
single valid reading, nothing is stored.

The `R` command's own behaviour is set the same way:

| Setting | Default | Meaning |
|---------|---------|---------|
| `RESET_SELF_AFTER_REMOTE_RESET` | `false` | After sending the reset packet, reset the base station too. With the default, `R` resets only the transmitter station. |
| `RESET_SELF_DELAY_MS` | `100` | Delay before the self-reset, so the `N: R` line gets out. Only used when `RESET_SELF_AFTER_REMOTE_RESET` is `true`. |

Radio pins are set where the controller is constructed in `main.cpp`:

```cpp
RadioController radioCtrl(9, 10);   // CE, CSN
constexpr uint8_t radioIRQPin = 2;  // transmitter only; must be pin 2 or 3
```

## LEDs

```cpp
LedControl leds(3, 4, 6, 7, 8);   // STAT_1, STAT_2, STAT_3, PULSE_TRIG, RADIO_STATUS
```

Both sketches call `leds.init(0)` in `setup()`, which skips the startup
animation. Change it to `leds.init(1)` to run the ~2.6 s LED chase on every
reset.

## Debug output

`debug_macros.h` (a separate copy per project) gates all diagnostic printing.
The base station's copy has:

```cpp
// Enable debug mode (uncomment the line below)
//#define DEBUG
```

The transmitter station's copy has the same `//#define DEBUG` line, under the
comment `//! Enable debug mode`. Neither copy has an `INFO_DEBUG` line: to get
only the `DEBUG_INFO_*` messages, add `#define INFO_DEBUG` next to it.

| Macro | Prints |
|-------|--------|
| *(neither)* | Default. The base station prints only its command replies. If the radio fails to initialise, both boards print the reason (`[RADIO] ...`) and the base station also prints its `[ERROR]` halt messages. |
| `INFO_DEBUG` | The `DEBUG_INFO_*` status messages. Base station: at boot, whether the settings were loaded from EEPROM or the defaults written, and the burst delay in force; the Timer1 value and delay of every trigger packet; each reset packet sent; a refused zero-delay trigger and failed trigger or reset packets; a `[CMD]` line for each `R`, `D`, `E`, `M` and `H` command. Transmitter station: every trigger fired, a reset packet received, and its `[ERROR]` halt messages if the radio fails to initialise. |
| `DEBUG` | Both sets — adds the verbose `DEBUG_*` diagnostics: a start-up line, radio init progress (`[RADIO] Initializing...`, `[RADIO] OK.`), failed sync packets, `Simulation mode activated` for each simulated measurement, and after every measurement `nwp`, the highest tick, the max `PINC` value and its index. On the base station it also slows the sampling loop; see [Sampling window](#sampling-window). |

!!! note "Debug output corrupts the data stream"
    Diagnostics go to the same serial port as the measurements, so a program
    parsing the output — such as the ground-station `UltraGpsXY` — will see
    interleaved lines it cannot decode. Enable these only when driving the board
    from a human-readable serial monitor.

## Simulation mode

The base station can produce plausible fake tick counts without any working
ultrasonic hardware, which is the fastest way to test the serial link and any
program consuming it.

There are two ways in:

**The `S` command** — always available. Runs the real radio and trigger path but
prints simulated values instead of measured ones.

**The `SIMULATE_REAL_COMMANDS` macro** — in `base-station/src/command_control.h`:

```cpp
//#define SIMULATE_REAL_COMMANDS
```

Uncomment it to make `P` and `C` behave like `S` too, so every `N:` and `C:`
line carries simulated data. Use this when the consuming program only issues `P`
and `C` and you do not want to modify it.

Simulated values come from `UltrasonicSampling::simulateEchoTimes()`: a base of
`401` counts with a ±50 deterministic variation per receiver that shifts on each
call.

```cpp
constexpr unsigned long BASE_COUNT = 401;
constexpr int VARIATION = 50;
```

!!! note "Simulation still needs the radio"
    `onSimulatePulse()` calls `handleUltrasonics(false)` before faking the
    numbers, so the radio must still initialise or the sketch halts in `setup()`.
    It does not need the ultrasonic hardware or the receivers.
