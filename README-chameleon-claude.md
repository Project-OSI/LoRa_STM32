# LSN50 + VIA Chameleon I2C Rollout Notes

This fork adds a standalone Chameleon-only firmware variant for the Dragino
LSN50. It uses the LSN50 MOD=3 sampling/uplink path for a VIA Chameleon I2C
read and emits a 32-byte stock-MOD=3-aligned V2 payload on the configured
Dragino application port, default FPort 2. Chameleon builds are MOD3-only:
fresh/FDR devices default to MOD3, stale EEPROM modes are clamped back to MOD3,
and AT/downlink MOD setters cannot move the running firmware out of MOD3.

## Build Variants

| Target | Defines / overrides | Payload |
|---|---|---|
| `chameleon` | `-UUSE_SHT -DUSE_CHAMELEON` | 32-byte V2 stock-aligned Chameleon payload, configured/default port |
| `chameleon-dummy` | `-UUSE_SHT -DUSE_CHAMELEON -DCHAMELEON_DUMMY` | Same V2 shape; canned values, no I2C |

Build with:

```bash
./build/build.sh chameleon
./build/build.sh chameleon-dummy
```

## Wiring

| LSN50 | VIA Chameleon |
|---|---|
| GND | GND |
| VDD (3.3 V) | VCC |
| PB6 | SCL |
| PB7 | SDA |

Power the reader at 3.3 V, never 5 V. The onboard pull-ups would otherwise
pull SDA/SCL to 5 V and risk damaging the STM32.

## Chameleon V2 Payload

Configured/default FPort 2, big-endian, 32 bytes.

The first 8 bytes keep the stock no-SHT MOD=3 frame: ADCs, status, and the
one-byte `batteryLevel_mV / 100` field. The Chameleon extension starts at
offset 8. Raw resistance values are omitted from routine V2 uplinks; SWT
conversion uses compensated resistance values.

| Offset | Size | Field |
|---|---:|---|
| 0 | 2 | PA0 / oil ADC mV |
| 2 | 2 | PA1 / ADC_1 mV |
| 4 | 2 | PA4 / ADC_2 mV |
| 6 | 1 | stock MOD=3 status byte |
| 7 | 1 | battery / 100 mV |
| 8 | 1 | payload version (`0x02`) |
| 9 | 1 | simplified Chameleon status flags |
| 10 | 2 | soil temperature x100 |
| 12 | 4 | compensated R1 (ohms) |
| 16 | 4 | compensated R2 (ohms) |
| 20 | 4 | compensated R3 (ohms) |
| 24 | 8 | DS18B20 array ID |

V2 status flag bits:

| Bit | Meaning |
|---|---|
| 0 | data invalid; do not trust trailing Chameleon fields |
| 1 | temperature fault (`-127 C` sentinel) |
| 2 | ID fault (`0xFF` x 8) |
| 3..7 | reserved, always 0 |

Decoder rule: inspect byte 9 before trusting bytes 10-31. If bit 0 is set, the
Chameleon measurement fields are zero-filled placeholders and the decoder
stores them as null. Per-channel open state is derived downstream from
compensated resistance equal to `10_000_000` ohm. Dry-connected probes that
saturate raw readings near `9_999_999` ohm are not treated as open by V2.

On Chameleon-only deployments with nothing wired to PA0/PA1/PA4, bytes 0-5
are floating ADC noise and should be ignored downstream. They remain in the
payload only to preserve stock MOD=3 layout.

Stock LSN50 frames are big-endian and use the configured application port.
This firmware preserves that convention. The I2C driver normalizes the
Chameleon slave's little-endian register responses before the payload encoder
writes big-endian uplink bytes.

### Legacy V1 Payload

Already-flashed V1 devices emit the previous 44-byte payload version `0x01`
with raw resistance fields at offsets 24/28/32 and the array ID at offset 36.
The OSI OS decoder still accepts V1 frames for rollout compatibility. New
Chameleon firmware builds emit V2 by default.

## kPa Conversion

Resistance to soil-water tension is computed by the server, not the node:

```text
x = ohms / 1000
kPa = a * ln(x) + b * x + c
```

Coefficients ship from VIA on paper; provisioning UX is out of scope for this
firmware version.

## Bring-Up Sequence

1. Build `chameleon-dummy`, flash it, and observe the raw configured/default
   FPort frame in ChirpStack or gateway logs. The first 8 bytes are live stock
   MOD=3 ADC/status/battery fields. The Chameleon extension bytes match the
   canned V2 values: compensated 1.6 kohm / 100 kohm / 1.6 Mohm,
   `DE AD BE EF DE AD BE EF` ID. That dummy ID is synthetic and intentionally
   does not look like a real DS18B20 ROM code.
2. Build `chameleon`, flash it, and leave the reader disconnected. Expect
   `status_flags & 0x01` set at offset 9 for data invalid. The same flag is
   expected if the STM32 I2C peripheral fails to initialise; the firmware does
   not hard-hang on that fault.
3. Connect the reader without soil probes. Expect compensated channel values
   to report the 10 Mohm sentinel; the decoder derives channel-open state from
   those values.
4. Connect a real probe set. Expect realistic resistances and clean flags. If
   temp fault bit 1 appears, it refers to the Chameleon-side DS18B20 path
   (`0x01` / `0x30`), not the LSN50 external DS18B20 path.
5. Run a 24-48 h soak.

Cycle time note: one acquisition includes the stock MOD=3 ADC sweep plus a
Chameleon measurement trigger/poll and 11 I2C register reads. A timeout can
add up to 2 s before the uplink; this is acceptable at minute-scale uplink
intervals but should be considered if the transmit interval is shortened.

## Dummy Payload Extension

For `chameleon-dummy`, bytes 8-31 after the variable stock prefix are:

```text
02 00 07 D0 00 00 06 40 00 01 86 A0 00 18 6A 00
DE AD BE EF DE AD BE EF
```

The complete frame shape is:

```text
<adc_pa0_be> <adc_pa1_be> <adc_pa4_be> <mod3_status> <bat_100mv>
02 00 07 D0 00 00 06 40 00 01 86 A0 00 18 6A 00
DE AD BE EF DE AD BE EF
```

## Rollback

- Flash the last known-good LSN50 firmware image for the target device.
- Or flash Dragino stock firmware for the matching LSN50 v2 region.
