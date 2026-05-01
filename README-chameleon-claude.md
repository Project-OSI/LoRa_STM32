# LSN50 + VIA Chameleon I2C Rollout Notes

This fork adds a standalone Chameleon-only firmware variant for the Dragino
LSN50. It uses the LSN50 MOD=3 sampling/uplink path for a VIA Chameleon I2C
read and emits a 44-byte stock-MOD=3-aligned payload on the configured Dragino
application port, default FPort 2.

## Build Variants

| Target | Defines / overrides | Payload |
|---|---|---|
| `chameleon` | `-UUSE_SHT -DUSE_CHAMELEON` | 44-byte stock-aligned Chameleon payload, configured/default port |
| `chameleon-dummy` | `-UUSE_SHT -DUSE_CHAMELEON -DCHAMELEON_DUMMY` | Same shape; canned values, no I2C |

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

## Payload

Configured/default FPort 2, big-endian, 44 bytes.

The first 8 bytes keep the stock no-SHT MOD=3 frame: ADCs, status, and the
one-byte `batteryLevel_mV / 100` field. The Chameleon extension starts at
offset 8.

| Offset | Size | Field |
|---|---:|---|
| 0 | 2 | PA0 / oil ADC mV |
| 2 | 2 | PA1 / ADC_1 mV |
| 4 | 2 | PA4 / ADC_2 mV |
| 6 | 1 | stock MOD=3 status byte |
| 7 | 1 | battery / 100 mV |
| 8 | 1 | payload version (`0x01`) |
| 9 | 1 | Chameleon status flags |
| 10 | 2 | soil temperature x100 |
| 12 | 4 | compensated R1 (ohms) |
| 16 | 4 | compensated R2 (ohms) |
| 20 | 4 | compensated R3 (ohms) |
| 24 | 4 | raw R1 (ohms) |
| 28 | 4 | raw R2 (ohms) |
| 32 | 4 | raw R3 (ohms) |
| 36 | 8 | DS18B20 array ID |

Status flag bits: `0` = I2C missing, `1` = timeout, `2` = temp fault
(-127 C), `3` = ID fault (`0xFF` x 8), `4..6` = R1..R3 open
(compensated or raw 10 Mohm sentinel), `7` reserved.

On Chameleon-only deployments with nothing wired to PA0/PA1/PA4, bytes 0-5
are floating ADC noise and should be ignored downstream. They remain in the
payload only to preserve stock MOD=3 layout.

Stock LSN50 frames are big-endian and use the configured application port.
This firmware preserves that convention. The I2C driver normalizes the
Chameleon slave's little-endian register responses before the payload encoder
writes big-endian uplink bytes.

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
   canned values: compensated and raw both 1.6 kohm / 100 kohm / 1.6 Mohm,
   `DE AD BE EF DE AD BE EF` ID.
2. Build `chameleon`, flash it, and leave the reader disconnected. Expect
   `status_flags & 0x01` set at offset 9 for I2C missing.
   The same flag is expected if the STM32 I2C peripheral fails to initialise;
   the firmware does not hard-hang on that fault.
3. Connect the reader without soil probes. Expect `0x70` at offset 9 for
   R1/R2/R3 open.
4. Connect a real probe set. Expect realistic resistances and clean flags. If
   temp fault bit 2 appears, it refers to the Chameleon-side DS18B20 path
   (`0x01` / `0x30`), not the LSN50 external DS18B20 path.
5. Run a 24-48 h soak.

Cycle time note: one acquisition includes the stock MOD=3 ADC sweep plus a
Chameleon measurement trigger/poll and 11 I2C register reads. A timeout can
add up to 2 s before the uplink; this is acceptable at minute-scale uplink
intervals but should be considered if the transmit interval is shortened.

## Dummy Payload Extension

For `chameleon-dummy`, bytes 8-43 after the variable stock prefix are:

```text
01 00 07 D0 00 00 06 40 00 01 86 A0 00 18 6A 00
00 00 06 40 00 01 86 A0 00 18 6A 00 DE AD BE EF
DE AD BE EF
```

The complete frame shape is:

```text
<adc_pa0_be> <adc_pa1_be> <adc_pa4_be> <mod3_status> <bat_100mv>
01 00 07 D0 00 00 06 40 00 01 86 A0 00 18 6A 00
00 00 06 40 00 01 86 A0 00 18 6A 00 DE AD BE EF
DE AD BE EF
```

## Rollback

- Flash the last known-good LSN50 firmware image for the target device.
- Or flash Dragino stock firmware for the matching LSN50 v2 region.
