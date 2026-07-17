# Branch review: feature/ml3-precision-adc (2026-07-16)

Independent review of the 13 implementation commits (`8405210..2b3baa8`, tasks 1–10 of the worker/reviewer execution), covering all seven ML3 modules, the vendor-file integration, and the host test harness. Method: four parallel read-only exploration passes (one per module cluster), then independent deep verification of the four hardest arithmetic cores by a separate reviewer that compiled the unmodified repo sources into fuzz harnesses and cross-checked them against arbitrary-precision references under UBSan/ASan. Nothing in this review relied on the branch's own test results.

## Verdict

The logic core is sound: every deep-verified item except one came back correct, and the one defect had a one-line fix. The branch is exactly what its execution log claims — host-verified library code awaiting Gate 0 data, with acquisition compile-time gated off and no register-level port adapters. Fixes from this review landed as tasks R1 and R2 (commits listed in §4).

## 1. Deep-verification results

| Item | Verdict | Evidence |
|---|---|---|
| ABBA statistics engine (median, MAD, exact-integer variance + floor-sqrt, `ml3_measurement_compute_uv_stats`) | correct | algebra re-derived; 1.4 M fuzzed bursts bit-exact vs `__int128` reference; `centered²` overflow proven impossible for clamped inputs |
| `abba_cycles = 2` configuration | **bug, confirmed** | floor of 2 accepted everywhere while quality/payload/stats all require ≥ 3 valid cycles; a 2-cycle node emits INVALID frames forever. Fixed in R1 |
| Calibration fixed-point apply (ppm gain, tempco cross term, 128-bit mul-div, piecewise) | correct | 1.5 M fuzzed applies bit-exact incl. negative `V_raw` with negative `dT`; five hand-recomputed vectors match |
| EEPROM record CRC + redundant-slot commit | correct | CRC coverage proven by flipping all 76 bytes of a record; bounds checked before variable-length reads; sequence-wraparound and write-fault injection at every index clean |

## 2. Findings and resolutions

| # | Severity | Finding | Resolution |
|---|---|---|---|
| 1 | High | `ML3_MEASUREMENT_MIN_ABBA_CYCLES 2U` contradicts the fixed valid-cycle floor of 3 | R1: floor raised to 3, aligned across measurement/AT/bsp validators and tests |
| 2 | High (design) | Plan §3.9's "INVALID if fewer than 3 of 4 valid" tolerance unreachable: `cycle_valid[]` only ever set true; any conversion failure aborted the whole burst, so valid-count was always 0 or N | Adjudicated 2026-07-16 (owner decision): loosen `adc_precision` so conversion timeout/overrun is recoverable (bounded stop + forced discard on next read; stop-failure stays session-fatal); measurement layer skips the failed cycle. Implemented in R2 |
| 3 | Medium | Remote downlink `AT+MOD=10` accepted while acquisition is gated off — a remote command could dark a field node (mode 10 queues no uplinks) | R1: downlink acceptance gated on `ML3_CONFIG_ACQUISITION_READY`; local serial path unchanged for bench work |
| 4 | Medium | `ML3_AT_STATUS_RECORD_CONTAINS_NUL` declared and dispatched but never returned | R1: wired for NUL inside the `AT+ML3CAL=` record portion |
| 5 | Low | Diagnostic frame part nibbles packed without range guard | R1: out-of-nibble `part_index`/`part_count` rejected |
| 6 | Low | `ml3_payload_quality_is_consistent` accepted DEGRADED with zero flags (a tuple the quality module cannot produce) | R1: rejected on builder and frame-validator paths |
| 7 | Low | `-Wmaybe-uninitialized` on `old_crc_offset` (provably safe) | R1: initializer added |
| 8 | Info | FPort is 13 in code vs "proposed 12" in the plan; `ml3_config.h` records it as the authorized D2 decision | FPort 13 / mode 10 is canonical; the osi-os edge cycle must build against it |
| 9 | Info | `ML3_STATE_ADC_CONFIGURE` is a pass-through (all work in ADC_CALIBRATE); `adc_precision_read_uV` unused by production path | Left as-is; harmless, test-covered API |

## 3. Process observations

- Tasks 5, 6, and 9 carry only native (non-binding) review; the binding `gpt-5.6-sol` review was quota-blocked and never happened. Task 10 (the vendor-file wiring, commit `2b3baa8`) had no recorded reviewer verdict at all. This review covers both gaps.
- The host suite builds warning-clean under the strict flag set and passes. Its shell mass is inverted, though: ~4,000 of ~5,000 shell lines test the test runner's own signal/process handling rather than firmware behavior. The hardening was reviewer-driven (real runner bugs), but future test work should go through a standard harness instead of extending the `/proc`-forensics scripts.
- The execution log's self-reported state matched what the code shows in every checked instance — the log is trustworthy.

## 4. Fixes applied from this review

| Task | Commits | Content |
|---|---|---|
| R1 | `75f3417`, `e309529` | findings 1, 3–7; ~22 test fixtures moved from 2-cycle to 3-cycle configs, one latent test-harness abort gap fixed en route |
| R2 | `8202ae8` | finding 2: recoverable conversion faults in `adc_precision`, per-cycle skip in the ABBA burst, fault flags raised only when the valid count falls below 3 |

R1 passed a task-scoped review (spec compliant, quality approved, two minor notes). R2 and the whole range passed a binding deep review on 2026-07-17 — verdict APPROVE FOR PUSH, with the recoverability boundary, stop-failure composition, channel-cache invalidation, state-machine bookkeeping, flag semantics, fail-safe/watchdog invariants, and test non-vacuity each independently verified (one scratch harness was built to exercise a permanently-unstoppable ADC mid-burst; it aborts cleanly with both rails low).

Non-blocking follow-ups from that review: stop-failure during ABBA reports the final INVALID cause as `ADC_INIT` rather than the originating class, and a later systemic abort drops recorded per-cycle classes (combined attribution is a two-line change at `set_adc_fault_and_continue` if ever wanted); a task3-level stop-failure composition test would be a useful addition.

Plan errata recorded by these fixes: §3.9/§3.14 cycle range is 3–8 (was "2–8"); §3.9's tolerance semantics are as decided in finding 2.
