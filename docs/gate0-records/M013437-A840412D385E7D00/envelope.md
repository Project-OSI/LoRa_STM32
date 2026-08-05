# Gate 0 §3 signal envelope — ML3 M013437 / node A840412D385E7D00

Date: 2026-08-05. Operator: Phil. Instrument: handheld DMM (model TBD on sheet). Supply: bench DC supply at 5.0 V (deliberate worst-case minimum), white=V+, brown+green=0V, grey unconnected. Probe powered only during readings (manual on/off; ML3 warm-up 0.5–1 s respected).

## Readings

| Condition | V_HI−GND (blue) | V_LO−GND (black) | V_HI−LO signal | Supply at source |
|---|---|---|---|---|
| Air (rods free) | 6 mV | 6 mV | ≈ 0 V | 5.0 V |
| Tap water (rods immersed) | 1.046 V | 7 mV | 1.046 V | 5.0 V |
| Intermediate (damp soil) | pending | pending | pending | |

Water reading slightly above the 1.0 V nominal top — documented ML3 behavior in water without soil (QSG note "may not be 100% vol"), not a defect.

## Preliminary macro feeds (one probe, room temperature, 5.0 V supply — not yet the full envelope)

- Common-mode observed so far: LO ∈ [6, 7] mV → candidate floor input for `ML3_CONFIG_CM_RANGE_MIN_MV`/`_MAX_MV` after remaining conditions (battery levels done implicitly at 5.0 V minimum; temperature extremes deferred).
- Signal ceiling observed: 1.046 V — payload/ADC range must accommodate >1.0 V (it does; note for calibration model).
- **Critical-path flag:** resting HI (air) and LO sit at 6–7 mV — the same order as a plausible ADC zero-ambiguity guard. §4 near-rail characterization (needs bench-readout firmware) decides whether the oversampled, calibrated floor resolves these levels. Until §4, no verdict on the §3 "clears the guard" pass criterion.

## Deferred/gaps for this record

- Power-up/power-down transient captures (no oscilloscope on bench) — required by runbook §3 before Gate 0 sign-off.
- Dry-soil and wet-soil conditions, temperature extremes.
- Reset test (§2.8c): PASS 2026-08-05 — rail = stored-charge decay only (2 V → <1 V over minutes, no rise, LEDs dark). ISP + brownout tests deferred. PB5 polarity: active-low, established from vendor source (`stm32l0xx_hw_conf.h` PWR_OUT + `bsp.c:522` RESET=enable), not yet bench-confirmed.
- STM32 REV_ID backfill (ST-Link read) pending.
