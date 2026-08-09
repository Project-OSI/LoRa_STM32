# LSN50v2 Custom Firmware Plan — Direct ML3 ThetaProbe Acquisition

**Document status:** Approved design, pending Phase 0 gate
**Version:** 2.1 — v2.0 plus consolidated external-review corrections (§0.1)
**Date:** 2026-07-12
**Target platform:** Dragino LSN50v2 (STM32L072CZT6, SX1276/SX1278)
**Target sensor:** Delta-T ML3 ThetaProbe (soil moisture, differential analog output) — **only**. No Chameleon board, no I²C peripheral of any kind on these nodes.
**Deployment:** Four parcel nodes, nominal 15-minute measurement interval
**Primary objective:** Acquire the ML3 differential analog output directly with the LSN50v2 ADC, without an active external ADC, while maintaining a quantified and defensible measurement uncertainty — subject to the Phase 0 gate below.

---

## 0. Changes from draft v1.0

1. **Chameleon and I²C removed entirely** (prompt contamination in the draft). These nodes carry an ML3 only.
2. **Phase 0 characterization gate added before reachable firmware work**, with the external-ADC fallback decision taken on measured data. Before Gate 0, build-only target configuration and unregistered adapter-object compilation are permitted only while all Gate 0/Phase 2 hardware readiness macros remain zero, and `ML3_CONFIG_ACQUISITION_READY` and `ML3_CONFIG_DEPLOYABLE` remain false; that path must not make ADC access, PB5 control, thermistor excitation, EEPROM write, LoRa queue, sample processing, or physical sampling reachable.
3. **The "20 mV safe-window floor" contradiction resolved** — see §0.1 item 1 for the v2.1 refinement.
4. **ML3 thermistor read in release 1** (no other soil-temperature source exists on these nodes).
5. **MCU die-temperature readout mandatory in release 1**; temperature correction expected, not exceptional.
6. **Payload reworked** with versioning and quality metadata (§0.1 item 4 supersedes the v2.0 byte map).
7. **OSI edge/cloud integration specified concretely** (Part V).
8. **ADC section additions:** sampling-charge budget; internal-reference enable/readiness sequencing.
9. **Calibration matrix trimmed with justification**; automation mandatory.
10. **Fork baseline:** stock Dragino `LoRa_STM32`; OSI firmware conventions adopted.
11. **Shield lands on node GND (battery negative)**, not "enclosure ground".

## 0.1 Changes in v2.1 (external review consolidation, 2026-07-12)

1. **Low-rail ambiguity fixed.** The ADC conversion range starts at 0 V, so code 0 cannot distinguish a valid ~0 V input from a clipped negative one, and clipping is unrecoverable by calibration. `LO_NEG` is replaced by `LOW_RAIL_CLIPPED` (either input), such readings are **INVALID**, and Gate 0 must establish a **measured minimum headroom** for both inputs (evidence-derived guard, not the old arbitrary 20 mV — but not zero). No headroom → front-end change or fallback.
2. **Thermistor read moved to the ML3-unpowered state.** The thermistor shares the brown power-return conductor; with the probe powered, 18 mA of return current corrupts the reading through cable resistance (ML3 manual, Appendix 2 — Delta-T's published curve applies directly only with sensor power off). New order: moisture → post-references → +5 V off → verified rail discharge → thermistor excitation → settle → read. The manual's lookup table is used (this is not a generic 10K3A1B part), and the bath check is multi-point.
3. **Temperature correction made multiplicative.** VREFINT drift is a gain error (up to 100 ppm/°C ⇒ ~4 mV at 1 V over 40 °C); an additive `b3·ΔT` fitted near 1 V would be wrong near 0 V. Model is now temperature-dependent offset **and gain** (§3.10); the EEPROM record stores both tempcos. Die-temperature acquisition (ADC_IN18, `TSENSE_CAL1/2`, SYSCFG buffer enables + readiness flags) added to the channel table and sequence — it was mandated but never sequenced in v2.0.
4. **Payload validity semantics defined.** Explicit VALID/DEGRADED/INVALID quality state + valid-cycle count in the payload; sentinels for every field; HI/LO defined as VREFINT-compensated **uncalibrated** values; the corrected-differential field carries a sentinel (never uncalibrated data) when calibration is invalid. VDDA and battery — the same measurement in the stock firmware (`HW_GetBatteryLevel` derives `batteryLevel_mV` from VREFINT) — merged into one field, freeing the bytes that now carry quality metadata. Canonical `device_data` gets **NULL** for INVALID acquisitions; `ml3_readings` always keeps the flagged raw packet.
5. **Gate 0 now gates the sensor supply and the monitor divider.** 5 V is the ML3's *minimum* specified supply; compliant voltage **at the probe terminals** under worst case (depleted battery, minimum temperature, maximum cable, inrush with fitted bulk capacitance) is a pass criterion. The 47k/47k monitor must be shown not to exceed ADC range at worst-case V5_max/VDDA_min (else re-ratio). PB5's reset/ISP/brownout state must be verified fail-safe in hardware — firmware cleanup cannot fix a floating active-low enable.
6. **Fallback made concrete.** "ADS1115-class" is replaced by a requirement to select and validate a specific fallback circuit; noted that the ADS1115's own inputs must stay within GND…VDD, so a Gate 0 failure on negative absolute input defeats it too unless the fallback includes input biasing.
7. **Calibration record completed; qualification hardened.** Record now stores the +5 V divider ratio, gain/offset tempcos, common-mode reference, and an optional piecewise table; calibration ID is u8 end-to-end. Qualification adds negative differential points (to −20 mV), per-board near-zero characterization, held-out validation points for Gate 1, stated calibrator uncertainty with guard bands, common-mode sweeps at temperature extremes, and numeric soak acceptance criteria.
8. **Part V expanded to the real ingest path.** A codec branch alone is insufficient: `lsn50-decode-fn` raw-decodes every packet as a stock LSN50 frame (`decodeRawAdcPayload`) and `osi-dendro-helper` caps modes at 1–9; `lsn50-sql-fn` has a fixed column mapping. All named work items added. **D5 resolved:** `ext_temperature_c` is the DS18B20 environmental channel — ML3 soil temperature gets a new canonical `soil_temperature_c`.
9. **VWC ownership, units, and agronomic validation decided.** Edge-authoritative VWC (edge computes and stores canonical `device_data.vwc`, cloud mirrors); canonical unit is **percent 0–100** (`channels.json` `vwc` unit `%`, server thresholds 1–100) with the ML3's m³/m³ fraction multiplied by 100 exactly once; Gate 2 split into acquisition validation and agronomic validation, with the explicit statement that electrical accuracy is not parcel-moisture accuracy.
10. **Airtime constraints are a build gate.** Routine and every diagnostic frame checked against the deployed region/data-rate maximum (LoRaWAN RP002); diagnostic payload byte-mapped, fragmented, and rate-limited.

Carried over unchanged: ABBA acquisition, hardware oversampling, VREFINT compensation, errata-aware ADC sequencing, redundant CRC'd calibration storage, fail-safe +5 V discipline, transmit-flagged-data-never-substitute, raw-voltage preservation, central reprocessable conversion.

---

# Part I — Mission and constraints

## 1.1 Architecture

```text
ML3 Signal HI  ── matched passive RC network ── PA0 / ADC_IN0
ML3 Signal LO  ── matched passive RC network ── PA1 / ADC_IN1
ML3 thermistor ── PB4-switched divider ──────── PA2 / ADC_IN2
ML3 power      ── LSN50v2 switched +5 V (PB5 control; reset/ISP/brownout state pending Gate 0)
+5 V monitor   ── precision divider ─────────── PA4 / ADC_IN4
Internal       ── VREFINT (ADC_IN17), temperature sensor (ADC_IN18)
LoRaWAN        ── existing LSN50v2 radio, OSI network, new dedicated FPort
```

The STM32L072 ADC is single-ended. The firmware estimates the ML3 differential voltage as `V_ML3 = V_HI − V_LO` from two sequentially sampled channels, using hardware oversampling, VREFINT compensation, matched filtering, ABBA drift cancellation, and per-node calibration.

## 1.2 Conditional approval

The direct-ADC design is approved **conditionally**, in two stages:

- **Gate 0 (Phase 0, before reachable ML3 acquisition):** the bench characterization in Part II must confirm the ML3 signal envelope — including minimum low-rail headroom and compliant supply at the probe — is compatible with single-ended acquisition, and the effort comparison against the fallback must come out in the direct design's favor. Before this gate, only build-only target configuration and unregistered adapter-object compilation are permitted while all Gate 0/Phase 2 hardware readiness macros remain zero, and `ML3_CONFIG_ACQUISITION_READY` and `ML3_CONFIG_DEPLOYABLE` remain false. That path must not make ADC access, PB5 control, thermistor excitation, EEPROM write, LoRa queue, sample processing, or physical sampling reachable. Gate 0 remains required before any reachable ML3 acquisition, PB5-powered ML3 test, calibration write, or deployment.
- **Gate 1 (Phase 2, before deployment):** one prototype must demonstrate full-chain performance, **evaluated on held-out points not used to fit the calibration** (§4.2), with calibrator/reference uncertainty documented and guard-banded (§4.2):

| Criterion | Required result |
|---|---:|
| Mean differential bias after calibration (held-out points) | ≤ 1.0 mV |
| Maximum residual over −20 mV…1.05 V and the validated common-mode envelope | ≤ 2.0 mV |
| Repeatability SD at constant input | ≤ 0.5 mV |
| Common-mode-dependent residual | ≤ 1.0 mV |
| Temperature residual after correction, over validated range | ≤ 2.0 mV |
| Fresh/depleted battery difference | ≤ 1.0 mV |
| Radio-sequence-induced difference | ≤ 0.5 mV |
| Warm-up drift during acquisition | ≤ 1.0 mV preferred; ≤ 2.0 mV max |
| Soil temperature vs reference, 0–40 °C, ≥ 5 points | ≤ 1.0 °C |
| Supply at ML3 terminals, worst case | within ML3 specification (≥ 5.0 V) |
| Valid acquisition rate | ≥ 99.5 % |
| 10,000-cycle soak | numeric criteria of §4.4 all pass |
| Reproducible build | binary reproduced, SHA-256 matched |
| Decoder validation | all test vectors pass |
| Airtime | routine + every diagnostic frame ≤ region/DR maximum (build gate, §3.13) |

**Fallback:** an external differential ADC on a small adapter board. §2.5 requires selecting and validating a **concrete circuit** — candidate TI ADS1115 (I²C, differential pair + single-ended channels, internal reference), noting honestly: its inputs must also stay within GND…VDD (a negative absolute input defeats it too without biasing), its internal reference has its own gain drift, and retaining HI/LO diagnostics costs extra single-ended conversions. If Gate 0 fails on negative absolute inputs, the fallback must include an input-bias front end or a different topology. No hard constraint against the fallback exists; the direct design is preferred, not mandated.

## 1.3 Scope

**Included (release 1):** direct ML3 differential acquisition on PA0/PA1; switched +5 V control and monitoring; ML3 thermistor (soil temperature) readout in the unpowered state; die-temperature acquisition; custom ADC configuration with hardware oversampling and VREFINT compensation; ABBA acquisition; per-node calibration with EEPROM storage; quality state, flags, and sentinels; versioned LoRaWAN payload on a dedicated FPort; serial configuration/diagnostic commands; bench qualification; OSI edge ingest, schema, and sync integration; edge-authoritative VWC; field pilot with acquisition and agronomic validation tracks.

**Excluded (deferred):** remote calibration writes over LoRaWAN; any claim that the LSN50v2 becomes a true differential ADC; deployment before Phase 0 and Phase 2 gates pass; presenting electrical accuracy as parcel-moisture accuracy (§5.6).

## 1.4 Governing sensor and platform facts

**ML3 (Delta-T manual v2.1):** differential analog DC output, 0–1.0 V nominal for moisture (air functional check 0 ± 4 mV — slightly negative differentials are legitimate); supply **5–14 VDC (5.0 V is the minimum, not a nominal with margin)**, ~18 mA active; recommended warm-up 0.5–1.0 s; continuous powering not recommended; thermistor on the grey conductor shares the brown power-return — Delta-T requires cable-current correction if read while powered, and endorses direct use of the published characteristic when moisture and temperature are measured sequentially with sensor power off (manual Appendix 2). The ML3 shall be powered only during moisture acquisition.

**Expected signal envelope (to be confirmed at Gate 0):** with brown tied to node GND, Signal LO sits just above node ground (return-current IR drop, expected 0–50 mV); HI = LO + differential, so both inputs may sit near 0 V in dry soil. **Near 0 V is normal operation — but the ADC cannot see below 0 V**, so Gate 0 must establish that both inputs keep a measured minimum headroom above the ADC's zero-ambiguity band under all conditions (§2.2/§2.3). Accuracy near the bottom rail is characterized and calibrated, not fenced off; *ambiguity* at the bottom rail is a hard fault (§3.11).

**LSN50v2:** STM32L072CZT6 + SX1276/78. PA0/PA1/PA4 ADC-capable; switched +5 V controlled by PB5 (documented active-low — verify polarity **and the pin's reset/ISP/brownout state with the board's actual pull arrangement**, D1); board revision 2.1 adds per-I/O ESD protection. ADC range is bounded by actual VDDA (measured via VREFINT), not the nominal 3.3 V label. The stock firmware's battery measurement *is* the VREFINT-derived VDDA (`HW_GetBatteryLevel`, `stm32l0xx_hw.c`) — the MCU rail is the battery rail; the payload therefore carries one VDDA/battery field, measured under acquisition load.

---

# Part II — Phase 0: characterization gate (Gate 0)

Before this phase completes, firmware work is limited to build-only target configuration and unregistered adapter-object compilation while all Gate 0/Phase 2 hardware readiness macros remain zero, and `ML3_CONFIG_ACQUISITION_READY` and `ML3_CONFIG_DEPLOYABLE` remain false. That work must not make ADC access, PB5 control, thermistor excitation, EEPROM write, LoRa queue, sample processing, or physical sampling reachable. Deliverable: a short written go/no-go decision with the measurements attached.

## 2.1 Baseline capture (per node)

Record: (1) LSN50v2 PCB hardware revision; (2) STM32 device marking/silicon revision; (3) existing firmware version and commit if previously flashed; (4) Dragino `LoRa_STM32` upstream commit chosen as baseline; (5) LoRaWAN region, data rate, and channel configuration (pins the §3.13 airtime budget); (6) availability of PA0, PA1, PA2 / ADC_IN2, PA4, and PB4 in the actual wiring; (7) ML3 serial number and cable length **and measured cable loop resistance**; (8) PB5 behavior during reset, ISP/bootloader entry, and brownout with the board's pull arrangement — the +5 V rail must be provably off in all of them (add/verify an external pull if not).

## 2.2 ML3 absolute-voltage, common-mode, and supply measurement

For each ML3, measure `V_HI−GND`, `V_LO−GND`, `V_HI−LO` with a high-impedance meter or oscilloscope under: unpowered; initial power-up transient; air; dry soil; intermediate moisture; wet soil; the manufacturer water functional check; power-down transient. Repeat the standing-value points at fresh and depleted battery and at temperature extremes where feasible.

Simultaneously measure **supply voltage at the ML3 terminals** (not the node connector) under: depleted battery, minimum expected temperature, maximum cable length, startup inrush with the fitted bulk capacitance.

**Fail conditions (any probe):** either signal goes negative relative to node ground under any condition; either signal exceeds measured VDDA; startup/shutdown transients exceed STM32 injection limits after the RC network; **supply at the probe terminals falls below the ML3's 5.0 V minimum under worst case**; **minimum observed LO or HI does not clear the §2.3 zero-ambiguity guard**. Any fail → level-shift/front-end change or fallback decision at Gate 0.

**Outputs:** the common-mode envelope `[LO_min, LO_max]` (→ `CM_RANGE` flag and calibration sweep range); the minimum observed headroom for each input (→ the `LOW_RAIL_CLIPPED` guard, §3.11); the supply-compliance record.

## 2.3 Near-rail ADC characterization and the zero-ambiguity guard

On **all four boards** (near-rail behavior is a per-unit property, and there are only four): with the Part III RC network and a bench source, measure oversampled VREFINT-compensated ADC error at `{0, 2, 5, 10, 20, 50, 100} mV` and `{VDDA−100, VDDA−20} mV` against a traceable meter.

Two distinct outputs:

1. **Zero-ambiguity guard `V_guard`:** the smallest input at which the board's reading is reliably distinguishable from a clipped-at-zero reading (ADC offset + noise + margin, from this data). Both ML3 inputs must exceed `V_guard` under all §2.2 conditions — this is the evidence-derived replacement for the draft's arbitrary 20 mV, and it cannot be zero.
2. **Near-rail accuracy:** residual above `V_guard` correctable to ≤ 1.0 mV by the calibration model (linear or piecewise). Uncorrectable → fallback.

## 2.4 Warm-up settling (coarse)

Oscilloscope observation of ML3 output settling versus time after power-up, per probe, room temperature — confirms the 0.5–1.0 s manufacturer figure and that 3000 ms bounds the configuration range. Definitive firmware-driven sweep in Phase 2 (§4.2).

## 2.5 Effort comparison and concrete fallback selection

Before Gate 0 sign-off, write down (one page): remaining direct-ADC cost (Parts III–IV) versus the fallback. The fallback side must name a **specific validated circuit** — candidate ADS1115: exact channel usage (differential P0–P1 for the pair; single-ended reads if HI/LO retention is wanted), input-range compliance against the measured §2.2 envelope (its inputs are also GND-referenced — negative absolute inputs need biasing), reference-drift expectations, and adapter-board effort. Include parts availability and four-node replication cost.

## 2.6 Gate 0 decision

Proceed with the direct design only if **all** hold: §2.2 passes for all four probes (including supply compliance and headroom); §2.3 passes on all four boards; §2.5 does not show the fallback strictly dominating. Otherwise switch to the selected fallback circuit (same Part V integration, same payload contract, simpler firmware). The payload and OSI integration are architecture-neutral so a Gate 0 flip does not waste them.

---

# Part III — Phase 1: analog interface and firmware

## 3.1 Wiring

| ML3 conductor | Function | LSN50v2 connection |
|---|---|---|
| White | Power V+ | Switched +5 V |
| Brown | Power return | GND |
| Blue | Signal HI | PA0 via matched RC network |
| Black | Signal LO | PA1 via matched RC network |
| Green | Shield | Node GND (battery negative) at node end only |
| Grey | Thermistor | PA2 / ADC_IN2 via §3.3 divider; build-only until its readiness gate is set |

Signal LO shall **not** be tied to ground.

## 3.2 RC network (precision differential path)

```text
ML3 Signal HI ── 1.00 kΩ ──┬── PA0        ML3 Signal LO ── 1.00 kΩ ──┬── PA1
                           │                                          │
                         100 nF                                     100 nF
                           │                                          │
                          GND                                        GND
```

| Component | Requirement |
|---|---|
| Series resistors | 1.00 kΩ, 0.1 %, ≤ 25 ppm/°C, same part family |
| Shunt capacitors | 100 nF, X7R or better, matched part family |
| Placement / routing | inside enclosure, at the ADC pins; HI/LO as a matched pair, away from antenna and boost traces |
| Pulls | none; GPIO analog mode, no pull |

Cutoff ≈ 1.59 kHz, τ = 100 µs. Unpopulated 1–10 nF C0G differential-cap footprint; populate only on §4.3 evidence.

**Sampling-charge budget:** at 160.5-cycle sampling and ×256 oversampling the ADC draws ~0.3–0.5 µA average from the sampled node; through 1.00 kΩ that is ~0.3–0.5 mV of droop, appearing almost entirely as gain error absorbed by calibration. Confirmed, not assumed, in §2.3/§4.2. Nobody resizes these resistors without redoing this math.

**Protection:** revision 2.1 has per-I/O ESD. Any additional protection symmetrical and low-leakage (mismatch ≪ 0.5 µA over temperature). No uncharacterized TVS on the precision inputs.

## 3.3 Thermistor interface (read with ML3 power OFF)

```text
VDD ── PB4 (push-pull, high only during read) ── R_ref 10.0 kΩ 0.1% ──┬── PA2 / ADC_IN2
                                                                        │
                                                            ML3 grey (thermistor)
                                                                        │
                                                            ML3 brown / GND
                                                        ADC pin ── 100 nF ── GND
```

- **Sequencing (load-bearing):** the thermistor shares the brown return conductor with the ML3's 18 mA supply current; read while powered, cable-resistance IR drop corrupts the measurement and Delta-T's published characteristic no longer applies directly (manual Appendix 2). The read therefore happens **after** +5 V is disabled and the rail is **verified discharged** via PA4 (below a threshold, with timeout → read skipped, `THERM_FAULT` set, soil temp = sentinel).
- PA2 / ADC_IN2 and PB4 are the selected build-only identity. The fitted-circuit settle time, PA4 rail-discharge guard, and manual lookup table remain pending Gate 0 and qualification; no numeric configuration value is authorized for them yet.
- PB4 high only during the read; after the fitted-circuit settle time, convert, then drive PB4 low. The reading is ratiometric (`ratio = code_OS / 65520`), so VDDA cancels; do not apply VREFINT compensation before `ml3_thermistor_convert`.
- **Conversion via the ML3 manual's thermistor lookup table with interpolation — this is not a generic 10K3A1B curve.** The table requires an independent transcription check before activation. Authoritative coefficients from Delta-T may replace it if supplied.
- Fault detection: junction pinned near either rail during excitation → `THERM_FAULT`; a thermistor fault never invalidates the moisture reading.
- Excitation GPIO verified electrically quiet during the ABBA burst (§4.3).

## 3.4 +5 V monitor

47.0 kΩ / 47.0 kΩ (0.1 %, low-tempco) divider from switched +5 V to PA4, 100 nF at the pin. **Checked constraint (Phase 1):** worst-case `V5_max / 2` must stay below worst-case `VDDA_min` minus margin — at depleted battery VDDA approaches the boost tolerance band and a 2.5 V+ tap can clip. If violated, re-ratio (e.g. 68k/33k → 5 V → 1.63 V) and record the ratio; **the measured per-node ratio is stored in the calibration record** (Appendix B). Local decoupling at the ML3 cable termination: 100 nF + 4.7–10 µF bulk (verify boost startup with bulk fitted). The divider also provides the §3.3 discharge check.

## 3.5 Source control, toolchain, modules

- Fork of Dragino `LoRa_STM32`: `vendor/dragino-baseline`, `feature/ml3-precision-adc`, `release/ml3-v1.x`. Archive baseline commit, compiler, linker script, stack, regional config.
- OSI firmware conventions adopted from the OSI Chameleon LSN50 firmware (standalone repo): payload version byte, status-flag style, reproducible builds (recorded toolchain, archived map, SHA-256; release valid only when a second developer reproduces the hash).
- No HAL migration during first implementation.
- Modules: `ml3_measurement`, `adc_precision`, `ml3_calibration`, `ml3_quality`, `ml3_payload`, `ml3_thermistor`, `ml3_at_commands` (.c/.h each); `bsp.c` calls the measurement service.

## 3.6 Operating mode and FPort

`#define MODE_ML3 10U` (provisional, D2). The mode disables all stock ADC sampling and stock conversions (`batteryLevel_mV / 4095` paths unreachable) and transmits on a dedicated FPort — proposed **FPort 12** (no collision with FPort 2 sensor frames / FPort 5 config; fleet shares one decoder; verify against stock Dragino usage, D2). Note the edge does not merely need a codec branch — see §5.1 for the ingest-path consequences.

## 3.7 ADC configuration

| Parameter | Value |
|---|---|
| ADC / resolution | ADC1, 12-bit native |
| External channels | PA0 = IN0 (HI), PA1 = IN1 (LO), PA2 = IN2 (thermistor, build-only until its readiness gate is set), PA4 = IN4 (+5 V mon); PB4 is the thermistor excitation GPIO |
| Internal channels | **VREFINT = IN17; temperature sensor = IN18** |
| ADC clock | target 8 MHz (PCLK/4 at 32 MHz; recompute and document if PCLK differs) |
| Sampling time | 160.5 cycles (meets VREFINT/TS minimum sampling requirements) |
| Hardware oversampling | ×256, right shift 4 → `OS_SCALE = 16`, full scale 65 520 |
| Trigger | single software trigger; continuous off; DMA off (release 1) |
| Self-calibration | once per acquisition cycle, ADC disabled, before retained measurements |
| Internal-reference enables | ADC_CCR `VREFEN`/`TSEN` **and** SYSCFG_CFGR3 buffer enables (`ENBUF_VREFINT_ADC`, `ENBUF_SENSOR_ADC`) with their readiness flags (`VREFINT_RDYF` et al.); wait both reference startup and buffer settling before the first conversion. Exact bit names verified against RM0376 at implementation. |
| Die temperature | IN18 converted once per acquisition (after the post-reference), scaled via factory `TSENSE_CAL1/2` |
| Radio / GPIO | radio in sleep, thermistor excitation low, no other activity during the burst |

≈16-bit resolution after oversampling is **not** 16-bit absolute accuracy — offset, gain, INL, channel mismatch, and reference error remain; calibration and the release criteria bound them.

**Errata discipline (ES0292):** never write `ADC_CFGR1/2` with `ADEN` set — stop → disable → wait `ADEN` clear → configure → calibrate → enable → convert. Poll single conversions, read immediately (OVR case). Explicit timeouts on disable/ready/calibrate/convert.

**Channel switching:** one external channel at a time; discard one full oversampled conversion after every channel change; repeat reads on the same channel need no new discard.

**VREFINT conversion (64-bit integers, µV end-to-end):**

```text
VDDA[µV]      = 3,000,000 × VREFINT_CAL × 16 / VREFINT_OS      (cal word @ 0x1FF80078, 3.0 V / 25 °C)
V_channel[µV] = C_OS × VDDA[µV] / 65,520
```

Never round HI or LO to whole mV before subtraction.

## 3.8 Acquisition state machine

```c
typedef enum {
    ML3_STATE_IDLE, ML3_STATE_PREPARE, ML3_STATE_POWER_ON, ML3_STATE_WARMUP,
    ML3_STATE_ADC_CONFIGURE, ML3_STATE_ADC_CALIBRATE, ML3_STATE_REFERENCE_PRE,
    ML3_STATE_SAMPLE_ABBA, ML3_STATE_REFERENCE_POST, ML3_STATE_POWER_OFF,
    ML3_STATE_VERIFY_DISCHARGE, ML3_STATE_SAMPLE_THERMISTOR,
    ML3_STATE_PROCESS, ML3_STATE_BUILD_PAYLOAD, ML3_STATE_QUEUE_TX, ML3_STATE_ERROR
} ml3_state_t;
```

Nominal sequence: wake → record reset cause, increment sequence → radio to sleep → analog pins configured → +5 V on → warm-up (timer-driven, default 1000 ms, range 500–3000 ms, final value from §4.2) → ADC configure + self-calibrate (internal buffers enabled and ready per §3.7) → VREFINT + +5 V monitor (pre) → **ABBA burst** → VREFINT + +5 V monitor (post) → **die temperature (IN18)** → **+5 V off** → **PA4 confirms rail discharged (timeout → skip thermistor, flag)** → thermistor excitation → settle → read → excitation off → process, calibrate, quality state → payload → queue uplink → sleep.

**Every error path and the watchdog-reset default disable +5 V and the thermistor excitation GPIO.** PB5's *hardware* reset state must already be fail-safe (§2.1 item 8) — firmware discipline is the second layer, not the only one. Watchdog refresh: before +5 V on, before/after the ABBA burst, before TX. All blocking calls time out. Transmission never overlaps analog acquisition.

## 3.9 ABBA acquisition and statistics

One cycle `H1 → L1 → L2 → H2`; `H = (H1+H2)/2`, `L = (L1+L2)/2`, `D = H − L`. Default 4 cycles (2–8 configurable); reading INVALID if fewer than 3 of 4 valid. Per reading: mean HI, mean LO, common mode (mean LO), mean/**median**/SD/MAD of cycle differentials, first-to-last drift, min/max, **valid-cycle count (transmitted, Appendix A)**. Median is the reported value; the rest ride diagnostics. Invalid readings are transmitted with flags and sentinels — never suppressed, never substituted.

## 3.10 Calibration model and storage

```text
V_corr = (b0 + b0T·ΔT) + (b1 + b1T·ΔT) · V_raw + bCM · (CM − CM_ref)

ΔT = T_die − T_ref
```

- Offset **and gain** both carry temperature terms because the dominant temperature mechanism — VREFINT drift, up to 100 ppm/°C ⇒ ~4 mV at 1 V over 40 °C — is **multiplicative**. An additive-only term fitted near 1 V would misbehave near 0 V. `bCM` on evidence from the common-mode sweep; terms are dropped (set to 0) when the data shows them insignificant, not added to over-fit.
- If linear residuals exceed limits, a monotonic piecewise-linear correction table (also covering §2.3 near-rail correction) is stored in the record — the record format carries it (Appendix B), it is not a promise without a home.
- Storage: STM32 data EEPROM, two redundant slots, Appendix B record (now including divider ratio, both tempcos, `CM_ref`, optional piecewise block; `calibration_id` is **u8**, matching the payload). Boot: validate both, newest valid wins, never overwrite the last valid slot before the replacement verifies. Writes via local maintenance only. `CAL_INVALID` → corrected-differential field carries the sentinel (uncalibrated data never appears in a field named "corrected"); raw HI/LO remain available.

## 3.11 Quality model

Three layers, all transmitted (Appendix A/C):

1. **Flags (u16):** individual conditions, Appendix C.
2. **Quality state (2 bits):** `VALID` / `DEGRADED` (any warning-level condition) / `INVALID` (any flag in the **invalidating mask** — Appendix C column — or valid cycles < 3). The decoder never has to re-derive validity from flag semantics.
3. **Valid-cycle count (4 bits):** 0–8.

Key semantics:

- **`LOW_RAIL_CLIPPED` (invalidating):** either input's oversampled reading falls at/below the board's Gate 0 zero-ambiguity guard `V_guard` for ≥ half the burst. Code 0 cannot distinguish "true 0 V" from "clipped negative"; calibration cannot recover a clipped signal; such readings are ambiguous and therefore INVALID. (Replaces v2.0's `LO_NEG`, which wrongly treated repeated zero as distinguishable from valid near-zero.)
- `HI_OVER` (invalidating): HI within 100 mV of VDDA — wiring/fault territory.
- `CM_RANGE` (warning): mean LO outside the Phase 0 envelope + margin.
- `THERM_FAULT` (warning): thermistor rail-pinned or rail-not-discharged; soil temp = sentinel; moisture unaffected.
- Sentinels: every payload field has one (Appendix A); an ADC timeout produces sentinels + `ADC_TIMEOUT` + INVALID, never a plausible zero.

## 3.12 Power

15-min interval × ~1.1–1.3 s powered window → low duty cycle; energy budget verified under fresh/depleted battery, minimum temperature, max TX power, poor link, final cable length (§4.4 records battery behavior). PB5 defaults to +5 V-off in hardware and firmware; ML3 never powered continuously.

## 3.13 Payload

Dedicated FPort (§3.6), versioned. **Routine payload: 25 bytes, big-endian** — Appendix A. HI and LO are **VREFINT-compensated, uncalibrated** values (they feed reprocessing and diagnostics); the corrected differential is the calibrated product or sentinel. One field carries VDDA≡battery (measured under acquisition load; §1.4) — the freed bytes fund the quality byte.

**Diagnostic payload:** byte-mapped (Appendix A.2), multi-frame with part-index/count, **every frame ≤ the region/DR maximum**. Sent on `AT+ML3TEST`, on command, and automatically on first occurrence of an invalidating condition — **rate-limited to one automatic diagnostic per fault signature per 6 h** (a persistently faulted sensor must not emit large uplinks every 15 min indefinitely; routine frames continue and carry the flags).

**Airtime build gate:** CI asserts routine and each diagnostic frame length against the deployed region + data-rate maximum from LoRaWAN RP002 (region/DR pinned in §2.1 item 5). Not an open decision — a failing assertion fails the build.

## 3.14 Serial commands

`AT+ML3?` · `AT+ML3WARM=<500–3000>` · `AT+ML3CYCLES=<2–8>` · `AT+ML3RAW=0|1` · `AT+ML3TEST` · `AT+ML3CAL?` / `AT+ML3CAL=<record>` / `AT+ML3CALCLR` · `AT+ML3VER?`. Calibration writes only via this local interface in release 1.

---

# Part IV — Phase 2: calibration and qualification (Gate 1)

## 4.1 Software tests (host-side)

Fixed-point VREFINT formula + overflow boundaries; calibration model incl. tempco gain/offset and piecewise table; median/MAD; thermistor table interpolation; quality-state derivation from flags + invalidating mask; sentinel handling for every field; payload pack/unpack vectors (shared with the edge decoder, §5.1) incl. sentinels, negative differentials, multi-frame diagnostics; **airtime assertions per §3.13**; CRC32 + redundant-slot selection; sequence rollover. <!-- slop-allow: pack/unpack is the conventional payload-vector terminology -->

## 4.2 Bench calibration (per node; automated rig mandatory)

- **Differential sweep:** {−20, −10, 0, 50, 100, 250, 500, 750, 900, 1000, 1050} mV at nominal common mode, at 3 temperatures (low field / room / high field). Negative points are in-range (air check 0 ± 4 mV; payload accepts −20 mV).
- **Common-mode sweep:** at {100, 500, 1000} mV differential, LO stepped across the Phase 0 envelope — **at room temperature and both temperature extremes, and at fresh + depleted supply at room temperature** (the envelope interacts with temperature and supply; the full matrix is still trimmed relative to the draft).
- **Battery:** spot check {100, 500, 1000} mV fresh vs depleted (verifies the VREFINT-compensation claim rather than re-characterizing it).
- **Fit/holdout split:** calibration coefficients fitted on a designated fit subset; **Gate 1 criteria evaluated exclusively on held-out points** (interleaved voltages not used in the fit).
- **Metrology:** the calibrator/reference-meter uncertainty is stated in the report and must be ≤ 0.25 mV (a quarter of the tightest acceptance limit); acceptance is judged with that guard band.
- ≥ 50 acquisitions per point via scripted `AT+ML3TEST`; record raw HI/LO, VREFINT, VDDA, +5 V, die temp.
- **Warm-up sweep (definitive):** {100, 250, 500, 750, 900, 1000, 1200, 1500, 2000} ms, all four probes, 3 temperatures; select where mean settled, drift ≤ 1 mV preferred / 2 mV max on every node, excitation stable.
- **Thermistor:** ≥ 5 bath points spanning 0–40 °C against a reference thermometer; ≤ 1.0 °C everywhere (two points cannot demonstrate a maximum error over the range).
- **Near-rail characterization on all four boards** (already required at Gate 0, §2.3; re-verified here with final firmware).

≈ 70 points × 50 acquisitions ≈ 3 500 automated acquisitions per node — the scripted rig is a Phase 2 deliverable.

## 4.3 Interference tests

(1) ML3 only, radio silent; (2) + passive interface; (3) LoRa TX immediately after acquisition; (4) max TX power; (5) poor-link/retry; (6) antenna close to vs separated from the sensor cable; (7) fresh/depleted/cold battery; (8) thermistor excitation verified quiet during ABBA; (9) RC configuration comparison {none / RC / RC + diff cap} → select per §3.2.

## 4.4 Accelerated cycling — numeric acceptance

≥ 10 000 cycles (wake → power → acquire → discharge-verify → thermistor → transmit or simulated → sleep). **Acceptance criteria (all must hold):**

| Metric | Limit |
|---|---:|
| Valid acquisition rate | ≥ 99.5 % |
| Stuck-+5 V events (rail on outside acquisition window) | 0 |
| EEPROM/calibration CRC failures | 0 |
| Unrecovered lockups (node absent > 2 intervals) | 0 |
| Watchdog-recovered resets | ≤ 0.1 % of cycles, each with cause logged |
| Sequence-number gaps (excluding radio loss in simulated-TX mode) | ≤ 0.1 % |
| Reported-value drift at constant input, first vs last 500 cycles | ≤ 0.5 mV |

## 4.5 Gate 1

The §1.2 table, on held-out data, with metrology guard bands, on one prototype. Only then is the design replicated to the remaining three nodes (each still gets §4.2 calibration). Rollback firmware and recovery instructions archived with the release.

---

# Part V — Phase 3: OSI integration and field pilot (Gate 2)

osi-os work; runs its own spec/plan cycle per **osi-schema-change-control** for the schema slice. Architecture-neutral (identical for direct-ADC and fallback). The requirements below are binding on that cycle.

## 5.1 Edge ingest path (not just a codec)

The current pipeline raw-decodes **every** LSN50 packet as a stock frame before mapping fields: `lsn50-decode-fn` (flows.json) calls `dendro.decodeRawAdcPayload(data.data)` unconditionally, `osi-dendro-helper` validates modes only in 1–9 (`lsn50ModeLabel`, `detectLsn50ModeCode`), and `lsn50-sql-fn` builds a fixed column mapping. An FPort-12 ML3 frame would have its payload bytes misread as stock mode bits. Required work items:

1. **ChirpStack codec:** new fPort-12 branch in `dragino_lsn50_decoder.js` (+ bcm2709 mirror): reject unknown protocol versions; signed fields and sentinels decoded correctly; flags exposed as named `ML3_…` booleans; quality state + valid cycles exposed; raw payload preserved. Test vectors generated by the same tool as §4.1 (shared ground truth).
2. **`lsn50-decode-fn`:** FPort-aware routing — ML3 frames bypass `decodeRawAdcPayload` and the stock mode inference entirely; mode-10/FPort-12 metadata recorded.
3. **`osi-dendro-helper`:** mode validators (currently capped at 9) must not misclassify or silently null the new mode.
4. **Apply Config / device metadata:** ML3 device role recognized; no dendrometer conversion path applied.
5. **`lsn50-sql-fn` and inserts:** raw insert to `ml3_readings` (always, flagged or not) and canonical insert to `device_data` — **NULL canonical voltage/VWC/soil-temp for INVALID quality** (raw diagnostic visibility and canonical agronomic validity are separate concerns).
6. **Verifiers/parity:** migration verifiers, schema fingerprints, ChirpStack device-profile parity, sync triggers.

## 5.2 Edge schema

- **`ml3_readings`** (append-only, edge-local; `chameleon_readings` precedent): deveui, recorded_at, f_cnt, protocol_version, status_flags, quality_state, valid_cycles, sequence, corrected differential (or NULL when sentinel), HI, LO, VDDA/battery, +5 V, noise, die temp, soil temp, calibration ID, raw payload hex. Retention per D7.
- **`device_data`:** canonical calibrated voltage column (naming in the schema cycle), **`soil_temperature_c` (new canonical channel — D5 resolved: `ext_temperature_c` is the DS18B20 "External temperature" environmental channel in `channels.json`/`farming.ts` and is not reused)**, and `vwc` (§5.5). NULL for INVALID acquisitions.
- All changes via ordered migrations and the verifier chain.

## 5.3 Sync

Canonical values ride the existing `DEVICE_DATA_APPENDED` contract (contract addition for the new columns). `ml3_readings` stays edge-local; revisit only if cloud analytics needs raw cycles.

## 5.4 GUI

ML3 device card in the Dragino family: VWC (once §5.5 lands) + calibrated voltage, soil temperature, battery (existing `bat_v` fallback), and a visible degraded/invalid state — flagged data shown as flagged, never rendered as a normal-looking number. History channels via the manifest.

## 5.5 VWC — edge-authoritative, percent, reprocessable

- **Ownership (decided):** the edge computes and stores canonical `device_data.vwc` and syncs it; the cloud mirrors. This matches OSI's edge-authoritative model — "cloud/edge-central" ambiguity removed.
- **Conversion:** ML3 manual voltage→√ε polynomial, then soil-specific coefficients (generic mineral/organic defaults; per-parcel overrides from §5.6 soil work) in a **versioned installation/soil calibration table** on the edge.
- **Units (decided):** the ML3 formula yields a m³/m³ **fraction**; canonical `vwc` is **percent 0–100** (`channels.json` unit `%`; server thresholds validate 1–100). Multiply by 100 **exactly once**, at this conversion boundary.
- **Reprocessing:** coefficient changes are data changes — trigger recomputation of affected historical rows **and enqueue corrected sync events** (the sync trigger fires on insert, not historical update — the Chameleon-repair lesson).

## 5.6 Field pilot (Gate 2) — two tracks

**Electrical accuracy is not parcel soil-moisture accuracy.** A ±2 mV voltage chain does not license "±2 mV-equivalent" moisture claims: soil-specific calibration, salinity (up to ~3.5 %-points), density and installation effects dominate, and each probe samples ~75 cm³.

**Gate 2a — acquisition validation:** one node, several days, alongside a reference logger reading the **same ML3** (D6): raw-voltage bias ≤ 2 mV vs reference, packet completeness ≥ 99.5 %, sequence gaps and flag rates within §4.4 limits, plausible diurnal response. This validates the voltage chain only.

**Gate 2b — agronomic validation (before VWC numbers are presented as parcel moisture):**
- **Installation record per probe:** depth/horizon, orientation, full burial and soil contact, distance from irrigation emitters.
- **Soil characterization per parcel:** texture (clay vs non-clay selects the Delta-T calibration procedure), bulk density, EC/salinity, calibration provenance.
- **Gravimetric soil samples across a wetting/drying cycle**, compared against computed VWC; acceptance bounds stated **before** sampling.
- **Replication statement:** either replicated probes per parcel, or an explicit written statement that each value is a point measurement, not a parcel mean.

Fleet rollout only after Gate 2a passes and Gate 2b is either passed or its limitations are documented and accepted in writing.

---

# Appendix A — Payloads

## A.1 Routine payload (25 bytes, big-endian, FPort proposed 12)

| Bytes | Field | Encoding | Unavailable sentinel |
|---:|---|---|---|
| 0 | Protocol version | u8 (= 1) | — |
| 1 | Payload type | u8 (0 = routine) | — |
| 2–3 | Status flags | u16 (Appendix C) | — |
| 4–5 | Acquisition sequence | u16, wraps | — |
| 6–7 | Corrected ML3 differential | i16, 0.1 mV | `0x7FFF` (also when `CAL_INVALID` or INVALID) |
| 8–9 | Mean Signal HI (**VREF-compensated, uncalibrated**) | u16, 0.1 mV | `0xFFFF` |
| 10–11 | Mean Signal LO (**VREF-compensated, uncalibrated**) | u16, 0.1 mV | `0xFFFF` |
| 12–13 | VDDA ≡ battery (under acquisition load) | u16, mV | `0xFFFF` |
| 14–15 | Switched +5 V (at divider, ratio-corrected) | u16, mV | `0xFFFF` |
| 16–17 | Differential noise (SD of cycles) | u16, µV | `0xFFFF` |
| 18–19 | MCU die temperature | i16, 0.01 °C | `0x7FFF` |
| 20–21 | Soil temperature (ML3 thermistor) | i16, 0.01 °C | `0x7FFF` |
| 22 | Quality | u8: bits 7–6 state (0 VALID, 1 DEGRADED, 2 INVALID), bits 5–4 reserved, bits 3–0 valid ABBA cycles | — |
| 23 | Calibration ID | u8 | 0 = none |
| 24 | Reserved | u8 (= 0) | — |

## A.2 Diagnostic payload (multi-frame)

Type byte = 1; byte 2 = part index (high nibble) / part count (low nibble). Frame 1: header — sequence, flags, quality, reset cause, warm-up setting, hardware rev, firmware build ID (u16), calibration schema+ID, VREFINT pre/post raw codes, PA4 pre/post raw codes, thermistor raw ratio. Frames 2..n: per-cycle raw codes (`H1,L1,L2,H2` × u16 = 8 bytes/cycle, ≤ 4 cycles per frame). **Every frame ≤ the region/DR maximum (build-gated).** Emission: on command; automatically on first occurrence of an invalidating condition, rate-limited to one per fault signature per 6 h.

# Appendix B — Calibration EEPROM record (schema v2)

```c
typedef struct {
    uint32_t magic;               uint16_t schema_version;  uint16_t record_length;
    int32_t  offset_uV;           /* b0  */
    int32_t  offset_tempco_uV_per_C;      /* b0T */
    int32_t  gain_ppm;            /* b1 deviation from unity, ppm */
    int32_t  gain_tempco_ppm_per_C;       /* b1T */
    int32_t  common_mode_ppm;     /* bCM */
    int32_t  cm_ref_uV;           /* common-mode reference */
    int32_t  v5_divider_ppm;      /* measured PA4 divider ratio, deviation from nominal, ppm */
    int16_t  reference_temp_centiC;
    uint8_t  calibration_id;      /* u8 end-to-end, matches payload byte 23 */
    uint8_t  piecewise_count;     /* 0 = no table */
    /* piecewise_count × { int32_t in_uV; int32_t correction_uV; } follows */
    uint32_t device_id_hash;      uint32_t sequence;        uint32_t crc32;
} ml3_calibration_t;
```

Two redundant slots; validate magic/schema/length/device hash/CRC at boot; newest valid wins; never overwrite the last valid slot before the replacement verifies.

# Appendix C — Quality flags

| Bit | Flag | Invalidating? | Meaning |
|---:|---|:--:|---|
| 0 | `ADC_INIT` | ✔ | ADC initialization failed |
| 1 | `ADC_CAL` | ✔ | ADC self-calibration failed |
| 2 | `ADC_TIMEOUT` | ✔ | conversion timeout |
| 3 | `ADC_OVERRUN` | ✔ | ADC data-handling fault |
| 4 | `HI_OVER` | ✔ | HI within 100 mV of VDDA (fault) |
| 5 | `LOW_RAIL_CLIPPED` | ✔ | either input at/below the board's zero-ambiguity guard `V_guard` ≥ half the burst — reading is ambiguous (true zero vs clipped negative are indistinguishable at code 0) |
| 6 | `DIFF_RANGE` | ✔ | differential outside −20…+1100 mV |
| 7 | `CM_RANGE` | — | mean LO outside Phase 0 envelope + margin |
| 8 | `VREF_DRIFT` | — | pre/post VDDA change > warn (invalid > 0.5 %) |
| 9 | `V5_LOW` | ✔ | excitation below validated limit at ML3 terminals |
| 10 | `V5_HIGH` | — | excitation above validated limit |
| 11 | `NOISE_HIGH` | — | dispersion > warn (invalid > 1.0 mV → INVALID) |
| 12 | `WARMUP_DRIFT` | — | first-to-last drift > warn (invalid > 2.0 mV → INVALID) |
| 13 | `CAL_INVALID` | — | calibration record invalid (corrected field → sentinel; raw HI/LO still valid) |
| 14 | `TEMP_RANGE` | — | die temp outside calibrated range |
| 15 | `THERM_FAULT` | — | thermistor open/short/rail-not-discharged (soil temp → sentinel) |

Quality state = INVALID if any invalidating flag, any "invalid >" threshold crossed, or valid cycles < 3; DEGRADED if any warning-level condition; else VALID. Provisional thresholds (final from §4.2): noise SD warn > 0.5 mV / invalid > 1.0 mV; ABBA drift warn > 1.0 mV / invalid > 2.0 mV; VDDA pre/post warn > 0.2 % / invalid > 0.5 %. Invalid readings transmitted with flags + sentinels; canonical `device_data` gets NULL (§5.1).

# Appendix D — Open decisions

| # | Decision | Needed by |
|---|---|---|
| D1 | PB5 polarity **and reset/ISP/brownout state + pull arrangement**, per node | Phase 0 §2.1 |
| D2 | Final mode number, FPort, and pinned region/data rate (airtime gate input) | Phase 1 start |
| D3 | Thermistor identity selected: PA2 / ADC_IN2 + PB4 excitation; verify the physical conditions and qualification inputs per node | Phase 0 §2.1 |
| D4 | Final warm-up value and QC thresholds | Phase 2 §4.2 |
| D5 | ~~Soil-temp column~~ **Resolved:** new canonical `soil_temperature_c`; `ext_temperature_c` stays DS18B20-only | — |
| D6 | Reference logger availability for Gate 2a | before Gate 2 |
| D7 | `ml3_readings` retention policy on the edge | Phase 3 schema cycle |
| D8 | Concrete fallback circuit (selected + validated per §2.5) if Gate 0 flips | Gate 0 |

# Appendix E — References

1. [Dragino LSN50 User Manual v1.7.3](https://www.dragino.com/downloads/downloads/LSN50-LoRaST/LSN50_LoRa_Sensor_Node_UserManual_v1.7.3.pdf)
2. [Dragino LSN50v2 datasheet](https://www.dragino.com/downloads/downloads/LSN50-LoRaST/Datasheet_LSN50_v2.pdf)
3. [Dragino LoRa_STM32 source](https://github.com/dragino/LoRa_STM32) — upstream baseline; `stm32l0xx_hw.c` battery≡VDDA measurement
4. [Delta-T ML3 ThetaProbe User Manual v2.1](https://delta-t.co.uk/wp-content/uploads/2017/02/ML3-user-manual-version-2.1.pdf) — output, wiring, warm-up, thermistor + Appendix 2 (power-off reading), V→√ε polynomial, clay/non-clay calibration, salinity effects
5. [STM32L072 datasheet](https://www.st.com/resource/en/datasheet/stm32l072v8.pdf) — ADC range 0–VREF+, VREFINT/TSENSE calibration, 100 ppm/°C VREFINT tempco
6. [STM32L07xxx/L08xxx errata ES0292](https://www.st.com/resource/en/errata_sheet/es0292-stm32l07xxxl08xxx-device-errata-stmicroelectronics.pdf)
7. [RM0376 reference manual](https://www.st.com/resource/en/reference_manual/rm0376-ultralowpower-stm32l0x2-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — ADC_CCR VREFEN/TSEN, SYSCFG_CFGR3 buffer enables/readiness
8. [AN5537 ADC oversampling](https://www.st.com/resource/en/application_note/an5537-how-to-use-adc-oversampling-techniques-to-improve-signaltonoise-ratio-on-stm32-mcus-stmicroelectronics.pdf)
9. [TI ADS1115 datasheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf) — fallback candidate, input-range limits
10. [LoRaWAN Regional Parameters RP002-1.0.3](https://lora-alliance.org/wp-content/uploads/2021/05/RP002-1.0.3-FINAL-1.pdf) — per-region/DR payload maxima
11. OSI Chameleon LSN50 firmware (standalone repo) — payload/versioning/build conventions only
