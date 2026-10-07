# Developer's Guide

A self-contained reference for developers who **fork the repository** and need
to work with the project's tooling — building, flashing, previewing the
documentation, extending the firmware, and cutting releases. Everything needed
is on this page.

## Overview

The repository holds two Arduino sketches, one per microcontroller in the
UltraGPS positioning system:

- **`base-station/`** — accepts single-letter commands over USB serial, sends
  the trigger packet over the nRF24L01+ link, times the echoes returned by six
  ultrasonic receivers on `A0`–`A5`, and prints the counts back over serial.
  This is the board the ground-station program (`UltraGpsXY`) talks to.
- **`transmitter-station/`** — listens for those radio packets, corrects its
  Timer1 phase to match the base station, and pulses the ultrasonic
  transmitter's trigger pin.

## Requirements

- **PlatformIO Core** (`pio`) with Python 3 — builds and flashes the firmware
- **`arduino-cli`** — optional, for flashing loose `.hex` files
- **`git`** — to clone the repository (no submodules)
- **Python 3 with `pip`** — only needed to build/preview the documentation

```bash
git clone https://github.com/Imperious22M/UltraGPS-Arduino.git
cd UltraGPS-Arduino

python3 -m venv ~/.venvs/platformio
~/.venvs/platformio/bin/pip install platformio
export PATH="$HOME/.venvs/platformio/bin:$PATH"
```

## Flashing a prebuilt release

You do not need a build toolchain to run the firmware — each release ships
prebuilt `.hex` images. Download them from the
[Releases page](https://github.com/Imperious22M/UltraGPS-Arduino/releases/latest)
and flash with `arduino-cli`:

```bash
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno \
    --input-file ultragps-base-station-v1.0.hex
```

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

### Compiling the firmware

```bash
./build.sh build              # both stations
./build.sh build -s base      # base station only
```

Each project's image lands at `<project>/.pio/build/uno/firmware.hex`.

### Flashing

```bash
./build.sh upload -s base
./build.sh upload -s transmitter -p /dev/ttyACM2
```

The default ports are `/dev/ttyACM0` for the base station and `/dev/ttyACM1`
for the transmitter; see [Uploading](uploading.md) for how to confirm which is
which.

### Watching the serial output

```bash
./build.sh monitor -s base
```

Then send `P`, `C`, `S`, or `R` — see [Usage](usage.md).

### Building the release artefacts

```bash
./build.sh dist
```

Produces, in `./dist`:

- `ultragps-base-station-v<VERSION>.hex`
- `ultragps-transmitter-station-v<VERSION>.hex`
- `ultragps-arduino-sources-v<VERSION>.zip`

The version comes from the `VERSION` file at the repository root. The source zip
is produced with `git archive`, so it contains only committed files — commit
your changes before running it.

### Cleaning

```bash
./build.sh clean              # removes both .pio/ directories and ./dist
./build.sh clean -s base      # base station only
```

## Working on the firmware

### Project layout

```
shared/                 libraries compiled into BOTH projects
  radio/
    radio.*             nRF24L01+ setup, pipe addresses, CommsPacket framing
    library.json        declares the RF24 dependency
  sysreset/
    sysreset.h          watchdog-based software reset
base-station/
  platformio.ini        board, ports, libraries, lib_extra_dirs = ../shared
  src/
    main.cpp            setup/loop, command table, echo measurement
    command_control.*   serial line buffering and dispatch
    timers.*            Timer1 CTC setup, TIMER1_COMPA ISR, PLL correction
    led_control.*       status LEDs
    debug_macros.h      compile-time-gated printing
transmitter-station/
  ...                   the same, minus command_control.*
```

!!! note "Radio code is shared; the other modules are still copies"
    Each `platformio.ini` sets `lib_extra_dirs = ../shared`, so PlatformIO's
    library finder compiles `shared/radio` and `shared/sysreset` into both
    firmware images. `build_flags = -I src` lets the shared code include the
    project's `debug_macros.h`. `timers.*`, `led_control.*` and
    `debug_macros.h` are still per-project copies, and a change to one does not
    reach the other.

### Debugging

Uncomment `#define DEBUG` (or `#define INFO_DEBUG`) in the relevant project's
`debug_macros.h` and rebuild. Remember that diagnostics share the serial port
with the measurement output, so anything parsing that output will choke — see
[Configuration](configuration.md).

For testing the serial protocol without working ultrasonic hardware, use the
`S` command or the `SIMULATE_REAL_COMMANDS` macro.

## Previewing the documentation

The docs are built with [MkDocs](https://www.mkdocs.org/). To preview them
locally without pushing:

```bash
pip install -r docs/requirements.txt    # one-time: install MkDocs
./build.sh docs                         # live preview at http://127.0.0.1:8000
./build.sh docs-build                   # render static site into ./site/
```

`mkdocs.yml` sets `strict: true`, so a broken internal link or a page missing
from the nav fails the build.

Pushing documentation changes to the `release` branch automatically rebuilds and
republishes the site via GitHub Actions (`.github/workflows/docs.yml`), which
runs `mkdocs gh-deploy` onto the `gh-pages` branch.

## Releasing a version

Released versions live in the repository's **Releases** tab, each with the two
`.hex` images and a zip of the sketch sources attached. The `release` branch is
the stable snapshot of the most recent release.

Releases are made by pushing a **version tag** (`vX.Y`). Pushing the tag triggers
the `Release` workflow (`.github/workflows/release.yml`), which builds both
stations with PlatformIO and publishes a GitHub Release with the artefacts
attached.

To make a release:

1. Bump the `VERSION` file at the repository root so it matches the tag you
   intend to push — `1.0` for tag `v1.0`. The release workflow **fails** if the
   tag and `VERSION` disagree, so this must be done first.
2. Push that commit onto the `release` branch:

    ```
    git checkout release
    git merge --ff-only <your-dev-branch>
    git push origin release
    ```

3. Tag the release commit and push the tag:

    ```
    git tag vX.Y            # e.g. v1.0 — must match the VERSION file
    git push origin vX.Y
    ```

The workflow then builds both images and publishes a release named "UltraGPS
Arduino Firmware vX.Y" with auto-generated notes and these assets:

| Asset | Contents |
|-------|----------|
| `ultragps-base-station-vX.Y.hex` | Base-station firmware image |
| `ultragps-transmitter-station-vX.Y.hex` | Transmitter-station firmware image |
| `ultragps-arduino-sources-vX.Y.zip` | Both sketches and their PlatformIO project files |

Users can then flash without building anything:

```bash
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno \
    --input-file ultragps-base-station-vX.Y.hex
```

To verify locally what the workflow will produce, run `./build.sh dist` and
inspect `./dist`.
