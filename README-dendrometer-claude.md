# LSN50V2 Ratiometric Dendrometer — Rollout Notes (Claude fork)

This fork replaces stock `MOD=3`'s 6-sample PA0+PA1 read with a 20-sample
paired oversampling routine on the same pins. The **wire payload layout is
unchanged from stock Dragino MOD=3** (12 bytes with `USE_SHT` compiled in),
so the gateway decoder treats the frame like any other stock MOD=3 frame.
The only functional difference is that the `oil` / `ADC_1` fields carry
higher-quality averages.

## Payload (`MOD=3`, 12 bytes, big-endian, `USE_SHT` defined)

| Offset | Bytes | Field                                                   |
|--------|-------|---------------------------------------------------------|
| 0-1    | 2     | `oil` mV (PA0 signal, 20-sample paired avg × batV/4095) |
| 2-3    | 2     | `ADC_1` mV (PA1 reference, 20-sample paired avg)        |
| 4-5    | 2     | `ADC_2` mV (PA4, unused by dendrometer wiring, = 0)     |
| 6      | 1     | status: `(switch<<7) \| (in1<<1) \| 0x08 \| (exit_temp&1)` |
| 7-10   | 4     | SHT20/SHT31 temp×10 + hum×10, OR BH1750 illum + `0x0000` |
| 11     | 1     | `batteryLevel_mV / 100`                                 |

The mode nibble at `(buf[6] >> 2) & 0x1f` decodes to `2`; gateway code adds 1
to yield `MOD3`. Status byte, SHT/BH1750 bytes, and battery byte are
byte-identical to stock — only the PA0/PA1 measurement path is different.

## What changed vs stock

- **Sampling**: stock reads PA0 six times then PA1 six times, each with 10 ms
  spacing, with a 50 ms inter-channel gap. The Claude fork enables a
  dedicated 5 V rail, settles 50 ms, then **interleaves** 20 PA0/PA1 reads at
  10 ms spacing (paired). Total dwell: ~250 ms (vs stock's ~170 ms).
- **Why 10 ms spacing**: half a 50 Hz mains-hum period. Consecutive samples
  land on opposite phases of any mains pickup and cancel in pairs. Sample
  count must stay even to preserve the rejection.
- **Why 20 samples**: 20 × 10 ms = 200 ms = 10 full 50 Hz cycles → perfect
  50 Hz rejection plus +5.2 dB random-noise averaging vs stock's 6 samples
  per channel.
- **PB4 / OIL_CONTROL**: not toggled on the dendrometer path. The production
  dendrometer wiring does not use it. (Stock MOD=8 still toggles it; stock
  MOD=3 through the combined `else if((mode==3)||(mode==8))` branch is
  unreachable for mode==3 because the new dendrometer branch runs first, so
  mode==8 keeps its exact stock behavior.)
- **PA4**: not read on the new mode==3 path. `ADC_2` is left at 0.
- **I²C SHT / BH1750**: read unchanged via the stock
  `if((mode==1)||(mode==3))` block in `BSP_sensor_Read`. Those bytes are
  written to the wire at offsets 7-10, exactly like stock.

## Ratio vs absolute

On the gateway, `dendroRatio = adcCh0V / adcCh1V` cancels VDDA variation
(VDDA ≈ `batV` on LSN50V2). Because PA0 and PA1 are sampled paired at 10 ms
cadence, any slow VDDA drift during the 200 ms burst is also mostly
cancelled. Raw mV values remain available for diagnostics.

## Gateway protection (REF_HIGH guard)

When VDDA sags during heavy LoRa TX, the divider can push both channels
toward 4095 and collapse the ratio to ~1.0 (indistinguishable from "sensor
at midpoint"). The gateway's `buildDendroDerivedMetrics` flags
`ratioInvalidReason = 'reference_voltage_too_high'` when PA1 exceeds 95% of
`batV`.

## Deploy order

1. Update gateway first: ship the matched `osi-dendro-helper` and
   `flows.json` changes (`feature/lsn50-dendrometer-decoder-claude` branch
   in the osi-os repo).
2. Restart Node-RED on the gateway.
3. Flash new firmware to one LSN50V2 dendrometer unit.
4. Join EU868, confirm uplinks, confirm the gateway populates
   `device_data.adc_ch0v`, `adc_ch1v`, and `dendro_position_mm` correctly
   for the calibrated zone.
5. Only then roll the firmware forward to other field units.

## Rollback

- Reflash the Dragino stock `EU868.hex` from
  `kDrive/OSI OS/Hardware/Dragino LSN50/V2/LSN50 & LSN50-v2/Firmware/v1.8.2/EU868.hex`.
- The stock 12-byte MOD=3 wire layout is what the gateway now expects, so
  stock firmware and Claude firmware both decode correctly after this
  rollout. Rollback is non-breaking for gateway-side decoding.

## Tests

- Host unit tests (firmware): `make -C tests test` (native gcc, no ARM toolchain needed)
- Gateway decoder tests: `npm test` in
  `conf/full_raspberrypi_bcm27xx_bcm2712/files/usr/share/node-red/osi-dendro-helper`
- ARM smoke compile: requires Keil (MDK-ARM) or IAR (EWARM) on a separate host
- End-to-end staging check: Silvan Pi (100.81.220.8) per the execution plan
