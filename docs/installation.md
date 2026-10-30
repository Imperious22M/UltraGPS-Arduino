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
[Uploading](uploading.md) for how to tell them apart. The base station keeps its
settings in EEPROM, which flashing does not erase; its burst delay, sampling
time and trigger rate can be changed over serial, and `M` restores the defaults (see
[Usage](usage.md)). Changing any other setting requires building from source
(and, for a base-station setting, sending `M` after flashing) — see
[Configuration](configuration.md).

## Prerequisites for building from source

- **PlatformIO Core** (`pio`) — the primary toolchain; each project is a
  PlatformIO project. Needs Python 3.
- **`arduino-cli`** — optional, only needed for flashing loose `.hex` files.
- **`git`** — to clone the repository.

The AVR compiler toolchain and the `RF24` library are downloaded automatically
by PlatformIO on the first build.

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

The Arduino usually appears as `/dev/ttyACM<n>` (or `/dev/ttyUSB<n>` on CH340 clones). Your user must be in the
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

To clean:

```bash
pio run -d base-station --target clean
```
