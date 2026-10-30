# UltraGPS Arduino Firmware

Arduino firmware for the St. Mary's University **UltraGPS** positioning system,
developed as part of graduate research.

The repository holds the two Arduino sketches that run on the Arduino Unos that
are the physical backbone of the system.
The **base station** accepts single-letter commands over USB serial, tells the
transmitter over a 2.4 GHz radio link when to fire, times the ultrasonic echoes
returned by six receivers, and prints the resulting tick counts back over serial.
The **transmitter station** listens for those radio packets, keeps its timer
phase-locked to the base station, and drives the ultrasonic transmitter.

The base station is the board that the
[UltraGPS Ground Station](https://github.com/Imperious22M/UltraGPS-Ground)
program (`UltraGpsXY`) talks to over USB serial.

## Documentation

- **[Architecture](architecture.md)** — how the two sketches are structured and
  how a measurement flows through the system.
- **[Hardware](hardware.md)** — boards, wiring, and the full pin map for both
  stations.
- **[Installation](installation.md)** — installing the toolchain (PlatformIO or
  `arduino-cli`) and getting the sources.
- **[Uploading](uploading.md)** — flashing each sketch onto the right board.
- **[Configuration](configuration.md)** — every setting and where it lives,
  including the base-station settings stored in EEPROM.
- **[Usage](usage.md)** — the serial command set and the output format.
- **[Developer's Guide](developer-guide.md)** — a self-contained reference for
  anyone forking the repository

## At a glance

| | |
|---|---|
| Sketches | `base-station/`, `transmitter-station/` (+ `shared/` libraries used by both) |
| Board | Arduino Uno (ATmega328P, 16 MHz) |
| Framework | Arduino, built with PlatformIO (`platform = atmelavr`) |
| Dependency | [`nRF24/RF24`](https://github.com/nRF24/RF24) (pulled in automatically) |
| Serial | 38400 bps, 8N1 |
| Radio | nRF24L01+, channel 125, 1 Mbps, 8-byte fixed payload |
| Pulse rate | 10 Hz by default, 1–50 Hz with `F` (Timer1, CTC mode) |
| Receivers | 6, on `A0`–`A5` |

## Quick start

Every release ships prebuilt `.hex` images and a zip of the sketch sources on
the **[Releases page](https://github.com/Imperious22M/UltraGPS-Arduino/releases/latest)**.
You do not need a toolchain to flash a release — `arduino-cli` alone will do it:

```bash
arduino-cli upload -p /dev/ttyACM0 -b arduino:avr:uno \
    --input-file ultragps-base-station-v1.0.hex
```

To build from source instead, see [Installation](installation.md) and
[Uploading](uploading.md).
