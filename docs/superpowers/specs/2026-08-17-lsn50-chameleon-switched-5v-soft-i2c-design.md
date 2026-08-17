# LSN50 Chameleon switched-5 V software-I2C design

**Date:** 2026-08-17  
**Review revision:** 2026-08-18
**Status:** Approved architecture; implementation has not started  
**Branch:** `feature/chameleon-v1.7-switched-5v-soft-i2c`  
**Hardware:** Dragino LSN50v2 rev 2.3a and VIA Chameleon I2C reader

## Goal

Build one Chameleon firmware image that uses the LSN50's stock PB5-controlled
+5 V output to power the reader directly and uses software I2C on PB13/PB12.
The design adds no regulator, level shifter, bus switch, or discrete pull-down.
It must retain the VIA register protocol, the existing Chameleon payload, and
the vendor firmware outside the pins and lifecycle owned by this integration.

The image is a separate successor to the completed PB13/PB14 I2C2 firmware.
The earlier image and its branch remain unchanged.

## Evidence behind the choice

The original switched-5 V build failed because PB6/PB7 have fixed 10 kOhm
pull-ups to always-on VDD. Those resistors held the nominally unpowered reader
near 1.45 V through SDA/SCL. The reader therefore entered an undefined
brownout state instead of receiving a clean reset.

PB12 and PB13 remove that path. The LSN50 v2.3 schematic shows no fixed rail
pull-up, pull-down, or capacitor on either exposed net beyond board-level ESD
protection. STM32L072 datasheet DS10689 identifies PB12 as FT and PB13 as FTf;
both accept a 5.5 V input while MCU VDD is at least 2.0 V when the internal
pull resistors are disabled. The reader's measured 4.7 kOhm pull-ups may
therefore raise the active bus to its 5 V supply. At 5 V, a driven-low line
sinks about 1.1 mA.

Dragino documents the +5 V output as a duty-cycled external-sensor supply:
PB5 enables it before sampling and disables it afterward. The previous field
installation also proved that this supply could operate the Chameleon reader
for days. Its recorded failure matches bus back-powering; it did not establish
a boost-converter or load-start failure.

The v2.3 schematic fits roughly 110 uF on the +5 V output (C4 and C11, plus
local converter decoupling) and uses a 4 MOhm feedback-divider path to ground.
Stored charge can therefore
leave a harmless voltage tail after PB5 switches the boost off. Off-state
voltage alone does not prove back-powering; a rebound after the rail has first
been discharged, or continuing sourced current, does.

These facts justify a controlled prototype. They do not waive the discharge,
supply-sag, endurance, or field gates in this specification.

## Wiring contract

| LSN50 v2 terminal | MCU signal | Chameleon connection | Electrical state |
|---|---|---|---|
| 14 | stock `+5V` output | reader VCC | PB5-switched; no inline regulator |
| 15 or another verified GND | GND | reader GND | common reference |
| 20 | PB12 | SDA | software open-drain, no MCU pull |
| 21 | PB13 | SCL | software open-drain, no MCU pull |

No USB-I2C adapter may remain connected during LSN50 operation. PB6, PB7, and
PB14 do not connect to the reader. The harness must use the v2 pin drawing and
the schematic signal names; the older manual contains conflicting terminal and
SDA/SCL labels.

The initial prototype adds no discharge component. If the measured +5 V,
SDA, or SCL off-state fails the gate below, stop. Do not compensate with
unbounded firmware delay. A discharge circuit or the switched-raw-VDD design
then requires a separate hardware decision.

## Pull-resistor decision

PB12 and PB13 use no external or internal pull-down. A pull-down would draw
current whenever the reader's 4.7 kOhm pull-up drives the bus high and would
reduce the high-level voltage. It is not required to discharge the lines:
when PB5 turns the reader off, its own pull-ups connect SDA/SCL to the same
falling rail.

The STM32 configuration is exact:

- active bus: GPIO open-drain output, output latch set before changing mode,
  no internal pull-up or pull-down;
- inactive bus: analog mode, no internal pull-up or pull-down;
- reset: the STM32 reset state and the LSN50's fitted PB5 gate pull-up keep the
  +5 V output disabled.

The firmware must never drive PB12 or PB13 push-pull high. `DEBUG` builds are
incompatible with this pinout because the vendor `DBG_Init()` drives
PB12-PB15 push-pull low. Compilation must fail when `DEBUG` and
`USE_CHAMELEON` are both defined.

## Source boundaries

The implementation keeps three responsibilities separate.

### VIA protocol

`via_chameleon.c` remains the protocol authority. It retains:

- 7-bit slave address `0x08`;
- zero-data address probe;
- trigger command `0x40`;
- status command `0x41`, where `0x01` means ready;
- 50 ms status polling with a 2000 ms measurement deadline;
- repeated-start register reads;
- temperature, compensated resistance, raw resistance, and array-ID register
  order and little-endian decoding;
- sentinel and status-flag behavior.

The implementation plan must not change this file unless a failing protocol
test proves a mismatch with the VIA reference library. Bus speed belongs to
the board adapter, not the VIA protocol module.

### Software-I2C transport

A focused software-I2C module implements the bus mechanics. Its public
operations cover initialization, shutdown, address probe, byte writes,
write-then-read with repeated start, and bus clear. It receives GPIO and time
operations through a small interface so native host tests exercise the real
state machine without STM32 headers.

The target bus rate is nominally 50 kHz and must not exceed 100 kHz. The
module implements standard open-drain behavior:

- driving low means setting the GPIO output low;
- driving high means releasing the GPIO and reading the physical pin;
- every SCL release waits until SCL reads high, allowing slave clock
  stretching;
- the master sends bytes most-significant bit first;
- the receiver ACKs every read byte except the final byte, which receives a
  NACK;
- register reads use a repeated START, not an intervening STOP;
- every exit attempts a STOP and leaves both lines released.

Timing is bounded by elapsed time, not iteration counts. One SCL-high wait is
limited to 2 ms, one byte is limited to 10 ms, and one board-level transaction
is limited to 50 ms. A timeout or stuck SDA aborts the transaction. Before any
configured same-cycle cold retry, the transport issues up to nine SCL pulses
followed by STOP. Bus clear itself is bounded and may fail without delaying the
scheduled uplink.

The transport meets the Standard-mode condition timing from UM10204. After SCL
has risen, START and repeated START wait at least 4.7 us before SDA falls and
hold SDA low for at least 4.0 us before SCL falls. STOP holds SDA low, waits for
SCL high, waits at least 4.0 us, releases SDA, and leaves at least 4.7 us of bus
free time. Normal data changes occur promptly after SCL falls and are stable
before the next SCL rise; the implementation must not add a 10 us data-valid
delay because Standard-mode tVD;DAT is at most 3.45 us.

TIM2 on STM32L072 is a 16-bit timer. The target binding may expose a monotonic
32-bit microsecond value only by extending the 16-bit counter in software,
resetting the extension at each session, and sampling it more often than the
65.536 ms hardware wrap. The existing 50 ms VIA poll interval is the largest
permitted unobserved interval while TIM2 is active.

### LSN50 lifecycle

`chameleon_lsn50_hw.c` adapts PB5, PB12, and PB13 to the transport and owns
the acquisition session. The lifecycle is:

1. Put PB12/PB13 in analog/no-pull mode.
2. Disable +5 V through the stock PB5 open-drain/released-OFF behavior.
3. Enable +5 V by driving PB5 low.
4. Wait for the configured reader-startup interval. Use 100 ms for the initial
   bench build and set the field value from the measured rail-settling time.
5. Preload the PB12/PB13 output latches high, then configure both pins as
   open-drain/no-pull.
6. Probe for at most 400 ms.
7. Allow the vendor's 2000 ms deadline for the reader's power-up measurement
   to become ready.
8. Run the existing VIA trigger, 2 s readiness wait, and register reads.
9. Attempt bus clear after a transport failure.
10. Return PB12/PB13 to analog/no-pull mode before disabling +5 V.
11. In the bench build, leave power off for 1000 ms after the first failed
    session and repeat once from step 1. Set the field retry delay from the
    measured discharge curve to the greater of 1000 ms or 150% of the time
    required for VCC, SDA, and SCL to fall below 0.1 V. Retain the same-cycle
    retry only when that delay is no more than 5 s. Otherwise, finish the
    uplink with fault flags and retry at the next scheduled sample instead of
    blocking the main loop.
12. Uplink the sample or its fault flags on schedule.

Normal cleanup, error cleanup, boot preparation, and pre-sleep preparation use
one idempotent shutdown function. Bus isolation always precedes rail-off. A
full acquisition, including any configured cold retry, has a 12 s wall-clock
cap. There are no retry storms between scheduled uplinks.

The initial firmware uses passive discharge. If the rail does not fall below
0.1 V before the next scheduled acquisition, that firmware is not eligible for
field use. A later revision may actively discharge the rail by switching PB5
off, driving both open-drain bus pins low for a measured bounded interval, then
returning them to analog mode. That sequence sinks charge through the reader's
pull-ups and does not drive either bus line high, but it changes the cleanup
contract and must receive its own scope, current, timeout, and fault tests
before use.

The acquisition path refreshes the independent watchdog during bounded wait
points at intervals no longer than 5 s. The existing fix that bounds
`GetLSIFrequency()` before watchdog initialization must be ported as a separate
targeted change; an LSI capture failure must not hang before the watchdog
exists.

## Vendor behavior retained and restored

The image remains dedicated to MOD3, as the prior Chameleon images are. This
prevents PB12 from being claimed by the ultrasonic and HX711 modes.

PB14 is no longer an I2C pin. Restore its stock digital-interrupt
initialization, downlink configuration, interrupt handling, and MOD3 payload
status rather than preserving the v1.6 compile guards that suppressed it.
PB6/PB7 retain their stock I2C1 implementation; the Chameleon path does not
initialize or modify I2C1 or I2C2.

PA1 remains the stock ADC input because this design does not use the external
P-FET. The standard MOD3 ADC fields and Chameleon V1 44-byte extension remain
unchanged. No edge decoder or payload-schema change is part of this work.

## Failure semantics

The transport reports distinct OK, NACK, timeout, and bus-fault results to the
existing VIA adapter. The lifecycle maps them to the established Chameleon
result and payload flags:

- missing address or register-read failure: `I2C_MISSING`;
- trigger, status-I/O, or readiness deadline failure: `TIMEOUT`;
- temperature, ID, and open-channel sentinels: existing sensor-specific flags;
- any failure after the configured bounded session or sessions: zero/null
  measurement fields plus the mapped flags, followed by the normal LoRa
  uplink.

No Chameleon failure may reset the MCU, block LoRa transmission, or leave +5 V
enabled across Stop mode.

## Build and artifact isolation

This branch produces one hardware image:

```text
build/LSN50-chameleon-soft-i2c-5v.bin
build/LSN50-chameleon-soft-i2c-5v.hex
```

The build target is `chameleon-soft-i2c-5v`. Its flags enable
`USE_CHAMELEON` and select the PB5-switched software-I2C backend. The branch
does not rebuild or replace the v1.6 PB13/PB14 artifacts. The boot banner must
identify this image as `[soft-i2c-5v]` so a field log cannot be confused with
the prior I2C2 debug builds.

## Test requirements

### Native tests

The software-I2C tests drive a fake wired-AND bus and verify:

- zero-data address probe and address shifting;
- ACK and NACK handling for address and data bytes;
- most-significant-bit-first writes;
- repeated START on register reads;
- ACK on intermediate read bytes and NACK on the last byte;
- SCL clock stretching within the deadline;
- SCL held low, SDA held low, and missing-reader exits;
- nine-clock bus clear plus STOP;
- Standard-mode START, repeated-START, STOP, and bus-free timing;
- the STM32 16-bit TIM2 counter extending correctly across `0xffff`;
- released lines after every success and failure.

Lifecycle tests verify the exact power/bus ordering, configured startup delay,
bounded probe, 2 s boot-ready limit, configured cold-retry policy, at most two
sessions, watchdog service, fault flags, and unconditional cleanup. Static
guards verify PB12/PB13 open-drain/no-pull ownership, absence of I2C2 from the
Chameleon adapter, restored PB14 vendor behavior, MOD3 locking, the forbidden
`DEBUG` combination, build-target identity, and unchanged payload layout.

All existing host tests must stay green. The ARM build must succeed from a
clean object directory, and the ELF/map inspection must show the expected
PB5 software-I2C image rather than either v1.6 power backend.

### Bench gates

Tests use the actual field binary, reader, array, cable, and battery type.

1. **Off-state characterization:** record reader VCC, SDA, and SCL at rail-off
   and after 0.1, 0.5, 1, 5, 30, 60, and 300 s, or until all three remain below
   0.1 V. Repeat after a successful acquisition and after every injected
   failure. Then use a temporary current-limited load to discharge +5 V below
   0.1 V, remove the load while PB5 remains off and PB12/PB13 remain analog,
   and observe the rail for another 5 minutes. A rebound to 0.3 V or more, or
   more than 5 uA sourced into the discharged reader branch, indicates a real
   leakage/back-power path and stops progression. Natural residual voltage
   before this forced-discharge check is stored-charge evidence, not a
   back-power verdict.
2. **Retry policy:** retain the same-cycle retry only if 150% of the measured
   passive discharge time fits within the 5 s retry-delay cap; use at least a
   1 s delay. Otherwise disable same-cycle retry. Passive discharge must still
   reach below 0.1 V before the shortest deployed acquisition interval. If it
   does not, add and validate the active-discharge sequence above or reject the
   no-added-components firmware for field use.
3. **On state:** the VIA board has no qualified supply specification in the
   available files. Use 5.5 V as the hard maximum, including overshoot, and
   require at least 3.0 V throughout a measurement for the DS18B20. A loaded
   rail below 4.5 V is a boost/load diagnostic, not an automatic rejection when
   it stays above 3.0 V and the reader remains correct. Idle SDA/SCL rise to the
   reader rail without exceeding 5.5 V. The measured rate is no more than
   100 kHz, and 30-70% rise time at the reader end is no more than 1 us with the
   longest intended field cable.
4. **Enable transient:** use a single-shot scope capture of +5 V, SDA, and SCL
   at 10 MS/s or faster where available, with 1 MS/s as the minimum. Record the
   MCU VDD minimum, loaded steady-state voltage, overshoot, and settling time.
   VDD must remain at or above 2.0 V and produce no reset flag or corrupted
   acquisition. Set the startup delay from the measured settling time with
   margin. Test a cold or passivated cell, not only a bench supply, and archive
   the verified RT9266 datasheet before interpreting converter behavior.
5. **Protocol endurance:** at least 500 one-minute acquisition sessions give
   zero MCU resets, no session beyond 12 s, at least 99% clean samples, and the
   correct flag on every failure. The image prints and clears the RCC reset
   flags once at boot; the normal result line retains the per-acquisition
   attempts count.
6. **Fault injection:** for 20 cycles each, test SDA open, SCL grounded, SDA
   grounded, reader VCC disconnected, reader absent, reader hot-plugged, the
   maximum intended cable, and a forced pre-sleep cleanup during an active
   session. The node must uplink every cycle, recover within one cycle after
   the fault is removed, and keep the rail off throughout sleep.
7. **Power:** log current for at least 24 hours against the same unit's firmware
   baseline. Target added average current is at most 250 uA at a 5-minute
   interval or 60 uA at a 20-minute interval; the steady off-state increment is
   at most 5 uA after stored charge has dissipated. Any average-current target
   exceedance needs a written battery-life calculation before field testing. A
   result above five times target, or an off-state increment above 5 uA,
   rejects the build.
8. **Field soak:** two units run for at least four continuous weeks. Pass means
   `i2c_missing` below 0.5%, no fault block over 30 minutes, no unexplained
   frame-counter reset, no sustained downward battery-voltage trend, stable
   array IDs, and Chameleon values consistent with the known-good range or a
   co-located reference.

Gate 1 separates stored energy from a powered-off source. Gate 4 rejects the
stock +5 V choice only when VDD leaves the stated operating range, the switched
rail cannot settle before communication, the MCU resets, or acquisition is
corrupted. Firmware timing may accommodate bounded settling; it cannot waive
those electrical failures.

## Files expected to change during implementation

- create `inc/chameleon_soft_i2c.h`;
- create `src/chameleon_soft_i2c.c`;
- modify `src/chameleon_lsn50_hw.c` and its header;
- modify `src/bsp.c` only where I2C2 ownership and the boot identity change;
- restore the PB14-specific vendor paths in `bsp.c`, `main.c`, `at.c`, and
  `stm32l0xx_it.c` by comparison with the upstream firmware;
- modify `build/build.sh` for the one branch-specific target;
- add focused native tests and update existing static guards;
- replace the v1.6 wiring README with a branch-specific bench guide;
- port the bounded LSI-frequency startup fix with its host guard.

`via_chameleon.c`, `chameleon_payload.c`, and their public data contracts are
outside the expected production-code diff.
