# Agent prompt — Task 10B (reduced scope): first measuring firmware

Hand this file to the implementing agent as its brief. Written 2026-08-09, branch head `96cd037`. This supersedes the scope in `docs/task10-agent-prompt.md`, whose Task 10A portion is complete.

---

## Mission

Produce the first firmware that actually measures the ML3 and puts a reading on the air. The measurement engine, quality evaluation, payload framing and AT surface are written, host-tested and reviewed; what is missing is the ADC port that connects the engine to real hardware, the service wiring that drives it, and the configuration values that bench characterization has supplied.

You will not have hardware. Your deliverable is code that compiles clean for the target, keeps every host test green, and a bring-up checklist precise enough for the project owner to execute at the bench.

## Scope: deliberately reduced

This is a few-month, four-node sensor trial. Scope was cut on 2026-08-09 by the project owner. **In scope:** the STM32 ADC port, `BSP_ML3_Service` wiring, and filling the configuration from the Gate 0 §4 record.

**Explicitly OUT of scope — do not implement, and do not "helpfully" add:**

- **EEPROM calibration storage.** No storage port, no slot map, no `device_id_hash`, no `AT+ML3CAL` commit path. Soil calibration now happens in the osi-os backend, and §4 measured the electrical chain accurate to ≤0.2 mV with no correction at all. Send `calibration_id = 0` (uncorrected). The decisions recorded in `docs/ml3-port-adapter-spec.md` §4 stand for when this returns; they are not needed now.
- **The thermistor.** PA2/PB4 identities are selected and gated (Task 10A) but the circuit is not built. Report soil temperature as unavailable so the payload emits its sentinel. Leave every thermistor readiness macro at zero.
- **Soil-moisture conversion.** The payload carries microvolts and has no water-content field. Voltage → refractive index → water content happens downstream. Do not add polynomials or soil coefficients to firmware.

## Where things are

- Working copy: `/home/phil/.config/superpowers/worktrees/LoRa_STM32/ml3-precision-adc`, branch `feature/ml3-precision-adc`.
- App root: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/` — **the parenthesis is literal; quote every shell path**.
- Build: `make -C gcc` (deployable) and `make -C gcc BENCH=1` (bench diagnostic). `arm-none-eabi-gcc` 16.1 installed; see `gcc/README.md`.
- Host tests: `bash tests/host/run_ml3_host_tests.sh` — **from the worktree root, not the app directory.**

## Read first, in this order

1. **`docs/gate0-records/M013437-A840412D385E7D00/section4-adc-characterization.md`** — the hardware measurements your configuration values must trace to, plus findings that constrain the ADC port.
2. **`docs/ml3-port-adapter-spec.md`** — the port seams. §4 (EEPROM) is out of scope; §3 and §5 are your contract.
3. **`docs/2026-07-12-lsn50v2-ml3-firmware-plan.md`** (v2.1) — governing design. It wins on conflict; if you find a genuine conflict, stop and report rather than choosing.
4. `src/bench_adc.c` — **working, hardware-proven ADC code for this exact board.** It is a bench tool, not the production port, but its register/HAL choices, sampling time, calibration call and VREFINT handling are validated against a meter. Use it as your reference implementation.
5. Headers as contracts: `inc/adc_precision.h`, `inc/ml3_measurement.h`, `inc/bsp.h`.

## What exists — do not rebuild or redesign

Seven modules are complete, host-tested under a strict `-Werror` set, and have passed multiple review rounds including independent fuzz-verification of their arithmetic: `adc_precision`, `ml3_measurement`, `ml3_calibration`, `ml3_thermistor`, `ml3_quality`, `ml3_payload`, `ml3_at_commands`. They are pure logic behind port structs. **Implement the port; do not touch the modules.** If you believe one contains a defect, report it — do not fix it as a side effect.

## Hard constraints

1. **The VREFINT stabilization guard is mandatory and is the known trap here.** The vendor HAL applies `ADC_TEMPSENSOR_DELAY_US` for the temperature sensor but never the documented `LL_ADC_DELAY_VREFINT_STAB_US` on the VREFINT path. On the bench tool this made the first reference sample read near full scale and skewed derived VDDA by 2.6% — diagnosed, fixed and confirmed on hardware (`2c355bc`). The production core's channel-change discard happens to be long enough in practice, **but coincidence is not a guard.** Add an explicit elapsed-time guard, and use a conversion timeout of at least ~10 ms. A 2.6% reference error propagates into every reading.
2. **A residual settling tail is still open.** Post-fix, the bench tool shows `VREF_MAX_FIRST` ≈ 1505 against a settled ≈ 1372, implying several samples decaying rather than one outlier. One discard plus 10 µs removes most, not all. Consider a longer delay or two discards in the production port, and say what you chose and why.
3. **Preserve the ES0292 ADC sequencing** the modules encode: stop → disable → configure → calibrate → enable → convert; no `CFGR` writes while `ADEN` is set.

## Safety invariants — non-negotiable

- Every abort path disables the +5 V rail and thermistor excitation. `cleanup_controls` does this; introduce no path that bypasses it.
- Do not move or remove watchdog refresh points.
- +5 V control is **PB5, active-low** (`PWR_OUT_PORT`/`PWR_OUT_PIN`, `inc/stm32l0xx_hw_conf.h:265-267`; `bsp.c:522` writes `GPIO_PIN_RESET` to enable). Inverting this powers a probe continuously, which the manufacturer forbids.
- The ML3 needs 0.5–1 s warm-up and must not be powered continuously.

## DECIDED 2026-08-09 — probe power source: the PB5-switched +5 V rail

The owner has ruled: the trial powers the ML3 from the **LSN50v2's own switched +5 V output under PB5 control**, not an external supply. This is the designed path — the probe is energized only for the measurement window, honouring the manufacturer's "do not power continuously" instruction and preserving battery life.

Consequences you must respect:

- The engine's warm-up sequencing, V5 monitoring via PA4, and discharge verification are all **live and load-bearing**. Wire them fully; do not stub them.
- The V5 limit, divider-ratio and discharge macros are supplied as **nominal values in Phase 2**, on the owner's hardware observation rather than a bench measurement. Use exactly those; do not substitute your own figures, and do not present them in comments or reports as measured.
- The **brownout rail check becomes a prerequisite before field installation** (not before your code). It is deferred at the owner's discretion, but the trial now depends on the rail behaving during a dying battery. Note this in your bring-up checklist.

## The work — phases, stop for review between them

**Phase 1 — ADC port.** Implement `adc_precision`'s port struct against the STM32L0 registers/HAL, using `bench_adc.c` as the validated reference. Honour the module's sequencing contract where it differs from the vendor's `HW_AdcInit`. Constraints 1 and 2 land here.

**Phase 2 — configuration.** Fill the `GATE0-PENDING` macros in `inc/ml3_config.h` from the §4 record and manufacturer spec. **Every value must cite its source line; invent nothing.** If a macro has no traceable basis, leave it pending and say so. Known inputs:
- Zero-ambiguity guard: **2 mV** (measured offset +0.35 mV; zero-point noise under 1 LSB ≈ 0.89 mV; probe floor observed 5.0–5.6 mV).
- Common-mode envelope: LO leg observed **5.0–7.2 mV** across air, damp and water — derive min/max with stated margin.
- Signal ceiling: must accommodate the observed **1110 mV** (above the 1 V nominal).
- Warm-up: **1500 ms** provisional (manufacturer 0.5–1 s plus margin).
- **V5 monitoring and discharge — use the nominal values below.** These are *not* bench-measured. The owner ruled on 2026-08-09, from direct experience with this hardware, that the board's 5 V output is stable at 5 V and shuts off completely, and that a bench session to measure it was not worth the time. Record each one's basis in the config comment as **owner hardware observation, not a bench record** — do not present them as measured.

| Macro | Value | Basis |
|---|---|---|
| `ML3_CONFIG_V5_MINIMUM_MV` | `4500` | Presence/sanity check, not spec enforcement: catches a dead or collapsed rail while tolerating normal variation and divider tolerance. The ML3's own minimum is 5.0 V; enforcing that precisely is not possible from a nominal-ratio divider. |
| `ML3_CONFIG_V5_MAXIMUM_MV` | `5500` | Upper sanity bound on the same basis. |
| `ML3_CONFIG_V5_DIVIDER_RATIO_PPM` | `500000` (ratio 0.5) | Two equal 1 kΩ resistors are fitted on the bench node, `+5V (14) → PA4 (26) → GND (15)`. Ratio is nominal; resistor tolerance gives a couple of percent, which is ample for a presence check. **The divider is required** — without it PA4 floats near zero and would be read as a failed supply, invalidating good measurements. |
| `ML3_CONFIG_DISCHARGE_THRESHOLD_MV` | `500` | Generous threshold below which the rail counts as collapsed. Owner reports the rail shuts off completely. |
| `ML3_CONFIG_DISCHARGE_TIMEOUT_MS` | `2000` | Generous. **Nothing in this trial depends on it** — discharge verification exists to guarantee the probe is unpowered before a thermistor read, and the thermistor is out of scope. |

- Region/DR/airtime: from the bench gateway's actual configuration — ask if not recorded.

**Phase 3 — service wiring.** Replace the no-op body of `BSP_ML3_Service` per spec §5: instantiate the port struct once, initialise the engine from existing settings, drive `ml3_measurement_step` to completion across service calls, keep `ml3_active` true so the main loop blocks low-power mode during acquisition, route `BSP_ML3_Abort` to `ml3_measurement_abort`. Report soil temperature unavailable; set `calibration_id = 0`.

**Phase 4 — gates.** Only after 1–3 are reviewed, flip the readiness macros so `ML3_CONFIG_ACQUISITION_READY` evaluates true, and confirm the engine is now linked into the default image (`arm-none-eabi-nm`). This deliberately changes the deployable image and its hash; the byte-identity gate that governs bench-tool work does not apply and must not be cited to avoid the change. Leave all thermistor readiness macros at zero.

## Verification gates — evidence, not assertions

- `bash tests/host/run_ml3_host_tests.sh` green (worktree root).
- `bash tests/host/ml3_target_integration_contract.sh` green; extend it for the new wiring in its existing style. Note it currently asserts `ACQUISITION_READY == 0` — that assertion must be updated in Phase 4, deliberately and visibly, not quietly deleted.
- `make -C gcc` clean; report text/data/bss and headroom against 192 KB flash / 20 KB RAM.
- `make -C gcc BENCH=1` still clean.
- New adapter sources compile under the same strict flag set as the seven ML3 modules (`gcc/Makefile`), including `-Wconversion` and `-Wshadow`.
- After Phase 4, confirm measurement-engine symbols are present in the default ELF (absent today by design).

## Deliverables

1. Code in phase-sized commits, conventional-commit subjects. Do not push.
2. A report per phase: what changed and where, your VREFINT guard choice and reasoning, per-macro citations, the power-source answer, and all gate evidence.
3. **A bring-up checklist** for the owner, in plain accessible language, one step at a time, with the expected observation and an explicit stop condition per step. Assume a multimeter and a serial console (`picocom -b 9600 /dev/ttyUSB0`; note `AT+ML3VER?` takes no `=`), flashing via STM32CubeProgrammer's GUI. Sequence it so the node is verified healthy in stock mode **before** `AT+MOD=10` is issued, and so the rail is confirmed safe before any probe is attached.

   Include these two specific checks:
   - **Probe unpowered, rail off:** meter PA0 (terminal 2) and PA1 (terminal 3) against ground; both should sit at essentially zero. This closes out a backpowering question and fills the missing "unpowered" row in the §3 envelope record.
   - **Do not use `AT+5VT`.** It corrupts the stack and resets the node: `at_5Vtime_set` (`src/at.c:1937`) passes a `uint16_t*` to a `%d` conversion that stores through an `int*` (`src/tiny_sscanf.c:820`), a four-byte write into a two-byte slot. Confirmed on hardware 2026-08-09 in the GCC build. It is a pre-existing vendor defect, out of scope to fix, and irrelevant to ML3 mode where the engine drives the rail directly — but it must not appear in any procedure you write.

## What not to do

- Do not edit vendor sources. Work around vendor defects in your adapter and document it.
- Do not modify the seven ML3 modules, their tests, or the measurement/quality/payload logic.
- Do not implement anything in the "OUT of scope" list.
- Do not invent configuration values, calibration constants, or timing figures.
- Do not claim hardware behaviour you have not observed — you have none. Say "expected" where you mean expected.
- Do not push, and do not flip readiness gates before phases 1–3 are reviewed.
- If the plan and this brief disagree, stop and report; the plan governs.
