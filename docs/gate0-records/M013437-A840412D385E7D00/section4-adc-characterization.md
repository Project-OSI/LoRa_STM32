# Gate 0 §4 — ADC characterization and zero-ambiguity guard

Node A840412D385E7D00 (LSN50v2 v2.3a, STM32L072) · probe ML3 M013437 · 2026-08-07 · operator Phil.
Firmware: bench variant `lora-bench.hex` at commit `2c355bc` (`AT+ML3ADC`, 64-sample average, first-conversion discard, VREFINT stabilization delay). Reference instrument: handheld DMM.

**Front-end: NONE. All measurements below were taken with the input pins unfiltered** — the planned 1 kΩ series + 100 nF network was not fitted (the 109 nF capacitor was measured but never connected; confirmed by the operator after the session). The probe drove PA0/PA1 directly. This is a deviation from the runbook, which specifies the sweep be performed with the production front-end in place. It does not invalidate the accuracy results — meter and ADC measured the same physical pin voltage in every case — but it does mean the production RC network is uncharacterized (see Findings 6).

## Verdict

**PASS.** The direct-ADC design is confirmed. No external ADC is required for the ML3, and decision D8 resolves to the direct STM32 path.

The failure mode this section exists to detect is a negative ADC offset that would clip the probe's near-ground signal to zero, making "dry soil" indistinguishable from "broken wire". **It does not occur on this board.** The measured offset is small and **positive**.

## Method note (deviation from the runbook, deliberate)

The runbook's injection table ({0, 2, 5, 10, 20, 50, 100} mV from a bench source) was **abandoned as unusable**: the available lab supply could not hold a stable output at those levels. Evidence — at a nominal 113 mV the 64 samples within a single burst ranged over codes 90–239 (≈79–209 mV), and the meter/ADC disagreement flipped sign between points (−16 mV at one setting, +68 mV at another). A sign-flipping error is diagnostic of an unstable source, not of converter error.

Replaced by a method that is both cleaner and more relevant: two stable known-voltage anchors (a zero point requiring no supply, and a VDD/2 resistive divider off the node's own regulated rail), then **verification against the actual ML3 probe across its full operating range**. The probe is a low-impedance, quiet source sitting at exactly the voltages of interest, so it removes the noisy-supply variable and tests the production configuration rather than a proxy for it.

## Anchor measurements

| Input | Meter | ADC mean code | Sample spread | ADC volts (VDDA-corrected) | Error |
|---|---|---|---|---|---|
| ~0 V (supply output off, 1 kΩ to open supply) | — | 0.39 | MIN 0 / MAX 1 | 0.35 mV | apparent offset **+0.35 mV** |
| VDD/2 (1 kΩ:1 kΩ divider) | 1832 mV | 2050.31 | MIN 2050 / MAX 2051 | 1832.5 mV | **+0.5 mV** |

Implied transfer function: slope 1.11895 codes/mV against an ideal 1.11885 — a match to one part in ten thousand.

**Caveat on the zero anchor:** the input was not actively driven to 0 V; the supply's output was switched off, leaving PA0 tied through 1 kΩ to a (presumed high-impedance) supply output with no capacitor fitted. The reading was nonetheless quiet and repeatable (mean 0.39, MIN 0, MAX 1), and the sign of the result is what matters — an ADC clipping a negative offset would read a flat 0 with no dither, which is not what occurred. The offset magnitude should be re-derived from a driven zero when the front-end is fitted. **The small-signal conclusion does not rest on this anchor**: the air-condition probe measurement (5.6 mV meter vs 5.66 mV ADC, both instruments on a driven low-impedance source) demonstrates millivolt accuracy directly.

## Verification against the ML3 probe (production configuration)

Probe powered at 5.0 V from bench supply; blue→PA0 (signal HI), black→PA1 (signal LO), brown+green→common ground, grey (thermistor) unconnected. VDDA reference 3655 mV (`VDDA_LAST`).

| Condition | Leg | Meter | ADC mean code | ADC volts (corrected) | Error |
|---|---|---|---|---|---|
| Air | PA0 (HI) | 5.6 mV | 6.34 / 6.44 | 5.66 / 5.75 mV | ≤ +0.15 mV |
| Air | PA1 (LO) | 5.0 mV | 5.53 / 5.84 | 4.94 / 5.21 mV | ≤ +0.21 mV |
| Damp cloth | PA0 (HI) | 231.0 mV | 259.00 | 231.2 mV | **+0.2 mV** |
| Damp cloth | PA1 (LO) | 5.0 mV | 5.63 | 5.03 mV | +0.03 mV |
| Water (immersed) | PA0 (HI) | 1110 mV | 1243.75 / 1244.13 | 1110.1 / 1110.4 mV | **+0.1 mV** |
| Water (immersed) | PA1 (LO) | 5.0 mV | 5.83 / 5.72 | 5.20 / 5.11 mV | ≤ +0.20 mV |

**Accuracy ≤ 0.2 mV across 5.6 mV → 1110 mV**, i.e. across the ML3's entire output span, verified against an independent reference at every point. Well inside the runbook's 1.0 mV correctability bar, before any calibration is applied.

Predictive check: the transfer function derived from the anchors predicted ~6.7 codes for the air condition; the probe delivered 6.34 and 6.44. The model describes the hardware.

## Macro feeds

| Macro | Value | Basis |
|---|---|---|
| `ML3_CONFIG_ZERO_AMBIGUITY_GUARD_MV` | candidate **2 mV** | offset +0.35 mV, zero-point noise < 1 LSB (0.89 mV); 2 mV clears both with margin and sits ~3.5× below the probe's observed 5.0–5.6 mV floor. Confirm against remaining boards before freezing. |
| Common-mode envelope | LO observed 5.0–5.2 mV across every condition | LO leg is stable and never negative; feeds `ML3_CONFIG_CM_RANGE_MIN_MV`/`_MAX_MV` |
| Signal ceiling | 1110 mV observed (above the 1 V nominal) | calibration model and payload range must accommodate >1 V |
| Near-rail accuracy | ≤ 0.2 mV, correctable | §4 pass criterion (≤ 1.0 mV) met without calibration |

## Findings for firmware

1. **VREFINT first-conversion artifact: diagnosed, fixed, re-verified on hardware.** Pre-fix, the first and last VDDA figures within one command disagreed by 92 mV (2.6%); the entire difference was traced arithmetically to a single unsettled sample reading ~3590 codes, and confirmed against the vendor HAL, which applies `ADC_TEMPSENSOR_DELAY_US` for the temperature sensor but never the documented `LL_ADC_DELAY_VREFINT_STAB_US` on the VREFINT path. After `2c355bc` (first-conversion discard + 10 µs delay) the gap is 13 mV (0.36%).
2. **Residual settling tail (open).** `VREF_MAX_FIRST` still reads ~1502–1506 against a settled ~1372, while the last block sits tight at 1371–1374. One discard plus 10 µs removes most but not all of the transient; the mean shift implies several high samples decaying toward the settled value, not a single outlier. Recommended: lengthen the delay or discard two conversions, then re-measure. Impact at present is 0.36% of scale (≈0.02 mV at the probe's air level), so it is not blocking.
3. **Production engine must be checked for the same defect.** `adc_precision` already performs a channel-change discard, which likely covers it, but the VREFINT *stabilization delay* specifically must be confirmed present on the production path during Task 10. A 2.6% reference error would otherwise propagate into every calibrated reading.
4. **No significant load on PA0.** An apparent 6–8 mV drop across the series resistor during the noisy-supply phase was an artifact of measuring a fluctuating signal sequentially; the VDD/2 divider read half the rail to within 2 mV, ruling out a board-level pull-down that would have attenuated the production signal.
5. **Sensor noise is real and expected, and was measured with NO input filtering.** Per-burst spread at the probe was ~10 codes in air and 8–12 codes at higher levels, while burst-to-burst means reproduced to ~0.1 code. Averaging works; the production ABBA multi-cycle scheme is well matched to this. Fitting the specified RC network should only reduce this spread.
6. **The production RC front-end (1 kΩ + 100 nF) is UNCHARACTERIZED.** It was not fitted for any measurement here. Expected impact on DC accuracy is nil: the VDD/2 divider read half the rail through a 1 kΩ source impedance, ruling out a board-level load that would drop voltage across a series resistor, and the 100 nF acts as a local charge reservoir far larger than the ADC's sampling capacitor. But "expected" is not "measured". Re-run at least the air and water conditions with the network fitted before freezing the front-end design, since a 1 kΩ resistor in front of a 5 mV signal is where an unnoticed leakage path would show up.

## Still open for this section

- **Re-run air + water with the 1 kΩ + 100 nF front-end fitted** (Finding 6) and re-derive the offset from a driven zero.
- Remaining three boards/probes (per-unit `V_guard`; near-rail behavior is a per-unit property and is not sampled).
- Temperature extremes; fresh-vs-depleted battery repeat.
- Power-up/power-down transient captures (no oscilloscope available on this bench).
- §3 intermediate dry-soil condition (air and water endpoints plus a damp intermediate are banked).
