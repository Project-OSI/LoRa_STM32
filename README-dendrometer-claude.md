# LSN50V2 Ratiometric Dendrometer — Rollout Notes (Claude fork)

This fork extends stock `MOD=3` with oversampled ratiometric dendrometer
measurement. The payload format changed; the gateway decoder in `osi-os`
must be updated before field units are flashed.

## Payload (`MOD=3`, 8 bytes, big-endian)

| Byte | Field                  |
|------|------------------------|
| 0-1  | battery_mv             |
| 2-3  | adc_signal_avg_raw     |
| 4-5  | adc_reference_avg_raw  |
| 6    | status_byte            |
| 7    | dendro_flags           |

Flags: `VALID=0x01`, `REF_LOW=0x02`, `REF_HIGH=0x04`, `ADC_FAIL=0x08`.

## Tunables (compile-time, `dendrometer.h`)

- `DENDRO_SAMPLE_COUNT` — default 50
- `DENDRO_SETTLE_MS` — default 50
- `DENDRO_INTER_SAMPLE_MS` — default 1
- `DENDRO_REF_MIN_RAW` — default 128
- `DENDRO_REF_MAX_RAW` — default 4080

Override at build time with `-D` if experimentation is needed.

## Deploy order

1. Update gateway first: ship the matched `osi-dendro-helper` change
   (`feature/lsn50-dendrometer-decoder-claude` branch in the osi-os repo).
2. Restart Node-RED on the gateway.
3. Flash new firmware to one LSN50V2 dendrometer unit.
4. Join EU868, confirm uplinks, confirm the gateway populates
   `device_data.dendro_position_mm` correctly for the calibrated zone.
5. Only then roll the firmware forward to other field units.

## Rollback

- Reflash the dragino stock `EU868.hex` from
  `kDrive/OSI OS/Hardware/Dragino LSN50/V2/LSN50 & LSN50-v2/Firmware/v1.8.2/EU868.hex`.
- The legacy decoder path in `osi-dendro-helper` is unchanged, so stock frames
  still decode correctly.

## Tests

- Host unit tests: `make -C tests test` (native gcc, requires no ARM toolchain)
- ARM smoke compile: see `docs/superpowers/plans/2026-04-18-lsn50v2-dendrometer-claude.md`, Task 12
- Full firmware image: requires Keil (MDK-ARM) or IAR (EWARM) on a separate host
