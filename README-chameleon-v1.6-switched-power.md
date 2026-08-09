# LSN50V2 + VIA Chameleon: two power variants

This branch produces two EU868 bench-test firmware images for the current VIA
Chameleon I2C reader. Both use PB13/PB14 as I2C2. Select the image that matches
the physical power circuit; the images are not interchangeable.

These are dedicated Chameleon MOD3 images. PB14 interrupt and digital-input
handling is disabled throughout each image so firmware cannot contend with
I2C2 SDA. Do not use either image for another LSN50 sensor mode.

Connector terminal numbers are deliberately omitted. Dragino documentation and
the earlier branch notes disagree, and the deployed LSN50V2 PCB revision has not
been physically confirmed. Identify pins by signal name and verify them with the
schematic or continuity measurements before wiring.

## Shared I2C wiring

| LSN50V2 signal | Chameleon connection | Firmware configuration |
|---|---|---|
| PB13 | SCL | I2C2 AF5, open-drain |
| PB14 | SDA | I2C2 AF5, open-drain |
| GND | GND | Common reference |

Do not use PB6/PB7. Their fixed board pull-ups are tied to the always-powered
LSN50 rail and can back-power a switched-off reader.

The MCU enables no internal I2C pull-ups. SDA and SCL require external pull-ups
to the reader's switched 3.3 V rail. Never pull either line to +5 V. Confirm
whether the Chameleon board already provides pull-ups before adding another
pair.

PB14 also has the LSN50 digital-input R14/C1 network. The firmware masks and
compiles out EXTI14 while using PB14 as SDA, but it cannot remove that passive
network. Dragino's LSN50-V2 manual, section 2.4.5, specifies a 0.1 uF C1 for the
PB14 interrupt retrofit on older boards. If that capacitor is fitted, a 4.7
kOhm SDA pull-up gives a 470 us time constant and about 398 us from 30% to 70%;
that is unusable at both 400 kHz and 100 kHz.

Inspect or measure C1 on the exact target board before connecting the reader.
If 0.1 uF is fitted, remove or isolate it before using PB14 as SDA. After C1 is
confirmed absent or sufficiently small, scope SDA and SCL on the assembled unit
and verify the 400 kHz Fast-mode rise-time limit. A future 100 kHz build could
accommodate modest residual capacitance, but it is not a workaround for 0.1 uF.

## Variant A: VCC through an external P-channel MOSFET

Use these files:

- `build/LSN50-chameleon-i2c2-vcc-pmos.bin`
- `build/LSN50-chameleon-i2c2-vcc-pmos.hex`

Wire the LSN50 VCC rail to the P-channel MOSFET source. Connect the MOSFET drain
to Chameleon VCC and to both I2C pull-ups. Connect PB12 to the gate and fit an
external gate-to-source pull-up. The firmware configures PB12 open-drain: LOW is
ON; released/high-impedance is OFF.

Select a MOSFET whose gate threshold and on-resistance are suitable across the
full battery/VCC range. Verify that reader and attached DS18B20 voltage stays
within their specified operating range at the lowest intended battery voltage.
The common DS18B20 minimum is 3.0 V, so this variant has a low-battery gate that
the firmware build cannot settle.

## Variant B: switched +5 V through an external 3.3 V regulator

Use these files:

- `build/LSN50-chameleon-i2c2-5v-reg.bin`
- `build/LSN50-chameleon-i2c2-5v-reg.hex`

Connect the LSN50 switched +5 V output to the input of an external inline 3.3 V
regulator. Connect the regulator output to Chameleon VCC and both I2C pull-ups.
The firmware preserves Dragino's stock PB5 open-drain control with its MCU
pull-up: LOW enables the LSN50 +5 V output and released/high disables it. The
reader must not be connected directly to +5 V.

Check regulator dropout, quiescent current, reverse leakage, startup time, and
output discharge. An output that remains charged after PB5 goes HIGH can keep
the reader partially powered between reports.

Variant B is the first bench candidate because it supplies a regulated reader
rail and avoids Variant A's low-VCC uncertainty.

## Acquisition behavior

Each report starts with the selected rail OFF and PB13/PB14 in analog/no-pull
state. Firmware turns the rail on, waits 25 ms, initializes a private I2C2 HAL
handle, and probes address `0x08` every 50 ms for at most 1500 ms. It
then sends trigger command `0x40`, polls status command `0x41` every 50 ms for an
absolute maximum of 2 s, and reads temperature, all raw and compensated
resistances, and the eight-byte array ID with repeated-start transactions.

Cleanup always deinitializes I2C2, returns PB13/PB14 to analog/no-pull, and turns
the rail off. A recoverable communication failure receives at most one complete
cold-power retry after 200 ms OFF. Open-channel, missing-temperature, and
invalid-ID sentinel values are valid protocol responses and do not cause a
retry. Equal raw and compensated resistance is also valid and is preserved.

The LoRaWAN payload stays at version 1 and 44 bytes. It retains both raw and
compensated readings. Status bit 7 remains reserved and is forced to zero.

## Build commands

From the repository root:

```bash
build/build.sh clean
build/build.sh chameleon-i2c2-vcc-pmos
build/build.sh chameleon-i2c2-5v-reg
```

Each target has a separate object directory. The script is repository-relative
and does not overwrite the older `LSN50-chameleon` artifacts.

## Bench gates before field use

Do not connect the Chameleon until OFF/ON polarity has been confirmed with a
meter on the selected power circuit.

1. Inspect or measure the target board's PB14 C1. If 0.1 uF is fitted, remove
   or isolate it before attaching SDA.
2. Flash Variant B first. With the reader disconnected, confirm PB5
   released/high/OFF, PB5 driven LOW/ON, regulated output voltage, and output
   decay after shutdown.
3. Connect the reader. Measure reader VCC, SDA, and SCL while active and after
   cleanup. Any persistent intermediate voltage while OFF indicates leakage or
   back-powering.
4. Scope SDA/SCL rise time and logic levels at 400 kHz with the deployed cable.
   Do not accept a marginal waveform merely because short bench reads succeed.
5. Measure active current and complete LSN50 sleep current. Compare sleep
   current with the reader physically disconnected.
6. Run 100 rapid acquisition cycles. Confirm one trigger per successful report,
   no unexpected cold retries, stable array ID, and preserved raw/compensated
   values.
7. Repeat with the reader unplugged, each resistance channel open, DS18B20
   absent, SDA or SCL faulted, and battery voltage reduced to the intended
   minimum. Confirm bounded completion and rail shutdown in every case.
8. Run a 12-24 hour test at the normal reporting interval while logging serial
   diagnostics, payloads, active current, and sleep current.
9. Repeat the same sequence for Variant A, adding MOSFET gate/source/drain and
   minimum-VCC measurements.

These measurements are release gates. Successful compilation and host tests do
not establish electrical safety, STOP-mode current, reliable 400 kHz timing, or
field readiness.
