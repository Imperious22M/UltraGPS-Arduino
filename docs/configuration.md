# Configuration

All configuration is **compile-time**. Neither sketch reads a config file or
accepts runtime settings, so every change on this page requires editing a source
file and re-uploading — see [Uploading](uploading.md).

## Where each setting lives

| File | Controls |
|------|----------|
| `<project>/platformio.ini` | Board, serial ports, monitor baud, libraries |
| `<project>/src/main.cpp` | Baud rate, pin assignments, command table, sampling window, simulation |
| `<project>/src/timers.cpp` | Pulse rate, trigger pin, trigger pulse width |
| `<project>/src/command_control.h` | Continuous-mode rate, command buffer limits (base station only) |
| `<project>/src/radio.{h,cpp}` | Radio channel, data rate, power, pipe addresses |
| `<project>/src/debug_macros.h` | Debug output level |

!!! note "Some settings must be changed on both boards"
    The baud rate, pulse rate, and radio parameters exist independently in each
    project. Change one and the boards stop agreeing. Each entry below says
    whether it needs to be kept in sync.

## PlatformIO project settings

`base-station/platformio.ini`:

```ini
[env:uno]
platform = atmelavr
board = uno
framework = arduino
lib_deps = nRF24/RF24
monitor_speed = 38400

monitor_port = /dev/ttyACM0
upload_port = /dev/ttyACM0
```

| Key | Default (base / transmitter) | Notes |
|-----|------------------------------|-------|
| `board` | `uno` | Change for a different AVR board. The `PINC` reads and Timer1 values assume an ATmega328P at 16 MHz. |
| `lib_deps` | `nRF24/RF24` | Downloaded on first build |
| `monitor_speed` | `38400` | Must match `BAUD_RATE` in `main.cpp` |
| `monitor_port` | `/dev/ttyACM0` / `/dev/ttyACM1` | Port the serial monitor opens |
| `upload_port` | `/dev/ttyACM0` / `/dev/ttyACM1` | Port `--target upload` flashes |

Override the upload port for a single run instead of editing the file:

```bash
pio run -d base-station --target upload --upload-port /dev/ttyACM2
```

## Serial

| Setting | File | Default | Notes |
|---------|------|---------|-------|
| `BAUD_RATE` | `main.cpp` (both) | `38400` | **Keep in sync** across both sketches, both `platformio.ini` files, and the ground-station program's `--baud-rate` |
| `CMD_BUF_SIZE` | `command_control.h` | `64` | Longest accepted command line, including the NUL. Longer lines are discarded, not truncated. |
| `CMD_MAX_ARGS` | `command_control.h` | `4` | Maximum arguments any one command can carry |

## Pulse rate

Both boards call a `timer1_init_*()` function from `setup()`. `timers.cpp`
provides four; the default on both is 10 Hz:

```cpp
timer1_init_10Hz();   // in setup(), base-station and transmitter-station
```

| Function | Rate | `OCR1A` |
|----------|------|---------|
| `timer1_init_5Hz()` | 5 Hz | 12498 |
| `timer1_init_10Hz()` | 10 Hz | 6249 |
| `timer1_init_15Hz()` | ≈ 14.999 Hz | 4166 |
| `timer1_init_20Hz()` | 20 Hz | 3124 |

**Change this on both boards.** The PLL corrects phase, not frequency — if the
boards run at different rates the transmitter is yanked back into phase every
packet and the timing is unusable.

All four use CTC mode with a prescaler of 256, so one tick is 16 µs. To add a
rate, copy one of the functions and set `OCR1A = 16000000 / 256 / f - 1`, then
assign the same value to `timer1_top`.

!!! note "Faster is not always better"
    The sampling window in `measure_echo_times()` is capped at 80 % of
    `timer1_top`, so raising the rate shortens the window and reduces the
    maximum measurable range. At 20 Hz the window is roughly half of what it is
    at 10 Hz.

## Trigger timing

| Setting | File | Default | Meaning |
|---------|------|---------|---------|
| `triggerPin` | `timers.cpp` (both) | `5` | Pin pulsed by the Timer1 ISR |
| `TRIGGER_PULSE_US` | `timers.cpp` (both) | `50` | Trigger pulse width. The HC-SR04 datasheet requires 10 µs; 50 µs is a safety margin. |
| `SONIC_BURST_DELAY` | `main.cpp` (base, inside `handleUltrasonics()`) | `2*1000` | Microseconds the transmitter waits after the packet before firing, giving the receivers time to settle into receive mode |
| `SYNC_PLL_INTERVAL` | `main.cpp` (base) | `10` | Milliseconds between PLL sync packets |
| `tick_delay` | `timers.cpp` (transmitter) | set by radio | Applied as `delayMicroseconds()` at the top of the ISR. Hard-capped at 16383 µs by `delayMicroseconds()`. |

## Receivers

```cpp
constexpr uint8_t ECHO_PIN[] = {A0, A1, A2, A3, A4, A5};
constexpr uint8_t RECV_CNT   = sizeof(ECHO_PIN)/sizeof(ECHO_PIN[0]);
#define echo_value() (PINC & 63)
```

`RECV_CNT` follows the array automatically, but `echo_value()`, the `this_ticks`
arrays, and the print statement in `handleUltrasonics()` are all hard-coded to
six channels on `PORTC`. Using fewer than six receivers works if you leave the
unused pins pulled up — they simply never transition and report `0`. Using
**more** than six, or moving a receiver off `A0`–`A5`, requires editing the
measurement and printing code as well. See [Notes](notes.md).

## Sampling window

Inside `measure_echo_times()`:

```cpp
constexpr long max_tick_val = 0x31000/4;   //! About 60 ms of sampling time
```

50 176 iterations of the polling loop, whichever comes first with the 80 %
timer cut-off. Raising it extends the maximum measurable distance; it cannot
usefully exceed the pulse period.

The transition buffers are fixed at 100 entries:

```cpp
static byte value[100];
static unsigned long ticks[100];
```

Sampling stops early if all 100 fill up. With six receivers and one rising plus
one falling edge each, 100 is generous — but a noisy echo line can burn through
it and truncate the measurement.

## Radio

Set in `RadioController::init()` in `radio.cpp`, and identical on both boards
apart from the pipe direction. **Every one of these must match on both boards.**

| Setting | Default | Notes |
|---------|---------|-------|
| `setChannel(100)` | `100` | 0–125. Change on both boards to dodge Wi-Fi interference. |
| `setDataRate(RF24_1MBPS)` | 1 Mbps | `RF24_250KBPS` gives more range at the cost of latency |
| `setPALevel(RF24_PA_HIGH)` | HIGH | Drop to `RF24_PA_LOW` if the modules are close and the supply is marginal |
| `setPayloadSize(8)` | 8 | Must equal `sizeof(CommsPacket)` |
| `setAutoAck(false)` | off | Deliberate — a retried trigger would arrive too late to be useful |
| `setRetries(0, 0)` | none | Same reason |
| `PIPE_TX_TO_RX` | `0xF0F0F0F0E1` | Base → transmitter. Change on both boards if two UltraGPS systems share a room. |
| `PIPE_RX_TO_TX` | `0xF0F0F0F0E2` | Opened both ways but currently unused |

Radio pins are set where the controller is constructed in `main.cpp`:

```cpp
RadioController radioCtrl(9, 10);   //! CE, CSN
constexpr uint8_t radioIRQPin = 2;  //! transmitter only; must be pin 2 or 3
```

## LEDs

```cpp
LedControl leds(3, 4, 6, 7, 8);   //! STAT_1, STAT_2, STAT_3, PULSE_TRIG, RADIO_STATUS
```

Both sketches call `leds.init(0)` in `setup()`, which skips the startup
animation. Change it to `leds.init(1)` to run the ~2.5 s LED chase on every
reset — handy for spotting unexpected reboots, but it delays the first
measurement by that long.

## Debug output

`debug_macros.h` (a separate copy per project) gates all diagnostic printing:

```cpp
//! Enable debug mode
//#define DEBUG
```

| Macro | Uncomment to get |
|-------|------------------|
| *(neither)* | Default. Only measurement output and fatal radio errors are printed. |
| `INFO_DEBUG` | `DEBUG_INFO_*` messages: radio init, packet send failures, trigger-fired notices |
| `DEBUG` | Both sets — adds `DEBUG_*` verbose diagnostics (`nwp`, highest tick, max `PINC` value) |

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

**The `SIMULATE_REAL_COMMANDS` macro** — at the top of `base-station/src/main.cpp`:

```cpp
//! Definition to simulate "real" commands with pulse data
//#define SIMULATE_REAL_COMMANDS
```

Uncomment it to make `P` and `C` behave like `S` too, so *every* command
returns simulated data. Use this when the consuming program only issues `P` and
`C` and you do not want to modify it.

Simulated values come from `simulate_echo_times()`: a base of `401` counts with
a ±50 deterministic variation per receiver that shifts on each call.

```cpp
constexpr unsigned long BASE_COUNT = 401;
constexpr int VARIATION = 50;
```

!!! note "Simulation still needs the radio"
    `onSimulatePulse()` calls `handleUltrasonics(false)` before faking the
    numbers, so the radio must still initialise or the sketch halts in `setup()`.
    It does not need the ultrasonic hardware or the receivers.

## Declared but unused

These constants are present in the sources but nothing reads them. They are
listed here so you do not change one and expect an effect:

| Constant | File |
|----------|------|
| `TRIGGER_HIGH_DURATION` (300 µs) | `base-station/src/main.cpp` |
| `TRIGGER_TICK_DELAY` (100 ticks) | `base-station/src/main.cpp` |
| `radioIRQPin` (pin 2) | `base-station/src/main.cpp` — the base station never attaches the interrupt |
| `LATENCY_COMPENSATION_US` (4000 µs) | `radio.h`, both projects |
