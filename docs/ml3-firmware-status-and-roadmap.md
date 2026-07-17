# ML3 firmware: status and road to the deployable image

This branch holds the complete host-verified logic core for direct ML3 ThetaProbe acquisition on the LSN50v2, deliberately inert until bench measurements exist. This document is the working map: what is done and trusted, what is gated and why, and the ordered path to a flashable production image. The design contract is `docs/2026-07-12-lsn50v2-ml3-firmware-plan.md` (v2.1); the independent review that hardened this state is `docs/reviews/2026-07-16-ml3-precision-adc-review.md`.

## 1. Current state

| Layer | State | Trust basis |
|---|---|---|
| `adc_precision` (ES0292 sequencing, µV math, discard logic, recoverable conversion faults) | done, host-tested | fuzz-verified against arbitrary-precision references (review §1) |
| `ml3_measurement` (16-state machine, ABBA burst with per-cycle fault tolerance, discharge verify, fail-safe cleanup) | done, host-tested | state-machine tests + independent stats verification |
| `ml3_calibration` (fixed-point model, redundant CRC'd EEPROM records) | done, host-tested | fuzz + fault-injection verified |
| `ml3_thermistor`, `ml3_quality`, `ml3_payload` (25-byte routine + multi-frame diagnostic + shared vectors) | done, host-tested | unit tests + shared JSON vectors (`tests/vectors/ml3_payload_vectors.json`) |
| `ml3_at_commands` + `at.c`/`command.c` dispatch | done, wired, host-tested | live in the target build; AT surface responds on hardware today |
| Vendor integration (`main.c`, `bsp.c`) | skeleton wired, acquisition gated | mode 10 selectable locally; `BSP_ML3_Service` inert behind `ML3_CONFIG_ACQUISITION_READY` |
| STM32 register/EEPROM port adapters | **absent by design** | blocked on Gate 0 (no fabricated hardware constants) |
| Keil build target | all modules in `Lora.uvprojx` | not yet compiled with ARMCC; host build is the verified one |

Two facts worth keeping in view. A device set to mode 10 today transmits nothing (acquisition inert), which is why the remote downlink switch into mode 10 is refused while the gate is false. And 5 of 7 modules have no production caller yet — they are linked into the target but unreachable until §3 below.

## 2. Decisions taken (canonical)

| Decision | Value | Where |
|---|---|---|
| Operating mode / FPort (plan D2) | mode 10, **FPort 13** | `ml3_config.h:16-19`; supersedes the plan's "proposed 12"; the osi-os edge decoder must target 13 |
| ABBA cycle range | 3–8 (plan said 2–8; 2 could never validate) | `ML3_MEASUREMENT_MIN_ABBA_CYCLES`, review finding 1 |
| Per-cycle fault tolerance (plan §3.9) | conversion timeout/overrun is recoverable at the ADC layer; the failed cycle is skipped; fault flags raised only if valid cycles < 3; reference/calibrate/die-temp failures still abort the whole reading | owner decision 2026-07-16, review finding 2 |
| Protocol | version 1, routine 25 bytes big-endian, sentinels per plan Appendix A | `ml3_config.h`, `ml3_payload.h` |

## 3. Road to the final image

Stages are ordered by dependency; nothing in a later stage starts before the earlier gate is recorded.

### Stage A — Gate 0 bench characterization (hardware, no firmware)
Run `docs/ml3-gate0-bench-runbook.md` on all four nodes/probes. Output: measured values for every `GATE0-PENDING` macro in `ml3_config.h`, the go/no-go decision (direct ADC vs ADS1115-class fallback), and resolution of D1/D3/D8. If Gate 0 flips to the fallback, the payload, quality, calibration, and OSI integration all carry over unchanged; only `adc_precision`/`ml3_measurement`'s ADC interaction is replaced.

### Stage B — Task 10: port adapters and result-chain wiring
Implement `docs/ml3-port-adapter-spec.md`: the three port structs against real registers, the `on_process`/`on_build_payload`/`on_queue` chain (calibration apply → quality evaluate → payload build → LoRaWAN queue), EEPROM slot map + `device_id_hash` decision, calibration commit from the staged AT chunks. Fill the Gate 0 macros; `ML3_CONFIG_ACQUISITION_READY` becomes true. First ARMCC build of the full target; bench bring-up verifies the ES0292 sequence on silicon.

### Stage C — Phase 2 qualification (Gate 1)
Plan Part IV: per-node automated calibration sweeps (~3,500 acquisitions/node), definitive warm-up sweep, thermistor bath calibration against the ML3 manual table, interference matrix, 10,000-cycle soak with the §4.4 numeric acceptance, reproducible-build hash reproduction. Output: per-node EEPROM calibration records, `PHASE2-PENDING` macros filled, Gate 1 sign-off → `ML3_CONFIG_DEPLOYABLE` true. Only then replicate to the remaining three nodes.

### Stage D — OSI edge/cloud integration (osi-os, parallel after B)
Plan Part V runs its own spec/plan cycle in osi-os under schema change control. Binding inputs from this repo: FPort 13/mode 10, the shared payload vectors as decoder ground truth, `ml3_readings` + `device_data` NULL-for-INVALID semantics, new canonical `soil_temperature_c`, edge-authoritative VWC in percent. The vector generator (`tests/tools/ml3_payload_vectors.js`) is the single source for both firmware and ChirpStack codec tests.

### Stage E — Field pilot (Gate 2) and release
Gate 2a acquisition validation (reference logger on the same ML3), Gate 2b agronomic validation before VWC is presented as parcel moisture. Release per plan §3.5: `release/ml3-v1.x`, archived toolchain and map, second-developer hash reproduction.

## 4. Building and testing

- Host suite (the verification that exists today): `bash tests/host/run_ml3_host_tests.sh` — compiles every module with `-std=c99 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wvla -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wundef` and runs all C suites plus the vector/coverage tools. `make test` wraps the fuller 12-stage suite (includes runner self-tests; needs `setsid`).
- Target build: Keil MDK, `MDK-ARM/STM32L072CZ-Nucleo/Lora.uvprojx`, group `Projects/End_Node/ML3`. No GCC/CMake firmware build exists.
- Contract checks worth running after config edits: `tests/host/ml3_config_contract.sh`, `tests/host/ml3_target_integration_contract.sh`.

## 5. Known gaps and their owners

| Gap | Blocking | Owner/when |
|---|---|---|
| No register port adapters, EEPROM commit, or result-chain wiring | acquisition on hardware | Stage B |
| `ml3_config.h` readiness macros all zero | `ACQUISITION_READY` | Stage A measurements |
| Thermistor production lookup table absent (manual table, not generic 10K3A1B) | soil temperature | Stage C (`THERMISTOR_TABLE_*`) |
| Auto-diagnostic rate-limiter table is RAM-only | one re-armed diagnostic after reset | accepted (within §4.4 watchdog budget); revisit at Stage C if soak data disagrees |
| ARMCC compile of the target never exercised | unknown target-only warnings | first Stage B build |
| Binding external review missing for tasks 5/6/9 | process completeness | superseded by the 2026-07-16 independent review |
