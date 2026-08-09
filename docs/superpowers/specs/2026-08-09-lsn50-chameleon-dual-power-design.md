# LSN50V2 Chameleon dual-power firmware design

## Status

Approved for implementation on 2026-08-09. This design replaces the initial
implementation on `feature/chameleon-v1.6-switched-i2c2`; that implementation
must not be used as a reference for HAL lifecycle or power sequencing.

## Goal

Build two EU868 LSN50V2 firmware images for the current VIA Chameleon reader.
Both images use PB13/PB14 I2C2, one protocol implementation, the existing
44-byte payload, and the same bounded acquisition state machine. They differ
only in how the reader rail is switched:

- VCC variant: LSN50 VCC feeds the Chameleon through an external P-channel
  MOSFET controlled by PB12.
- 5 V variant: the LSN50 switched +5 V output feeds an external inline 3.3 V
  regulator. PB5 controls the LSN50 rail.

The output artifacts are bench-test firmware. Compilation does not make either
electrical design field-ready.

## Sources and verified constraints

The protocol source of truth is the vendor package at
`/home/phil/kDrive/OSI OS/Hardware/Chameleon/VIAChameleonI2CMaster/`:

- `VIAChameleonI2C.h`
- `VIAChameleonI2C.cpp`
- `VIAChameleonI2CMaster.ino`
- `Example output.txt`

The hardware references are Dragino's
[LSN50 manual v1.7.4](https://dragino.com/downloads/downloads/LSN50-LoRaST/LSN50_LoRa_Sensor_Node_UserManual_v1.7.4.pdf)
and ST's
[STM32L072 datasheet](https://www.st.com/resource/en/datasheet/stm32l072cz.pdf).
They establish the following constraints:

- Chameleon address is 7-bit `0x08`; nominal bus speed is 400 kHz.
- PB13 is I2C2 SCL and PB14 is I2C2 SDA using AF5.
- PB5 LOW enables the LSN50 +5 V output; PB5 HIGH disables it.
- PB14 is also the stock digital-interrupt input and has a board-level R14/C1
  network. Chameleon firmware must own PB14 while built with `USE_CHAMELEON`.
- PB6/PB7 have fixed pull-ups to the always-powered LSN50 rail and cannot be
  used with a power-cycled reader.
- The reader performs a measurement at power-up, but the master still sends
  `0x40` for each reporting cycle and polls `0x41` every 50 ms for up to 2 s.
- Register reads use command-byte write followed by repeated-start read.
- Equal raw and compensated resistance is valid vendor output. It does not
  indicate pending compensation and must not cause a retry or invalid sample.

Connector terminal numbers are excluded because the initial branch document
conflicts with the manual and the exact deployed PCB revision has not been
physically confirmed.

## Initial adversarial review

The required pre-implementation Sol/xhigh review found these blockers:

1. `build/build.sh` uses absolute paths into a separate v1.5 checkout, so the
   v1.6 branch does not build its own source.
2. The committed v1.5 and v1.6 BIN/HEX files are byte-identical.
3. The v1.6 acquisition still initializes I2C1 on PB6/PB7 and uses its ready
   flag before repointing the shared handle to I2C2.
4. The code sets the HAL handle state to `READY` to avoid MSP initialization
   and disables I2C registers directly instead of using a coherent HAL
   initialize/deinitialize lifecycle.
5. Stock PB14 initialization, interrupt, downlink, and payload paths can still
   touch SDA.
6. The existing polling loop combines 50 ms delays with one-second HAL
   transaction timeouts, so its nominal two-second limit is not a wall-clock
   bound.
7. The host tests preserve unsupported settle, equality-retry, and
   `COMP_PENDING` behavior.

The review also identified electrical gates that firmware cannot prove:
actual R14/C1 population, 400 kHz rise time, P-MOSF suitability at minimum
VCC, regulator reverse leakage, switched-rail discharge, and STOP-mode current.

HX711 is outside this work. It belongs to another sensor mode and remains
unchanged. The Chameleon VCC build only guarantees that its own mode owns PB12
during acquisition and leaves the P-MOSF off afterward.

## Software architecture

### Protocol module

`via_chameleon.c` contains only the vendor protocol:

- probe address `0x08`;
- write trigger `0x40`;
- poll status `0x41` at 50 ms intervals against an absolute deadline;
- repeated-start reads of temperature, six resistances, and the eight-byte ID;
- little-endian conversion;
- sentinel validation and per-field validity tracking.

It contains no STM32 registers, GPIO selection, power control, or HAL handle
manipulation. The unsupported 250 ms post-ready delay, equality-triggered
retries, and `COMP_PENDING` inference are removed.

### LSN50 hardware and acquisition module

`chameleon_lsn50_hw.c` owns:

- the dedicated static I2C2 handle;
- PB13/PB14 AF5 initialization and analog/no-pull shutdown state;
- startup ACK polling with short transaction timeouts;
- the selected power backend;
- one complete cold-power retry for recoverable communication failures;
- cleanup on every exit path.

The acquisition invariant is:

```text
rail off + bus high impedance
  -> rail on
  -> 25 ms stabilization delay
  -> I2C2 initialize
  -> address probe every 50 ms to a 1500 ms deadline
  -> trigger and bounded ready polling
  -> register reads and validation
  -> HAL I2C2 deinitialize
  -> PB13/PB14 analog, no pull
  -> rail off
```

Retry repeats the entire sequence once. Open-channel, missing-DS18B20, and
invalid-ID sentinels do not trigger a power-cycle retry.

### HAL integration

`HAL_I2C_MspInit` and `HAL_I2C_MspDeInit` branch on the handle instance.
I2C1 retains its stock PB6/PB7 behavior. I2C2 configures PB13/PB14 AF5,
open-drain, with no MCU pull-ups. The firmware never writes HAL's internal
state field.

Chameleon builds compile out stock PB14 EXTI initialization and ignore
requests that would re-arm EXTI14. The MOD3 Chameleon payload path does not
read PB14 as a digital input. Other firmware modes keep their existing PB14
behavior.

### Power backends

Exactly one backend must be selected at compile time.

`CHAMELEON_POWER_EXTERNAL_PMOS`:

- initialize PB12 open-drain and released so the external gate-source pull-up
  defines OFF;
- drive PB12 LOW to turn the P-MOSFET on;
- release PB12 only after I2C2 is deinitialized and the bus is analog;
- leave unrelated stock modes unchanged.

`CHAMELEON_POWER_LSN50_5V`:

- hold PB5 HIGH initially;
- drive PB5 LOW to enable +5 V before initializing I2C2;
- drive PB5 HIGH after I2C2 shutdown and bus isolation;
- bypass generic `power_time`/`AT+5VT` timing only for Chameleon acquisition,
  so the backend has deterministic ownership of the rail.

## Protocol results and payload compatibility

The internal result type distinguishes:

- startup probe NACK/no device;
- trigger failure;
- status transport failure;
- measurement busy timeout;
- complete register-read failure;
- partial sample;
- valid sample with temperature, ID, or open-channel sentinel flags.

The LoRaWAN payload remains version 1 and 44 bytes. Raw and compensated
resistances stay at their established offsets. Bit 7 remains reserved and
zero; it is not repurposed from `COMP_PENDING`. Existing bit 0/bit 1 mappings
are used conservatively to make the current OSI decoder reject incomplete
samples. Exact transport causes remain available in the internal result and
serial diagnostics. A richer on-wire error model requires a future payload
version and synchronized decoder change.

EU868 DR0-DR2 allow 51 bytes in the bundled LoRaMAC region table, so the
44-byte frame fits. The same frame does not fit US915 DR0 and may require a
higher rate in dwell-time regions. These two artifacts are explicitly EU868.

## Build outputs

The build must be repository-relative and must not overwrite the v1.5 artifact
names. It produces separate object directories and these outputs:

- `build/LSN50-chameleon-i2c2-vcc-pmos.bin`
- `build/LSN50-chameleon-i2c2-vcc-pmos.hex`
- `build/LSN50-chameleon-i2c2-vcc-pmos.elf`
- `build/LSN50-chameleon-i2c2-vcc-pmos.map`
- `build/LSN50-chameleon-i2c2-5v-reg.bin`
- `build/LSN50-chameleon-i2c2-5v-reg.hex`
- `build/LSN50-chameleon-i2c2-5v-reg.elf`
- `build/LSN50-chameleon-i2c2-5v-reg.map`

## Test design

Protocol tests cover vendor commands, repeated-start board calls, 50 ms
polling, absolute deadlines, little-endian values, short reads, transport
failures, sentinels, and the vendor equal raw/compensated examples.

Lifecycle trace tests inject each failure at the hardware boundary and assert
the order of power, I2C, bus isolation, and cleanup. They run once for each
backend and prove that retry occurs at most once after a complete power-off.

Build verification compiles both variants from the same commit, checks their
names and sizes, confirms they differ, and runs the OSI v1 decoder golden
fixture. Physical measurements remain required for rail decay, back-power,
rise time, current, low-battery behavior, and soak testing.

## Bench gates

Before connecting the reader, verify OFF and ON rail polarity for the selected
backend. After connection, measure reader VCC, SDA, SCL, active current, and
complete LSN50 sleep current. Scope I2C rise time at 400 kHz with the deployed
cable; use a documented 100 kHz build only if Fast-mode timing is not met.

Run 100 rapid acquisition cycles, then a 12-24 hour test at the normal report
interval. Repeat with an unplugged reader, each open channel, removed DS18B20,
an induced bus fault, and reduced battery voltage. Variant B is tested first
because its regulated reader rail avoids the low-VCC uncertainty in Variant A.
