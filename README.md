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

python3 -m venv ~/.venvs/platformio
~/.venvs/platformio/bin/pip install platformio
export PATH="$HOME/.venvs/platformio/bin:$PATH"
```

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

Each value is one receiver's tick count, in `A0`–`A5` order.

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
| `C` | Enter continuous mode — a stream of continous responses `C:` |
| `S` | Simulates a `P` command and prints **simulated** counts |
| `R` | Resets the transmitter station over the radio, replies `N: R`, then resets the base station |

Commands must be terminated with a newline or carriage return. Any `P`, `S` or `R` command ends
continuous mode.

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

All configuration are compile-time:

| Setting | Where |
|---------|-------|
| Serial port used for upload/monitor | `<project>/platformio.ini` |
| Baud rate (38400) | `BAUD_RATE` in `<project>/src/main.cpp` **and** `monitor_speed` |
| Pulse rate (10 Hz) | the `timer1_init_*Hz()` call in `setup()` — **Needs change on both boards** |
| Radio channel, power, pipes | `shared/radio/radio.h` and `RadioController::init()` in `shared/radio/radio.cpp` — **shared by both boards** |
| Debug output | `#define DEBUG` / `INFO_DEBUG` in `<project>/src/debug_macros.h` |
| Simulated data | the `S` command, or `SIMULATE_REAL_COMMANDS` in `base-station/src/main.cpp` |

See the **[Configuration](https://imperious22m.github.io/UltraGPS-Arduino/configuration/)**
page for the complete reference.

## Developer's Guide

See the **[Developer's Guide](https://imperious22m.github.io/UltraGPS-Arduino/developer-guide/)**
in the documentation for more details.
