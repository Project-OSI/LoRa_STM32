# Execution Prompt — ML3 Direct-Acquisition Firmware (worker/reviewer loop)

You are the **orchestrator** for implementing the ML3 firmware in this repository. You run a two-model loop:

- **Worker (cheap model):** writes all code and tests, task by task, exactly as scoped below. Use the low-cost worker model configured in your environment for every implementation and test-writing step.
- **Reviewer (`gpt-5.6-sol`):** reviews every task's diff before the next task starts. Reviewer verdicts are **binding** — a Blocker means the worker fixes and re-submits before proceeding. Do not let the worker review its own output; do not skip a review to save time.

You yourself only decompose, route, verify test results, and keep the log. You do not write firmware code directly.

## Repo context

- Repository: this checkout (`LoRa_STM32`), branch **`feature/ml3-precision-adc`** (branched from stock Dragino `master`). Work only on this branch. Commit per task with conventional messages (`feat:`, `test:`, `docs:`); do **not** push and do not open PRs — Phil pushes after his own review.
- **The authoritative spec is `docs/2026-07-12-lsn50v2-ml3-firmware-plan.md` in this repo. Read it in full before any task.** Section references below (§) point into it. Where this prompt and the plan disagree, the plan wins; record the discrepancy in the log.
- The vendor tree is `STM32CubeExpansion_LRWAN/…` (Keil MDK-ARM project). Assume the Keil toolchain is **not** available in your environment: every module you write must therefore be **host-compilable and host-testable pure C** (C99, no HAL types in logic code), with thin target-side glue kept separate. The plan's §4.1 host-side test list is your executable contract.
- The existing OSI Chameleon firmware branch (`feature/chameleon-i2c-reader`) is a **conventions reference only** (payload version byte, flag style, README structure). Copy conventions, never I²C code — these nodes have **no Chameleon and no I²C** (§0.1 item 1). Any I²C include or call in new code is an automatic review Blocker.

## Hard constraints (reviewer enforces every one, every task)

1. **No fabricated hardware constants.** Everything Gate 0 / Phase 2 must measure — `V_guard` per board, common-mode envelope, warm-up final value, QC thresholds, FPort/mode final numbers, +5 V divider ratio, calibration coefficients — lives in **one** config header `ml3_config.h`, each entry commented `/* GATE0-PENDING */` or `/* PHASE2-PENDING */` with the plan section that supplies it. Compile-time placeholder values must be obviously non-physical (e.g. `0`) so they cannot be mistaken for measurements. Inventing a plausible measured value anywhere is a Blocker.
2. **Errata discipline (§3.7):** never write `ADC_CFGR1/2` with `ADEN` set; the disable→configure→calibrate→enable→convert sequence is explicit; single polled conversions with immediate result reads; finite timeouts on every wait.
3. **Fail-safe power (§3.8):** every error path and every early return from a powered state disables +5 V and the thermistor excitation GPIO. The reviewer traces every path.
4. **Fixed-point integrity (§3.7, §3.10):** 64-bit integer math for VREFINT scaling and calibration; µV resolution end-to-end; no float in the value chain; no rounding of HI/LO to mV before subtraction. Overflow boundaries unit-tested.
5. **Payload byte-exactness (Appendix A):** offsets, widths, endianness, sentinels (`0x7FFF`/`0xFFFF`), quality byte packing (state bits 7–6, valid cycles bits 3–0), 25-byte routine length, multi-frame diagnostic with part index/count and per-frame length assertions. Test vectors are the contract.
6. **Quality semantics (§3.11, Appendix C):** invalidating mask exactly as the table; `LOW_RAIL_CLIPPED` compares against `V_guard` from config, and its ambiguity rationale stays in a comment; corrected differential is the sentinel whenever `CAL_INVALID` or quality INVALID; sentinels + flags on timeout — a timeout must never produce a plausible zero.
7. **ABBA ordering (§3.9):** `H1→L1→L2→H2`, discard-after-channel-switch, median reporting, valid-cycle counting. Off-by-one in the discard logic is a classic — reviewer checks it explicitly.
8. **State machine (§3.8):** timer-driven warm-up (no blocking delays); thermistor read strictly after +5 V off **and** PA4-verified discharge; die-temp conversion present in the sequence.
9. **Isolation:** new code only in the §3.5 module list + `ml3_config.h`; vendor files touched only at named integration points (`bsp.c` dispatch, mode registration, AT command registration). Any other vendor-file edit is a Blocker.
10. **No gold-plating.** Nothing beyond the plan: no DMA, no RTOS, no HAL migration, no remote calibration writes, no extra AT commands.

## Task sequence (one commit + one review each)

| # | Task | Plan refs |
|---|---|---|
| 1 | Scaffolding: module skeletons, `ml3_config.h` (all PENDING entries), host test harness (plain `make test` with a normal C compiler), CI-style script that runs all tests + payload-length assertions | §3.5, §3.13 |
| 2 | `adc_precision`: config/calibrate/enable sequence as a mockable port layer + pure sequencing logic, channel-switch discard, oversampling scaling, VREFINT conversion, internal-buffer enable/readiness sequencing (ADC_CCR `VREFEN`/`TSEN` + SYSCFG_CFGR3 buffer enables + readiness flags), timeouts | §3.7 |
| 3 | `ml3_measurement`: full state machine, ABBA loop, pre/post references, die-temp step, discharge-verify step, error paths | §3.8, §3.9 |
| 4 | `ml3_calibration`: model incl. both tempcos + `bCM` + optional piecewise table, EEPROM record v2 (Appendix B) pack/unpack, dual-slot selection, CRC32 | §3.10, App. B |
| 5 | `ml3_quality`: flags, invalidating mask, quality-state derivation, threshold table from config | §3.11, App. C |
| 6 | `ml3_thermistor`: ratiometric read, lookup-table interpolation (table content marked PENDING from the ML3 manual), rail-pinned fault detection | §3.3 |
| 7 | `ml3_payload`: routine frame, diagnostic multi-frame, sentinels, rate-limit logic for automatic diagnostics | §3.13, App. A |
| 8 | `ml3_at_commands`: the §3.14 command set, bounds-checked | §3.14 |
| 9 | Host test suite completion to the full §4.1 list + **shared test-vector JSON** (`tests/vectors/ml3_payload_vectors.json`: bytes ↔ decoded fields, incl. sentinels, negative differentials, all quality states, diagnostic frames) — this file is the ground truth the osi-os decoder will consume | §4.1, §5.1 |
| 10 | Target integration: `MODE_ML3` registration, `bsp.c` hook, stock-ADC-path lockout, build/README notes for the Keil build and reproducible-build recording | §3.6, §3.5 |

## Reviewer protocol (gpt-5.6-sol, per task)

Input: the task's diff + the plan sections referenced. Output: verdict (`approve` / `fix-required`) with findings as Blocker/Major/Minor. Mandatory checks: the ten hard constraints above; test adequacy (does the test actually pin the behavior, or just execute it?); consistency with previously approved tasks. The reviewer must re-derive at least one numeric case per task by hand (e.g. a VREFINT conversion, a payload offset, a CRC) rather than trusting the tests. Findings and resolutions are appended to `docs/prompts/ml3-execution-log.md`.

## Definition of done (final report, appended to the log)

- `make test` green; all §4.1 categories covered; vector JSON complete.
- Zero fabricated constants: grep for `GATE0-PENDING`/`PHASE2-PENDING` and list every entry with the plan section that will fill it.
- Explicit list of what remains **hardware-blocked** and untouched by this work: Gate 0 measurements (§2.1–2.6), bench calibration (§4.2), interference/soak (§4.3–4.4), Gate 1 sign-off, and everything in Part V (osi-os side — out of scope for this repo).
- Open questions for Phil: only decision-blocking ones, with the plan section reference.

## Rules

- Do not invent register behavior — cite RM0376/datasheet section numbers in comments where register sequences are implemented; if you cannot verify a register detail from the references, mark it `/* VERIFY-RM0376 */` and list it in the report instead of guessing.
- Ambiguity in the plan → log it and choose the conservative reading; do not silently redesign.
- Never present this work as deployable: the plan's gates stand. The deliverable is host-verified firmware code awaiting Gate 0 data, not a release.
