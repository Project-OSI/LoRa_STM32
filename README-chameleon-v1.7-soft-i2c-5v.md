# LSN50v2.3a Chameleon switched-5 V software-I2C bench guide

## Image identity

This guide covers the single v1.7 bench image for a Dragino LSN50v2 rev 2.3a
and a VIA Chameleon I2C reader. It powers the reader from the LSN50's stock,
PB5-switched +5 V output and communicates over a software I2C bus. It is a
bench candidate, not a field-ready release.

| Item | Value |
|---|---|
| Branch | `feature/chameleon-v1.7-switched-5v-soft-i2c` |
| Build target | `chameleon-soft-i2c-5v` |
| Binary | `build/LSN50-chameleon-soft-i2c-5v.bin` |
| Intel HEX | `build/LSN50-chameleon-soft-i2c-5v.hex` |
| Power control | PB5, stock active-low +5 V enable |
| I2C transport | software I2C, nominally 50 kHz, no more than 100 kHz |
| Bus pins | PB13 SCL and PB12 SDA |
| Boot identity | `Chameleon acquisition enabled [soft-i2c-5v]` |

The image is dedicated to MOD3. Do not use it for a different LSN50 sensor
mode. PB12 and PB13 are a GPIO software-I2C pair; they are not a hardware I2C
peripheral assignment.

## Wiring by terminal and signal

Use the LSN50v2 pin drawing for the terminal numbers. The older manual has
conflicting terminal and SDA/SCL labels.

| LSN50 terminal | MCU or rail | Chameleon reader connection |
|---|---|---|
| 14 | stock `+5V` output | VCC |
| 15, or another verified GND terminal | GND | GND |
| 20 | PB12 | SDA |
| 21 | PB13 | SCL |

Connect only this harness during LSN50 operation. Disconnect the USB-I2C
adapter, PB6, PB7, and PB14 from the reader. The reader and LSN50 need a common
ground; another LSN50 terminal is acceptable only after continuity verifies it
is GND.

## Electrical states and no-pulldown rule

PB5 drives low to enable the stock +5 V rail and is released to switch it off.
While measuring, PB12 and PB13 are open-drain outputs with no MCU pull
resistors. The firmware drives only low or releases a line; it never drives a
bus line high. During cleanup and sleep, it returns both pins to analog mode
with no pull, then disables the +5 V rail.

Do not fit internal or external pull-downs on PB12 or PB13. The reader's
approximately 4.7 kOhm pull-ups follow its switched rail, so a pull-down would
draw current while the reader is active and reduce the logic-high voltage.

Do not remove the LSN50 battery while the +5 V output is charged. That sequence
is unsupported because the MCU VDD rail can collapse before the reader rail.
For a permanent outdoor cable, decide and document enclosure-level ESD/TVS
protection before deployment. It is not a bench-build gate.

## Acquisition and failure behavior

Each session starts with the reader rail off and PB12/PB13 isolated. The
firmware enables +5 V, waits 100 ms, configures the open-drain bus, and probes
address `0x08` for up to 400 ms. It retains the VIA protocol: trigger `0x40`,
status `0x41`, 50 ms readiness polls, repeated-start register reads, and the
existing V1 payload layout.

The transport has a 50 ms transaction limit. The full acquisition, including a
single cold retry, has a 12 s wall-clock cap. On a transport fault it attempts
a bounded bus clear, isolates PB12/PB13, switches off +5 V, waits 1000 ms, and
can retry once only if sufficient time remains. A final failure carries the
existing Chameleon fault flags and still permits the normal LoRa uplink.

The result line records the acquisition result and attempts count. The boot
output also records one classified reset cause, for example `Chameleon
reset:iwdg flags:0x...`; it should appear once per reset, not once per reading.

## Build and artifact verification

From the repository root, build the image in its private object directory:

```bash
build/build.sh clean
build/build.sh chameleon-soft-i2c-5v
```

Verify the target identity before flashing:

```bash
arm-none-eabi-size build/LSN50-chameleon-soft-i2c-5v.elf
strings build/LSN50-chameleon-soft-i2c-5v.elf | \
  rg 'Chameleon acquisition enabled \[soft-i2c-5v\]'
sha256sum build/LSN50-chameleon-soft-i2c-5v.bin \
  build/LSN50-chameleon-soft-i2c-5v.hex
```

Flash either the BIN or the HEX with the normal LSN50 procedure. Keep the
recorded hash with the bench log so UART and uplink observations identify the
exact image.

## Flash and UART smoke test

Flash the new `LSN50-chameleon-soft-i2c-5v.bin` or
`LSN50-chameleon-soft-i2c-5v.hex`, attach the reader using the wiring table,
and power-cycle the LSN50. Confirm the boot output contains:

```text
Chameleon acquisition enabled [soft-i2c-5v]
```

Then use the vendor-supported query and one manual acquisition:

```text
AT+MOD=?
AT+GETSENSORVALUE=0
```

`AT+MOD=?` must report mode 3. `AT+GETSENSORVALUE=0` must complete without a
reset and print a Chameleon result. Confirm one normal uplink retains
`Work_mode=3ADC+IIC` and the V1 Chameleon fields. A successful smoke test only
authorizes the following bench tests.

## Off-state and enable-transient measurements

Measure reader VCC, SDA, and SCL at rail-off and after 0.1, 0.5, 1, 5, 30, 60,
and 300 seconds, or until all three nodes stay below 0.1 V. Repeat after a
successful acquisition and after each injected failure. Then connect a
temporary current-limited load to bring +5 V below 0.1 V. Remove the load with
PB5 off and PB12/PB13 analog, then watch for voltage rebound and sourced
current for another 5 minutes.

Capture +5 V, SDA, and SCL at enable with a single-shot scope. Use at least
1 MS/s, or 10 MS/s or faster when available. Record VDD minimum, +5 V
overshoot, loaded rail voltage, and settling time with a cold or passivated
cell as well as with a bench supply. Archive a verified RT9266 datasheet before
using converter-specific limits or expected waveforms in the verdict.

| Observation | Required action |
|---|---|
| VCC/SDA/SCL cross below 0.1 V soon enough that 150% of the time is at most 5 s | Set the retry delay to that value, with a 1 s minimum. |
| Natural decay takes more than 5 s but reaches below 0.1 V before the shortest deployed interval | Disable same-cycle retry; retry at the next scheduled sample. |
| Natural decay does not reach below 0.1 V before the shortest deployed interval | Do not field-release the passive-discharge build; specify and validate active discharge or add hardware. |
| After temporary discharge below 0.1 V, a node rebounds to at least 0.3 V or sourced current exceeds 5 uA | Stop and investigate leakage/back-power before adding hardware. |
| A residual node remains above 0.3 V before forced discharge but continues falling | Treat it as stored charge; continue the natural-decay observation to at least 5 minutes. |
| MCU VDD stays at least 2.0 V, the rail settles, and no reset or corruption occurs | Keep the stock +5 V design; use the measured startup delay with margin. |
| VDD falls below 2.0 V, the rail does not settle, the MCU resets, or data corrupts | Reject direct +5 V for this hardware and cell condition. |

The active bus rate must be no more than 100 kHz and the 30–70% rise time at
the reader end no more than 1 us with the longest intended cable. VCC, SDA,
and SCL must remain at or below 5.5 V. Reader VCC must stay at or above 3.0 V
throughout a measurement. MCU VDD must stay at or above 2.0 V during the enable
transient, including with a cold or passivated cell. A loaded +5 V rail below
4.5 V requires diagnosis, but does not automatically fail when it remains at
least 3.0 V and every protocol gate passes.

## Protocol endurance and fault injection

Run at least 500 one-minute acquisition sessions with the intended reader,
array, cable, and battery type. Pass requires zero resets, no acquisition over
12 s, and at least 99% clean samples. Cause one controlled pin reset and one
controlled watchdog reset; after each, verify the reset-cause boot line.

Run 20 cycles each of the following cases: SDA open, SCL grounded, SDA
grounded, reader VCC disconnected, reader absent, reader hot-plugged, maximum
intended cable, and forced pre-sleep cleanup during an active session. Each
cycle must uplink, recover within one scheduled sample after the fault is
removed, and leave the rail off during sleep.

## Power-consumption logging

Log current for at least 24 hours against the same unit's firmware baseline.
The added average-current target is at most 250 uA at a 5-minute interval or
60 uA at a 20-minute interval. The steady off-state increment after stored
charge dissipates must be at most 5 uA.

Any average-current target exceedance requires a written battery-life
calculation before field testing. More than five times the applicable average
target, or more than 5 uA off-state increment, rejects the build.

## Four-week field gate

Run two units continuously for at least four weeks after every preceding gate
has recorded evidence. Pass requires `i2c_missing` below 0.5%, no fault block
longer than 30 minutes, no unexplained frame-counter reset, no sustained battery
decline, stable array IDs, and plausible Chameleon values against a known-good
range or a co-located reference.

## Known rejected wiring

Do not use these arrangements for this image:

- PB13/PB14 I2C2. PB14 carries the C1 network and cannot provide an I2C SDA
  edge on the target board.
- Direct switched-5 V on PB6/PB7. Their fixed pull-ups back-power an off reader.
- Continuous VDD on PB6/PB7. The reader's roughly 8.5 mA continuous draw and
  lack of a cold recovery path make it unsuitable for battery operation.
- A USB-I2C adapter left connected to the production harness.
- Pull-downs on PB12 or PB13.
- Treating PB12/PB13 as hardware I2C. This image deliberately uses software
  open-drain I2C on those GPIOs.
