# ML3 Gate 0 bench runbook

Gate 0 is the go/no-go characterization that decides whether the direct-ADC design proceeds or the external-ADC fallback is selected. No firmware work beyond bench scripts happens before it completes, and every `GATE0-PENDING` macro in `ml3_config.h` takes its value from a measurement recorded here. This runbook turns plan Part II (`docs/2026-07-12-lsn50v2-ml3-firmware-plan.md`, §2.1–§2.6) into a bench procedure; the plan text governs on any conflict.

Deliverable: one written go/no-go decision (§6 below) with the measurement records attached, one record set per node. Four nodes, four ML3 probes — near-rail behavior is a per-unit property, so nothing is sampled.

## 1. Equipment

| Item | Requirement |
|---|---|
| Voltmeter / reference meter | traceable calibration, uncertainty ≤ 0.25 mV (a quarter of the tightest Gate 1 limit) |
| Oscilloscope | for power-up/down transients and warm-up settling |
| Bench voltage source | adjustable 0 mV – 3.3 V, mV resolution, for §4 near-rail injection |
| Battery simulation | fresh cell and depleted cell (or programmable source at both levels) |
| Temperature | access to the low/high field extremes where feasible (chamber or outdoor soak) |
| The node's RC network | 1.00 kΩ / 100 nF per plan §3.2, fitted before §4 |

## 2. Baseline capture (per node, §2.1)

Record before any measurement:

1. LSN50v2 PCB hardware revision.
2. STM32 device marking / silicon revision.
3. Existing firmware version and commit if previously flashed.
4. Dragino `LoRa_STM32` upstream baseline commit (currently `aaa4b50`).
5. LoRaWAN region, data rate, channel configuration → pins the §3.13 airtime budget and fills `ML3_CONFIG_LORA_REGION_ID`, `ML3_CONFIG_LORA_DATARATE`, `ML3_CONFIG_MAX_FRMPAYLOAD_BYTES`.
6. Availability of PA0, PA1, PA4, one spare ADC pin, one spare GPIO in the actual wiring → resolves open decision D3 (thermistor pin + excitation GPIO) and fills `ML3_CONFIG_THERMISTOR_ADC_CHANNEL`, `ML3_CONFIG_THERMISTOR_EXCITATION_GPIO`.
7. ML3 serial number, cable length, and measured cable loop resistance.
8. PB5 behavior during reset, ISP/bootloader entry, and brownout, with the board's actual pull arrangement. The +5 V rail must be provably off in all three states; add or verify an external pull if not. Fills `ML3_CONFIG_PB5_ACTIVE_LOW` and `ML3_CONFIG_PB5_RESET_ISP_BROWNOUT_SAFE`. Firmware cannot fix a floating active-low enable — this is a hardware pass/fail.

## 3. ML3 signal envelope and supply compliance (§2.2)

For each probe, measure `V_HI−GND`, `V_LO−GND`, `V_HI−LO` with the high-impedance meter under each condition: unpowered; power-up transient (scope); air; dry soil; intermediate moisture; wet soil; the manufacturer water functional check; power-down transient (scope). Repeat the standing-value points at fresh and depleted battery, and at temperature extremes where feasible.

Simultaneously measure supply voltage **at the ML3 terminals** (not the node connector) under the worst case: depleted battery, minimum expected temperature, maximum cable length, startup inrush with the fitted bulk capacitance.

| Record | Feeds |
|---|---|
| Common-mode envelope `[LO_min, LO_max]` | `ML3_CONFIG_CM_RANGE_MIN_MV` / `_MAX_MV`, calibration sweep range |
| Minimum observed headroom per input | the §4 zero-ambiguity guard check |
| Supply-compliance record | `ML3_CONFIG_V5_MINIMUM_MV` / `_MAXIMUM_MV` |

Fail conditions (any probe): either signal negative relative to node ground under any condition; either signal above measured VDDA; startup/shutdown transients beyond STM32 injection limits after the RC network; supply at the probe terminals below the ML3's 5.0 V minimum under worst case; minimum observed LO or HI does not clear the §4 guard. Any fail → front-end change or fallback decision at §6.

## 4. Near-rail characterization and the zero-ambiguity guard (§2.3)

On all four boards, with the RC network fitted and the bench source driving the input: measure oversampled, VREFINT-compensated ADC error at {0, 2, 5, 10, 20, 50, 100} mV and {VDDA−100, VDDA−20} mV against the reference meter.

Two outputs per board:

1. **Zero-ambiguity guard `V_guard`** — the smallest input reliably distinguishable from a clipped-at-zero reading (ADC offset + noise + margin, derived from this data, never zero). Both ML3 inputs must exceed it under every §3 condition. Fills `ML3_CONFIG_ZERO_AMBIGUITY_GUARD_MV`.
2. **Near-rail accuracy** — residual above `V_guard` must be correctable to ≤ 1.0 mV by the calibration model (linear or piecewise). Uncorrectable → fallback.

The §3.4 divider constraint is checked here too: worst-case `V5_max/2` must stay below worst-case `VDDA_min` minus margin. If violated, re-ratio (e.g. 68k/33k) and record the measured per-node ratio → `ML3_CONFIG_V5_DIVIDER_RATIO_PPM`.

## 5. Warm-up settling, coarse (§2.4)

Scope the ML3 output settling after power-up, per probe, room temperature. Confirms the manufacturer's 0.5–1.0 s figure and that 3000 ms bounds the configuration range; the definitive firmware-driven sweep happens in Phase 2 (§4.2). Provisional value fills `ML3_CONFIG_WARMUP_TIME_MS`; also derive `ML3_CONFIG_DISCHARGE_THRESHOLD_MV` and `_TIMEOUT_MS` from the observed rail-discharge curve through the PA4 divider.

## 6. Effort comparison and decision (§2.5–§2.6)

Write one page comparing the remaining direct-ADC cost (plan Parts III–IV) against the fallback. The fallback side must name a specific validated circuit — candidate ADS1115 with exact channel usage, input-range compliance against the measured §3 envelope (its inputs are also GND-referenced; negative absolute inputs need biasing), reference-drift expectations, adapter-board effort, parts availability, and four-node replication cost. This resolves open decision D8.

Proceed with the direct design only if all of: §3 passes for all four probes (including supply compliance and headroom), §4 passes on all four boards, and the fallback does not strictly dominate the comparison. Record the signed decision; it fills `ML3_CONFIG_GATE0_APPROVAL_READY`.

Airtime closure: with region/DR pinned in §2, verify the 25-byte routine frame and every diagnostic frame length against the RP002 maximum, and fill `ML3_CONFIG_ROUTINE_MAX_AIRTIME_US` / `ML3_CONFIG_DIAGNOSTIC_MAX_AIRTIME_US`. The build gate asserts these; a failing assertion fails the build.

## 7. Record-keeping

One directory per node, named by ML3 serial + node EUI, containing: the §2 baseline sheet, raw meter/scope captures for §3–§5, the per-board `V_guard` derivation, and the §6 decision page. These records are the provenance for every number that later lands in `ml3_config.h` — a macro without a traceable record here does not get flipped to `_READY 1`.
