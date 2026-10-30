# Uploading

Flashing a sketch means picking the right project **and** the right serial port.
The two boards are identical Arduino Unos, so nothing prevents the
base-station firmware from being flashed on the transmitter board.

## Identify the boards

Listing everything currently attached:

```bash
arduino-cli board list
```
Example output:
```
Port         Protocol Type              Board Name  FQBN            Core
/dev/ttyACM0 serial   Serial Port (USB) Arduino Uno arduino:avr:uno arduino:avr
/dev/ttyACM1 serial   Serial Port (USB) Arduino Uno arduino:avr:uno arduino:avr
```

PlatformIO can do the same:

```bash
pio device list
```

Ports are assigned in the order the boards are plugged in, not by role, so
**`/dev/ttyACM0` is not permanently the base station**. If unsure which
is which, unplug one and re-run the command. The repository's defaults assume:

| Project | Default port |
|---------|--------------|
| `base-station` | `/dev/ttyACM0` |
| `transmitter-station` | `/dev/ttyACM1` |

Those defaults live in each project's `platformio.ini` — change them if
the boards enumerate differently (see [Configuration](configuration.md)).

## Upload with PlatformIO (recommended)

From the repository root:

```bash
pio run -d base-station --target upload
pio run -d transmitter-station --target upload
```

This compiles and flashes in one step, using the `upload_port` from that
project's `platformio.ini`. To override the port for a single run:

```bash
pio run -d base-station --target upload --upload-port /dev/ttyACM1
```

A successful upload ends with `[SUCCESS]` and an `avrdude` verification line.

## Upload with arduino-cli

`arduino-cli` cannot build these sketches: `arduino-cli compile` needs a `.ino`
file, which `src/` does not have, and the `shared/` libraries are only found
through PlatformIO's `lib_extra_dirs`. Build with PlatformIO (above). What
`arduino-cli` can do is flash a firmware image: a prebuilt `.hex` from a
release, or the `firmware.hex` PlatformIO writes to `<project>/.pio/build/uno/`:

```bash
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno \
    --input-file ultragps-base-station-v1.0.hex
```

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| `Permission denied: /dev/ttyACM0` | Your user is not in the `dialout` group — see [Installation](installation.md) |
| `avrdude: stk500_recv(): programmer is not responding` | Wrong port, or a serial monitor still holds the port open. Close it and retry. |
| `RADIO_STATUS` blinks slowly and never goes solid | `radio.begin()` or `isChipConnected()` failed — check the nRF24 wiring and its power supply |
| Upload succeeds, serial prints nothing | Monitor is at the wrong baud rate. The sketches use 38400 by default; the base station's rate is a setting stored in EEPROM, which survives uploads — if it was changed, connect at that rate and send `M` |
| `P` returns all zeros | The transmitter station is not firing — confirm it is powered, flashed, and its radio LED is solid. If it is, check the sampling time (`H`): it must be long enough to catch the echoes but shorter than the trigger period (100 ms at 10 Hz); `H 80` restores the default |
| Garbled serial output | Baud mismatch, or `DEBUG` was left enabled in `debug_macros.h` and is interleaving diagnostics with data |
