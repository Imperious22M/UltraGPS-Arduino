# UltraGPS Arduino Firmware

Arduino firmware for the St. Mary's University **UltraGPS** positioning system,
developed as part of graduate research.

## What it does

The repository holds the two sketches that run on the system's
microcontrollers:

- **`base-station/`** — accepts single-letter commands over USB serial
  (38400 bps), sends a trigger packet to the transmitter over a 2.4 GHz
  nRF24L01+ link, times the ultrasonic echoes returned by six receivers on
  `A0`–`A5`, and prints the resulting tick counts back over serial.
- **`transmitter-station/`** — listens for those radio packets, corrects its
  Timer1 phase to stay locked to the base station, and pulses the ultrasonic
  transmitter's trigger pin.

Both are PlatformIO projects targeting an Arduino Uno. The radio layer they
share (pipe addresses, packet framing, send/receive) lives once in `shared/`
and is compiled into both via `lib_extra_dirs`. The only third-party
dependency, [`nRF24/RF24`](https://github.com/nRF24/RF24), is declared in
`lib_deps` and downloaded on the first build.

The base station is the board that the
[UltraGPS Ground Station](https://github.com/Imperious22M/UltraGPS-Ground)
program (`UltraGpsXY`) talks to over USB serial.

See the [full documentation](https://imperious22m.github.io/UltraGPS-Arduino/)
for the pin map, configuration reference, command set, and design notes.

## Requirements

- **PlatformIO Core** (`pio`) with Python 3 — builds and flashes the firmware
- **`arduino-cli`** — optional, for flashing loose `.hex` files
- **`git`** 

```bash
git clone https://github.com/Imperious22M/UltraGPS-Arduino.git
cd UltraGPS-Arduino
./build.sh build
```

`build.sh` needs only `python3` with the `venv` module (`sudo apt install
python3-venv` on Debian/Ubuntu). It uses a working `pio` if you have one;
otherwise it installs PlatformIO into `.venv-platformio/` in the repository on
first use. Ubuntu's apt `platformio` package (4.3.4) is skipped automatically:
it crashes with the `click` version Ubuntu 24.04 ships.

On Linux your user must be in the `dialout` group to reach the serial port:

```bash
sudo usermod -aG dialout "$USER"   # After running this log out and back in
```

## Quick start

You don't need to build anything to run the firmware — each release ships
prebuilt `.hex` images. Download them from the
**[Releases page](https://github.com/Imperious22M/UltraGPS-Arduino/releases/latest)**
and flash each board:

```bash
# For the base-station:
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno \
    --input-file ultragps-base-station-v1.0.hex

# For the transmitter-station
arduino-cli upload -p /dev/ttyACM1 -b arduino:avr:uno \
    --input-file ultragps-transmitter-station-v1.0.hex
```

Then open a serial monitor at 38400 bps and send the "pulse" command `P`:

Example response (may be all 0's if no receiver towers are connected):
```
N: 4404, 3226, 4063, 4041, 3133, 4109
```

Each value is one receiver's tick count, in `A0`–`A5` order. A tick is one pass
of the base station's sampling loop, as in the original firmware. The loop is
bounded by Timer1 (`TCNT1`), not by the count, so the duration of one tick
depends on the compiled loop: check any `Ax + B` calibration against known
distances after reflashing.

## Using the build script

`build.sh` is the single entry point for building, flashing, cleaning,
packaging, and previewing the docs. Run it from the repository root:

```bash
./build.sh <command> [options]
```

### Commands

| Command | What it does |
|---------|--------------|
| `build` | Compile the firmware (both stations by default) |
| `upload` | Compile and flash one station |
| `monitor` | Open a 38400 bps serial monitor on one station |
| `clean` | Remove the `.pio/` build directories and `./dist` |
| `dist` | Build both stations and collect the release artefacts into `./dist` |
| `docs` | Preview the documentation locally with live reload |
| `docs-build` | Render the static documentation site into `./site/` |

### Options

| Option | Applies to | Effect |
|--------|-----------|--------|
| `-s`, `--station` | `build`, `upload`, `monitor`, `clean` | `base`, `transmitter`, or `all` (default `all`; `upload` and `monitor` require a single station) |
| `-p`, `--port` | `upload`, `monitor` | Override the port set in `platformio.ini` |
| `-h`, `--help` | — | Show usage |

### Compiling and flashing

```bash
./build.sh build                      # both stations
./build.sh build -s base              # base station only

./build.sh upload -s base             # compile and flash the base station
./build.sh upload -s transmitter -p /dev/ttyACM2
```

Each project's image lands at `<project>/.pio/build/uno/firmware.hex`. The
default ports are `/dev/ttyACM0` for the base station and `/dev/ttyACM1` for the
transmitter — run `arduino-cli board list` to confirm which board is on which
port before flashing.

Using Platform IO (recommended):

```bash
pio run -d base-station
pio run -d base-station --target upload
```

### Watching the serial output

```bash
./build.sh monitor -s base
```

| Command | Effect |
|---------|--------|
| `P` | Fire one pulse and print one normal command `N:` |
| `C` | Enter continuous mode — a stream of continuous responses `C:` |
| `S` | Simulates a `P` command and prints **simulated** counts |
| `R` | Resets the transmitter station over the radio and replies `N: R`. The base station keeps running unless `RESET_SELF_AFTER_REMOTE_RESET` is set (off by default) |
| `D` | Sweeps the burst delay (0–500 µs by default), prints one `D:` line per step and then the best one, and stores it in EEPROM for future measurements (takes ~15 s) |
| `E` | Stores the default burst delay (`SONIC_BURST_DELAY`, 2000 µs) again; replies `E: <delay>` |
| `H <ms>` | Sets the echo sampling time in milliseconds (1 ms up to 80 % of the Timer1 period at the current trigger rate: 80 at 10 Hz, 40 at 20 Hz, 800 at 1 Hz; default 80) and stores it in EEPROM; replies `H: <ms>, <Timer1 counts>`, e.g. `H: 80, 5000`. An out-of-range value changes nothing |
| `F <Hz>` | Sets the trigger (pulse) rate, 1–50 Hz (default 10), on **both** stations: stores it in EEPROM, restarts Timer1, sends the new rate to the transmitter (which stores it and resets) and caps the sampling time to 80 % of the new period; replies `F: <Hz>, <Timer1 TOP>, <sampling ms>`, e.g. `F: 20, 3124, 40` |
| `M` | Resets every setting stored in EEPROM to its default; replies `M: OK` |

Commands must be terminated with a newline or carriage return. Any `P`, `S`, `R`,
`D`, `E`, `H`, `F` or `M` command ends continuous mode. Sampling always stops
at 80 % of the Timer1 period (or earlier, at the `H` time), so a window can
never run into the next trigger pulse.

### Building the release artefacts

```bash
./build.sh dist             # → ./dist
```

Produces the two `.hex` images and a zip of the sketch sources, named from the
`VERSION` file at the repository root.

### Cleaning

```bash
./build.sh clean            # removes both .pio/ directories and ./dist
```

## Configuration

The base station's own settings (burst delay, sampling time, baud rate, echo
pins, the `D` sweep, the `R` behaviour and more) are stored in its **EEPROM**.
Their defaults are in `base-station/src/settings-eeprom.h`; the first boot
writes them, and `D`, `E`, `H`, `F` and `M` change them at runtime. EEPROM survives
reflashing, so after editing a default, send `M` (or change
`SETTINGS_LAYOUT_VERSION`, now `2`) for the board to use it. The transmitter
station stores only the trigger rate it was sent by `F`. Everything else is compile-time: rebuild and re-upload to
change it.

| Setting | Where |
|---------|-------|
| Serial port used for upload/monitor | `<project>/platformio.ini` |
| Baud rate (38400) | Base station: `BAUD_RATE` default in `base-station/src/settings-eeprom.h`, stored in EEPROM. Transmitter: `BAUD_RATE` in `transmitter-station/src/main.cpp`. **And** `monitor_speed` |
| Burst delay (2000 µs) | The `D` and `E` commands, stored in EEPROM; default `SONIC_BURST_DELAY` in `base-station/src/settings-eeprom.h` |
| Sampling time (80 ms) | The `H <ms>` command, stored in EEPROM; default `SAMPLING_TIME_MS` in `base-station/src/settings-eeprom.h` |
| Pulse rate (10 Hz) | The `F <Hz>` command at runtime (1–50 Hz), stored in both boards' EEPROM; default `TRIGGER_RATE_HZ` in `shared/radio/radio.h`. Both boards must run firmware that knows `PKT_RATE`, so **reflash them together** |
| Radio channel, power, pipes | `shared/radio/radio.h` and `RadioController::init()` in `shared/radio/radio.cpp` — **shared by both boards** |
| Debug output | `#define DEBUG` / `INFO_DEBUG` in `<project>/src/debug_macros.h` |
| Simulated data | the `S` command, or `SIMULATE_REAL_COMMANDS` in `base-station/src/command_control.h` |

See the **[Configuration](https://imperious22m.github.io/UltraGPS-Arduino/configuration/)**
page for the complete reference.

## Developer's Guide

See the **[Developer's Guide](https://imperious22m.github.io/UltraGPS-Arduino/developer-guide/)**
in the documentation for more details.
