# Usage

Only the **base station** has a user interface. It listens on USB serial at
38400 bps and responds to single-letter commands. The transmitter station has no
command interface — once flashed it runs autonomously, reacting to radio packets
and its own timer.

## Connecting

Any 38400 bps serial terminal works:

```bash
pio device monitor -p /dev/ttyACM0 -b 38400
```

```bash
screen /dev/ttyACM0 38400        # exit with Ctrl-A then k
```

Opening the port asserts DTR, which resets the Uno. `setup()` waits up to 3
seconds for the host to attach, so give it a moment before sending the first
command.

In normal operation the port is held by the
[ground-station program](https://github.com/Imperious22M/UltraGPS-Ground)
(`UltraGpsXY`), which issues these same commands on behalf of network clients.
Only one program can hold the port at a time.

## Commands

Send one letter followed by a newline or carriage return. Anything else is
silently discarded.

| Command | Effect | Response |
|---------|--------|----------|
| `P` | Fire one pulse and measure the echoes. Leaves (or returns to) polling mode. | One `N:` line |
| `C` | Enter continuous mode — measure and report repeatedly until interrupted | A stream of `C:` lines |
| `S` | Fire one pulse but report **simulated** counts. Returns to polling mode. | One `N:` line |

!!! note "Line endings matter"
    `CommandControl::poll()` only dispatches a command when it sees `\n` or
    `\r`. In the Arduino IDE's Serial Monitor, set the line-ending dropdown to
    *Newline* or *Carriage return* — with *No line ending* nothing will ever
    happen.

### Leaving continuous mode

There is no stop command. Send `P` or `S` — both set the polling state back to
`POLLING`, which ends the stream. Resetting the board works too.

### Invalid input

Unknown letters, empty lines, and lines longer than 63 characters are dropped
without any response. The base station never echoes input and never reports an
error for a bad command, so silence after a command means it was not recognised.

## Output format

Every measurement is one line: a two-character mode marker, then six
comma-separated counts, one per receiver, in `A0`–`A5` order.

```
N: 4404, 3226, 4063, 4041, 3133, 4109
C: 4398, 3231, 4059, 4044, 3130, 4112
```

| Marker | Meaning |
|--------|---------|
| `N: ` | Normal — a single measurement, from `P` or `S` |
| `C: ` | Continuous — part of the stream started by `C` |

The marker exists so a consuming program can tell solicited replies from
streamed data on the same port.

### What the numbers mean

Each value is the number of sampling-loop iterations between the trigger and
that receiver's echo going low — a **count**, not microseconds and not
millimetres. Converting to distance takes the form `Ax + B`, where `A` scales
counts to time or distance and `B` absorbs the fact that counting does not start
at exactly zero. Both constants are determined by calibration downstream; the
firmware does no conversion.

A value of `0` means that receiver never produced a usable echo transition
within the sampling window — out of range, not wired, or the pulse never fired.

## Example session

```
$ pio device monitor -p /dev/ttyACM0 -b 38400
P
N: 4404, 3226, 4063, 4041, 3133, 4109
P
N: 4401, 3229, 4066, 4038, 3135, 4106
C
C: 4400, 3227, 4064, 4040, 3134, 4108
C: 4399, 3230, 4062, 4043, 3131, 4110
C: 4402, 3228, 4065, 4039, 3136, 4107
P
N: 4403, 3227, 4063, 4041, 3133, 4109
```

## Rates

In continuous mode `loop()` checks `continuousPollingDelay` (20 ms) before
starting the next measurement, but each measurement blocks in
`handleUltrasonics()` waiting for the next Timer1 compare match. The trigger
rate is therefore the real limit: at the default 10 Hz you get about ten lines
per second regardless of the 20 ms constant. Raising the output rate means
changing the timer rate on **both** boards — see
[Configuration](configuration.md).

## Startup behaviour

On reset the base station:

1. Opens the serial port at 38400 and waits up to 3 s for the host.
2. Sets `A0`–`A5` to `INPUT_PULLUP`.
3. Initialises the LEDs (animation off by default).
4. Starts Timer1 at 10 Hz.
5. Initialises the radio. **If this fails the sketch halts**, blinking
   `RADIO_STATUS` on a two-second cycle (1 s on, 1 s off) and printing
   `[ERROR] Still halted.` on each cycle. Nothing recovers this except fixing
   the wiring and resetting the board.
6. Leaves `RADIO_STATUS` solid and waits for commands.

There is no banner or ready message on a successful start — the first output you
see is the response to your first command.
