# Conversation: Creating Arduino Upload Script

**Date:** 2026-02-02

## Task
Created a Python script called `upload` that uploads the `base-station.ino` file to an Arduino using `arduino-cli` with command line argument support for selecting which Arduino to use.

## Script Features

- **List connected boards:** `./upload --list`
- **Auto-upload** (single board): `./upload`
- **Select by port:** `./upload -p /dev/ttyUSB0`
- **Select by index:** `./upload -i 0`
- **Specify board type:** `./upload -p /dev/ttyUSB0 -b arduino:avr:uno`
- **Custom sketch:** `./upload /path/to/other.ino`

## Configuration

- **Default board type:** `arduino:avr:uno`
- **Default sketch:** `base-station.ino` in the script's directory

## Files Created

- `upload` - Python script for uploading sketches to Arduino

## Usage

```bash
./upload --help
./upload --list          # List connected boards
./upload                 # Auto-detect and upload
./upload -i 0            # Upload to board at index 0
./upload -p /dev/ttyUSB0 # Upload to specific port
```
