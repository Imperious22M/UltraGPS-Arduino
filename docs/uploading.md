# Uploading

Flashing a sketch means picking the right project **and** the right serial port.
The two boards are identical Arduino Unos, so nothing stops you from putting the
base-station firmware on the transmitter board — identify the port first.

## Identify the boards

List everything currently attached:

```bash
arduino-cli board list
```

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
**`/dev/ttyACM0` is not permanently the base station**. If you are unsure which
is which, unplug one and re-run the command. The repository's defaults assume:

| Project | Default port |
|---------|--------------|
| `base-station` | `/dev/ttyACM0` |
| `transmitter-station` | `/dev/ttyACM1` |

Those defaults live in each project's `platformio.ini` — change them there if
your boards enumerate differently (see [Configuration](configuration.md)).

!!! note "Pinning the port names"
    To stop the ports swapping between reboots, add a `udev` rule keyed on each
    Uno's serial number so it always appears under a stable symlink such as
    `/dev/ultragps-base`. Get the serial with
    `udevadm info -a -n /dev/ttyACM0 | grep serial`.

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

`arduino-cli` can build and flash the PlatformIO source tree directly, because
the sketches are plain C++ under `src/`:

```bash
arduino-cli compile --upload -p /dev/ttyACM0 -b arduino:avr:uno base-station/src
```

To flash a prebuilt `.hex` from a release without compiling anything:

```bash
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno \
    --input-file ultragps-base-station-v1.0.hex
```

!!! note "The `base-station/upload` helper script is out of date"
    The repository ships a Python helper at `base-station/upload` that wraps
    `arduino-cli` with board selection (`--list`, `-p`, `-i`). It defaults to a
    sketch named `base-station.ino`, which no longer exists — the project moved
    to PlatformIO and the code now lives in `base-station/src/`. Pass the source
    directory explicitly if you want to use it:

    ```bash
    ./base-station/upload --list
    ./base-station/upload -p /dev/ttyACM0 base-station/src
    ```

    See [Notes](notes.md).

## Verify the upload

Open a serial monitor at **38400 bps** and send a `P`:

```bash
pio device monitor -p /dev/ttyACM0 -b 38400
```

A healthy base station answers with a line of six comma-separated counts:

```
N: 4404, 3226, 4063, 4041, 3133, 4109
```

On the transmitter station there is nothing to type — it has no command
interface. Check the LEDs instead: `RADIO_STATUS` (pin 8) goes solid once the
radio initialises, and `PULSE_TRIG` (pin 7) lights when a trigger fires.

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| `Permission denied: /dev/ttyACM0` | Your user is not in the `dialout` group — see [Installation](installation.md) |
| `avrdude: stk500_recv(): programmer is not responding` | Wrong port, or a serial monitor still holds the port open. Close it and retry. |
| `RADIO_STATUS` blinks slowly and never goes solid | `radio.begin()` or `isChipConnected()` failed — check the nRF24 wiring and its power supply |
| Upload succeeds, serial prints nothing | Monitor is at the wrong baud rate; the sketches use 38400 |
| `P` returns all zeros | The transmitter station is not firing — confirm it is powered, flashed, and its radio LED is solid |
| Garbled serial output | Baud mismatch, or `DEBUG` was left enabled in `debug_macros.h` and is interleaving diagnostics with data |
