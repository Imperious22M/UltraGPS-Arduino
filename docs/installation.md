# Installation

There are two ways to get firmware onto a board: flash a prebuilt `.hex` from a
release, or build the sketches from source. Flashing a release needs no build
toolchain at all.

## Flash from a release (recommended)

Each tagged version is published on the
[Releases page](https://github.com/Imperious22M/UltraGPS-Arduino/releases) with
three assets attached:

| Asset | What it is |
|-------|------------|
| `ultragps-base-station-vX.Y.hex` | Prebuilt base-station firmware |
| `ultragps-transmitter-station-vX.Y.hex` | Prebuilt transmitter-station firmware |
| `ultragps-arduino-sources-vX.Y.zip` | The Arduino sketches and their PlatformIO project files |

Download the two `.hex` files and flash them with `arduino-cli`:

```bash
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno \
    --input-file ultragps-base-station-v1.0.hex

arduino-cli upload -p /dev/ttyACM1 -b arduino:avr:uno \
    --input-file ultragps-transmitter-station-v1.0.hex
```

Take care to send each image to the right board — see
[Uploading](uploading.md) for how to tell them apart, and
[Configuration](configuration.md) if you need to change a setting (which does
require building from source).

## Prerequisites for building from source

- **PlatformIO Core** (`pio`) — the primary toolchain; each project is a
  PlatformIO project. Needs Python 3.
- **`arduino-cli`** — optional, only needed for the `base-station/upload`
  helper script and for flashing loose `.hex` files.
- **`git`** — to clone the repository.

The AVR compiler toolchain and the `RF24` library are downloaded automatically
by PlatformIO on the first build; nothing is vendored in the repository.

### Installing PlatformIO Core

```bash
python3 -m venv ~/.venvs/platformio
~/.venvs/platformio/bin/pip install platformio
# Optional: put `pio` on your PATH
export PATH="$HOME/.venvs/platformio/bin:$PATH"
```

The official installer script is an alternative:

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o get-platformio.py
python3 get-platformio.py
```

### Installing arduino-cli

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
arduino-cli core install arduino:avr
```

### Serial port permissions (Linux)

The Uno appears as `/dev/ttyACM0`, `/dev/ttyACM1`, … Your user must be in the
`dialout` group to open it:

```bash
sudo usermod -aG dialout "$USER"
```

Log out and back in for the group change to take effect.

## Get the sources

```bash
git clone https://github.com/Imperious22M/UltraGPS-Arduino.git
cd UltraGPS-Arduino
```

There are no submodules — a plain clone is complete.

## Build

Each station is built separately with `pio run -d <project>`:

```bash
pio run -d base-station
pio run -d transmitter-station
```

The first build takes a couple of minutes while PlatformIO downloads the
`atmelavr` platform and the `RF24` library; later builds take seconds. Each
project's artefacts land in `<project>/.pio/build/uno/`, with the flashable
image at `firmware.hex`.

A clean build of the current sources fits comfortably on an Uno:

| Project | Flash | RAM |
|---------|-------|-----|
| `base-station` | 10230 B (31.7 % of 32256 B) | 923 B (45.1 % of 2048 B) |
| `transmitter-station` | 4242 B (13.2 %) | 242 B (11.8 %) |

To clean:

```bash
pio run -d base-station --target clean
```

!!! note "`.pio/` is not tracked"
    Each project's `.gitignore` excludes `.pio/`, so build output and the
    downloaded `RF24` copy never enter the repository. Deleting the directory is
    always safe.
