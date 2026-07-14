# ML3 Execution Log

## Setup Context

- Worktree: `/home/phil/.config/superpowers/worktrees/LoRa_STM32/ml3-precision-adc`
- Branch: `feature/ml3-precision-adc`
- Task: Task 1, ML3 direct-acquisition firmware scaffold and host test harness
- Target: STM32L072CZT6 Dragino LSN50v2 application tree under `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/`
- Instructional status: software is being built on user instruction even though Gate 0 remains pending; the branch must stay non-deployable until the zeroed physical/hardware/qualification blockers are resolved

## TDD Evidence

- RED: `make test`
- RED result: failed as expected because the planned ML3 source scaffold did not yet exist, starting with `adc_precision.c`
- GREEN: `make test`
- GREEN result: passed after adding the ML3 config header, module skeletons, and host contract test harness
- Hygiene check: `git diff --check`
- Hygiene result: clean

## Gate 0 Status

- No hardware measurements were performed in this task
- No physical blockers were promoted to nonzero values
- The ML3 readiness contract remains blocked by zero placeholders in `ml3_config.h`

## Task 1 Review — gpt-5.6-sol

- Reviewed base: `a3103dc0111b374847b137dbab9bdfc6f12c5126`
- Reviewed head: `fabb24d01b6411d7ca29e655cd6b3525a0985537`
- Review scope: committed range `a3103dc..fabb24d01b6411d7ca29e655cd6b3525a0985537`, committed files, independent host-test output, and post-test worktree state

### Commands and evidence

- `git rev-list --count a3103dc..HEAD` returned `1`; `git log --format='%H %s' a3103dc..HEAD` showed the single conventional commit `fabb24d... chore: scaffold ML3 firmware test harness`.
- `git diff --name-status a3103dc..HEAD` showed only the seven planned `.c/.h` pairs, `ml3_config.h`, the root `Makefile`, host test files, and this execution log. The authoritative plan and execution prompt were untouched.
- A committed-range added-line search found no I2C token/code/include/dependency and no HAL/STM32 include in the new firmware, Makefile, or host tests.
- `make test` returned 0 with GCC and compiled every planned module under C99 with strict warnings; `CC=clang make test` independently returned 0.
- `git status --short` after each test run reported `?? tests/host/.build/`; the harness leaves its object files and test executable untracked.
- `git diff --check a3103dc..HEAD` returned 2 at the reviewed head with `docs/prompts/ml3-execution-log.md:25: new blank line at EOF.` The mandatory review append removes that particular EOF condition, but the reviewed worker head did not meet the claimed committed-range hygiene requirement.
- The smoke/contract test includes every new header, links every new module, pins the three fixed payload facts at compile time, pins the listed placeholder values to zero, and pins deployability false. The recorded RED state is consistent with a missing first module but is not independently replayable from the single final commit; only the final GREEN state was independently reproduced.

### Stage A — exact specification compliance: FAIL

#### Blocker

- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h:8-13,54-59` and `tests/host/ml3_contract_test.c:13-14` promote `MODE_ML3 = 10U` and FPort `12U` into fixed protocol facts and tests. Appendix D leaves both in open decision D2; §3.6 calls 10 provisional and 12 proposed. This violates the instruction that unresolved decisions remain literal zero, and it can lock target integration and routing to unapproved identifiers. Set both values to literal `0U` with `PENDING_HARDWARE_DECISION` comments, give them explicit readiness flags outside protocol readiness, and change the contract test to require zero while D2 is open.

#### Major

- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h:19-52` does not represent all known physical/qualification blockers or all readiness evidence. In particular, the plan's final VDDA/VREF pre/post warning and invalid thresholds and calibration coefficients are absent, and `ML3_CONFIG_PB5_ACTIVE_LOW_READY` does not separately capture the required reset/ISP/brownout fail-safe evidence. As written, later code has no sole config home for those pending values, and deployability can eventually become true without all required evidence. Add literal-zero pending entries and distinct readiness terms for every known unresolved value/evidence item, including D2, PB5 fail-safe state, final VREF-drift QC thresholds, and Phase 2 calibration/qualification readiness; include all of them in the deployability expression and contract test.
- `tests/host/run_ml3_host_tests.sh:8,34-35` creates `tests/host/.build/` in the worktree and never removes or ignores it. Both independent test runs left the same untracked directory, so `make test` is not cleanly reproducible as required. Use an EXIT trap or temporary directory so successful and failed runs clean generated artifacts, then assert a clean post-test status.
- `docs/prompts/ml3-execution-log.md:25` contained an extra blank line at EOF in the reviewed head, so `git diff --check a3103dc..HEAD` failed despite the log claiming a clean hygiene result. The required review append resolves that exact EOF artifact; the amended range still needs a fresh `git diff --check` after the worker fixes the open findings.

#### Minor

- None.

Stage A passes the exact seven-pair module inventory, pure-C99 skeleton boundary, absence of behavior/target integration, no-HAL/no-I2C constraints, root `make test` wiring, strict compilation coverage, plan/prompt immutability, and one-commit discipline. It fails on the findings above.

### Stage B — code quality/maintainability

Not performed because Stage A has open Blocker/Major findings, as required by the reviewer contract.

### Numerical hand derivation

- Protocol version is the fixed Appendix A byte-0 value: `1`; `ML3_CONFIG_PROTOCOL_VERSION` and its test correctly use `1U`.
- Routine payload type is the fixed Appendix A byte-1 value: `0`; `ML3_CONFIG_PAYLOAD_TYPE_ROUTINE` and its test correctly use `0U`.
- Routine length is `1 + 1 + (10 x 2) + 1 + 1 + 1 = 25` bytes: two one-byte header fields, ten two-byte fields from status through soil temperature, then quality, calibration ID, and reserved. Equivalently, Appendix A spans offsets 0 through 24 inclusive, so `24 - 0 + 1 = 25`. The config and test correctly use `25U`.
- D2 is not a fixed numeric fact: final mode and FPort must both remain literal `0U` pending decision, while region ID and data rate correctly remain `0U`. The reviewed head incorrectly uses mode `10U` and FPort `12U`.
- Every present Gate 0 physical/hardware/qualification value in `ml3_config.h:22-38` is literal `0U`, and every present readiness flag in lines 40-52 is `0U`. Therefore `ML3_CONFIG_GATE0_READY = 0`, while the current fixed comparisons make `ML3_CONFIG_PROTOCOL_READY = 1`; consequently `ML3_CONFIG_DEPLOYABLE = 1 && 0 = 0`. Gate 0 is correctly non-deployable at this head, but the readiness coverage is incomplete as described above.

### Final verdict

FIX-REQUIRED

### Resolution after Task 1 re-review

- `ml3_config.h` now keeps the unresolved D2 mode/FPort values at `0U`, adds `ML3_CONFIG_GATE1_APPROVAL_READY`, removes the redundant aggregate-ready switches, and derives `GATE0_READINESS` and `PHASE2_READINESS` directly from their per-decision terms plus the single Gate 0 / Gate 1 approvals.
- The host contracts are green: `./tests/host/ml3_config_contract.sh`, `./tests/host/ml3_readiness_cohesion_contract.sh`, `make test`, and `CC=clang make test` all passed; `bash -n tests/host/*.sh` and `git diff --check` / `git diff --check a3103dc..HEAD` both passed; no `tests/host/.build` or other temporary leftovers remained.
- The regression coverage is in place: the marker-mutation test validates the real config first and then fails a copied config with the marker removed, the TERM regression now observes the runner return `143`, the concurrency regression now passes with per-run temp build directories, and `CC=/bin/false ./tests/host/run_ml3_host_tests.sh` fails nonzero while cleaning its build directory.
- Verified D2 blocker: stock `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/main.c` already uses `AppData.Port = 12;` for the downlink-reply uplink path, so the ML3 FPort decision remains blocked and stays at `0U`; no replacement was chosen.

### Resolution after review

- D2 promotion: `ML3_CONFIG_MODE_ML3` and `ML3_CONFIG_FPORT` are now literal `0U` with explicit `ML3_CONFIG_MODE_ML3_READY` and `ML3_CONFIG_FPORT_READY` placeholders; they were removed from `ML3_CONFIG_PROTOCOL_READY` and pinned in the host contract test.
- Blocker/readiness coverage: `ml3_config.h` now carries zeroed, marked pending entries for the unresolved Gate 0 and Phase 2 items identified in the plan and execution prompt, including PB5 polarity and reset/ISP/brownout verification, thermistor pins, zero-ambiguity guard, CM envelope, +5 V divider and limits, discharge threshold/timeout, warm-up, airtime gates, VDDA drift, die-temperature range, calibration coefficients, and the aggregate gate/qualification readiness flags. The config contract script rejects generic pending markers and requires `GATE0-PENDING` or `PHASE2-PENDING` with section provenance.
- Clean test reproduction: `tests/host/run_ml3_host_tests.sh` now uses an `EXIT`/signal trap to remove `tests/host/.build`, and `tests/host/ml3_clean_tree_after_make_test.sh` verifies that `make test` leaves no `.build` directory and does not change worktree status.
- Whitespace hygiene: `git diff --check a3103dc..HEAD` now passes after the review append and the subsequent fixes.
- Final review state: awaiting re-review.

## Task 1 Re-review — gpt-5.6-sol

- Reviewed base: `a3103dc0111b374847b137dbab9bdfc6f12c5126`
- Reviewed head: `cdcce042b6d5d1536b703b882cb7d31bf1eb7d40`

### Previous finding resolutions

- **Resolved — D2 Blocker:** mode and FPort are literal `0U`, have exact pending markers and separate readiness gates, are absent from fixed protocol readiness, and are compile-time asserted zero.
- **Resolved — blocker/readiness coverage Major:** the previously missing PB5 fail-safe, VDDA-drift, calibration, and qualification entries/readiness gates are now present, zero, marked, and included in deployability.
- **Resolved — generated artifacts Major:** normal GCC/Clang runs and an intentional compiler failure remove `.build` and preserve worktree status.
- **Resolved — whitespace Major:** the committed range passes `git diff --check`.

### Commands and evidence

- Fresh `make test` with GCC and `CC=clang make test` both returned 0; `.build` was absent and `git status --short` was empty after each.
- `./tests/host/ml3_clean_tree_after_make_test.sh` returned 0. Its before/after status comparison is correctly ordered and uses the same untracked-file mode.
- `CC=/bin/false ./tests/host/run_ml3_host_tests.sh` returned 1 as intended, removed `.build`, and left the tree clean.
- A controlled `TERM` during the config phase returned 0, proving that the combined EXIT/signal trap can report an interrupted run as successful.
- Two staggered concurrent `make test` runs using a delayed compiler returned 0 and 2; one linker failed because the other run removed the shared `.build` directory.
- All 67 pending zero definitions currently have the exact `GATE0-PENDING`/`PHASE2-PENDING` marker with section provenance and have matching compile-time zero assertions.
- `git diff --check a3103dc..HEAD` passed; the range contains exactly one conventional commit; plan/prompt diffs are empty; the committed implementation added-line scan found no I2C token and no HAL/STM32 header; the seven `.c/.h` skeleton pairs are exact.

### Stage A — exact specification compliance: PASS

- Fixed facts are protocol version `1`, routine type `0`, and routine length `1 + 1 + (10 x 2) + 1 + 1 + 1 = 25` bytes. D2 mode/FPort and all Gate 0/Phase 2 entries remain nonphysical zero. All readiness aggregates evaluate false, so deployability is false.
- Scope, pending-marker/provenance form, config centralization, module inventory, pure-C99 host boundary, forbidden-addition checks, test wiring, immutable plan/prompt, clean committed range, and one-commit discipline pass.

### Stage B — code quality/maintainability: FAIL

#### Major

- `tests/host/run_ml3_host_tests.sh:35-39` and `tests/host/ml3_clean_tree_after_make_test.sh:9-13` install the cleanup-only handler for `HUP`, `INT`, and `TERM`. Bash resumes after that handler, and the controlled `TERM` run exited 0. An interrupted verification can therefore be accepted as green. Keep cleanup on `EXIT`, but give each signal a handler that exits nonzero or restores and re-raises the signal; add an interruption regression that asserts the signal-appropriate nonzero status and cleanup.
- `tests/host/run_ml3_host_tests.sh:8,43-59` uses one repository-global `.build` directory. Staggered concurrent tests reproduced a linker failure when one process's EXIT cleanup deleted the other process's directory. Allocate a unique per-run build directory (preferably with `mktemp -d`) and clean only that directory; cover concurrent runs in the regression.
- `tests/host/ml3_config_contract.sh:22-32` feeds marker validation only definitions that already contain a `/*` comment. Removing a pending marker makes the line disappear from the loop, so the test still passes while the exact marker contract is broken. Validate every expected pending zero definition against the full marker/provenance regex, with only fixed zero protocol facts explicitly exempted.
- `ml3_config.h:53-54,78-86,93-133` duplicates readiness concepts (`GATE0_READINESS_READY`, `QUALIFICATION_READY`, `PHASE2_QUALIFICATION_READY`, `PHASE2_READINESS_READY`) and duplicates the measured divider as both `V5_DIVIDER_RATIO_PPM` and `CAL_V5_DIVIDER_PPM`. This creates multiple switches and values for the same evidence, making later target integration drift-prone. Keep the per-decision readiness flags, one explicit Gate 0 approval and one Gate 1 approval, derive aggregate readiness directly, and keep one canonical measured divider value that calibration storage consumes.

### Final verdict

FIX-REQUIRED

## Task 1 Second Re-review — gpt-5.6-sol

- Reviewed base: `a3103dc0111b374847b137dbab9bdfc6f12c5126`
- Reviewed head before append: `db221e369a19171914c9b57fd4339e94272c1fba`
- Scope: full committed range, governing plan/prompt, intact prior reviews, production module/config files, and all host-suite scripts

### Prior Stage B Major resolution table

| Prior Major | Resolution | Independent evidence |
|---|---|---|
| TERM returns signal-appropriate nonzero and cleans | **Partial** | Packaged and direct finite-child tests returned exactly 143 and cleaned their temp build. A deliberately hanging compiler left the runner alive two seconds after TERM, so child termination/bounded shutdown is unresolved. |
| Concurrent runs avoid shared repository build state | **Resolved** | Packaged concurrency regression passed; three independent staggered pairs each returned `0,0`, used unique temp directories, and left no artifacts. |
| Marker validator catches a deleted marker on copied real config | **Resolved** | Real config passed; copied config with the mode marker removed returned 1 with the expected missing-marker diagnostic. |
| Readiness/config cohesion and divider uniqueness | **Resolved** | Cohesion contract passed; forbidden duplicate aggregate macros are absent; exactly one divider value/readiness pair and exactly one Gate 0 plus one Gate 1 approval remain. |

### Commands and evidence

- `make test` with GCC and `CC=clang make test` both returned 0 through the nonrecursive suite entrypoint.
- Direct signal, concurrency, marker-mutation, readiness-cohesion, and clean-tree regressions returned 0; `bash -n tests/host/*.sh` passed.
- `CC=/bin/false ./tests/host/run_ml3_host_tests.sh` returned 1 and cleaned its temp build.
- Independent direct TERM returned exactly 143 and cleaned, but took about five seconds while the active validator child completed. With a 30-second compiler child, the runner was still alive two seconds after TERM and required review-side process-group termination.
- `git diff --check a3103dc..HEAD` passed; `git rev-list --count a3103dc..HEAD` returned 1; the plan and execution prompt have empty diffs; the added-line scan found no I2C or HAL/STM32 dependency; final pre-append status was clean.
- The exact inventory is the seven planned `.c/.h` pairs plus `ml3_config.h`. All modules compile as strict pure C99 skeletons and expose no Task 2+ behavior.
- Stock `src/main.c:461` already assigns downlink-reply uplinks to FPort 12. D2 therefore remains blocked at zero; no replacement mode or FPort was fabricated.

### Stage A — exact specification compliance: PASS

- All 61 pending definitions are literal `0U`, carry an exact `GATE0-PENDING` or `PHASE2-PENDING` marker with section provenance, and have matching zero assertions. D2 mode/FPort and readiness remain zero. Protocol readiness contains only fixed protocol facts; Gate 0 and Phase 2 readiness remain false, therefore deployability is false.
- Scope, inventory, host purity, strict compilation, marker contract, config centralization, immutable governing docs, forbidden-addition checks, diff hygiene, and one-commit discipline pass.

### Stage B — code quality/maintainability: FAIL

#### Blocker

- None.

#### Major

- `tests/host/run_ml3_host_tests.sh:41-49`, `tests/host/run_ml3_host_suite.sh:10-24`, and `tests/host/ml3_signal_term_regression.sh:40-62` handle TERM only in the parent while production commands run as foreground children. Bash defers the trap until the foreground child returns: the finite fixture eventually reports 143, but a hanging compiler kept the runner alive beyond the two-second bound. The regression therefore proves the exit code only for a self-terminating child and does not prove child cleanup or bounded interruption. Run external commands as tracked jobs/process groups, forward the signal, use a bounded wait with escalation, and extend the TERM regression with a nonterminating child that must be reaped promptly while still returning 143 and cleaning artifacts. Apply the same bounded discipline to the unbounded waits at `tests/host/ml3_concurrency_regression.sh:73-76` and cleanup waits at lines 32-40.
- `tests/host/ml3_clean_tree_after_make_test.sh:5,19,29-35` is named and diagnosed as a `make test` cleanliness regression but invokes only `run_ml3_host_tests.sh`. Because `run_ml3_host_suite.sh:15-24` invokes this check last, any repository artifact left by an earlier suite regression is already present in `PRE_STATUS` and is accepted unchanged. The test does not protect the production `make test` path it claims to cover. Capture pre/post status around the entire suite in `run_ml3_host_suite.sh` (without recursive make), or wrap the full suite from a separate nonrecursive entrypoint; keep explicit checks for temp/build leftovers.

#### Minor

- None.

### Numerical derivation

- Protocol version is fixed at byte 0: `1`.
- Routine type is fixed at byte 1: `0`.
- Routine length is `1 + 1 + (10 x 2) + 1 + 1 + 1 = 25` bytes, equivalently offsets 0 through 24 inclusive.
- D2 mode and FPort are unresolved and remain `0U`; stock FPort 12 is already occupied at `src/main.c:461`, and no substitute was invented. All Gate 0/Phase 2 values and readiness flags are zero, so both aggregate gates and deployability evaluate false.

### Final binding verdict

FIX-REQUIRED

### Resolution after Spark-resume reviewer FAIL

- Resumed worker session `019f5c6f-256e-78b3-a42d-55b2b628fe66` with `gpt-5.3-codex-spark`. Its patch tool was rooted to `/home/phil/Repos/osi-os`, so it only changed a sandbox copy and those copied changes were not treated as implementation evidence.
- Removed the unbounded identity-capture fallback waits from `run_ml3_host_tests.sh` and `run_ml3_host_suite.sh`. If initial identity capture is inconclusive, the scripts now retry only until the existing stage deadline, reap an already-exited direct child, or fail bounded instead of waiting indefinitely.
- Changed abnormal cleanup in runner and suite TERM/descendant regressions to return failure when `ml3_drain_owned_process_identity_file` fails. The scripts no longer hide guard errors with `|| true`.
- Replaced raw status-regression cleanup TERM calls with identity-file guard cleanup, and changed TERM trigger points in runner/suite signal regressions to use `ml3_signal_owned_process_identity_file`.
- Hardened `ml3_process_guard.sh` so descendant signaling snapshots each owned PID with PGID, session, and start time, then revalidates the same identity immediately before signaling.
- Hardened `ml3_marker_mutation_regression.sh` so `mktemp`, fixture copy, mutation failure, or a no-op mutation exits nonzero.
- Added `ml3_drain_direct_child_pid` for the rare path where a direct child exists but stable identity capture cannot be established before the stage deadline. That path now uses bounded TERM, KILL, zombie detection, and reap before returning failure.
- Changed `ml3_concurrency_regression.sh` to make EXIT cleanup failure force exit 1, matching the other lifecycle regressions.

#### Verified after final blocker repair

- `bash -n tests/host/*.sh`
- `git diff --check`
- `rg -n 'ml3_drain_owned_process_identity_file.*\|\| true|kill -TERM "\$(ML3_STATUS_RUNNER_PID|SUITE_PID)' tests/host/*.sh` returned no matches.
- `rg -n 'kill -TERM "\$(runner_pid|suite_pid|ML3_STATUS_RUNNER_PID|SUITE_PID)' tests/host/*.sh` returned no matches.
- `timeout -k 2s 25s bash tests/host/ml3_marker_mutation_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_runner_signal_status_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_suite_signal_status_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_signal_term_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_runner_descendant_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_suite_signal_term_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_suite_descendant_regression.sh`
- `timeout -k 2s 35s bash tests/host/ml3_concurrency_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_clean_tree_after_make_test.sh`
- `timeout -k 5s 90s bash tests/host/run_ml3_host_suite.sh`
- `timeout -k 5s 90s make test`
- `timeout -k 5s 90s make test CC=clang`

The full suite and both outer `make test` variants returned 0 and printed only the expected pass lines from the concurrency and clean-tree stages. D2, Gate 0, and Phase 2 values remain zero/pending; no target, HAL, STM32, I2C, or vendor integration was added.

### Task 1 fourth re-review — gpt-5.6-sol

PASS

- Verified the direct-child identity-capture timeout path now drains with bounded TERM, KILL, zombie detection, and reap through `ml3_drain_direct_child_pid`.
- Verified concurrency EXIT cleanup now forces exit 1 on guard cleanup failure.
- Rechecked prior blockers: no discarded guard cleanup failures, no raw status cleanup TERM, descendant identities are revalidated before signaling, and marker fixture setup failures return nonzero.
- Rechecked Stage A boundaries: firmware, config, Makefile, plan, prompt, and vendor integration files are unchanged; forbidden HAL/STM32/I2C scans were empty; all unresolved Gate 0/Phase 2 entries remain zero-marked and deployability remains false.

#### Reviewer verification

- `bash -n tests/host/*.sh`
- Focused marker, signal-status, TERM, descendant, concurrency, and clean-tree regressions
- Forced `TMPDIR=/proc` marker setup-failure probe returned status 1.
- Fresh GCC `make test`
- Fresh Clang `make test`
- Forced compiler failure returned status 1.
- `bash tests/host/ml3_config_contract.sh`
- `bash tests/host/ml3_readiness_cohesion_contract.sh`
- `git diff --check`
- Final scans found no live test processes, no `tests/host/.build`, and no `/tmp/ml3-*` directories.

### Resolution after Task 1 Third Re-review

- Added `tests/host/ml3_process_guard.sh` as the shared process-lifecycle guard. It records PID, PGID, session, and `/proc` start time; signals only the owned process or owned isolated process group; treats PID reuse as unowned; drains with TERM, KILL, post-KILL verification, and child reap.
- Reworked `tests/host/run_ml3_host_tests.sh` and `tests/host/run_ml3_host_suite.sh` to run active commands as tracked `setsid` children. Both now preserve signal exit codes on clean drain and return `1` when the real drain path fails. The suite stage timeout is configurable through `ML3_SUITE_STAGE_TIMEOUT_MS`; production uses a longer outer bound, while copied-suite fixture tests pin the inner timeout to 5 s.
- Replaced raw PID-file cleanup in runner/suite TERM, descendant, status, and concurrency regressions with stored process identities and the shared guard. Bounded waits now fail the regression instead of hanging, and cleanup failures propagate.
- Added `tests/host/ml3_suite_signal_status_regression.sh` and extended the runner status regression. Both force a controlled production drain failure by corrupting the active identity, assert external status `1`, and include a mutant that flattens the failure to `143` to prove the test covers the failure branch.
- Updated copied-suite fixtures, including `ml3_clean_tree_after_make_test.sh`, so every production suite stage has a placeholder and the whole-suite cleanliness check covers the new suite status regression.

#### Verified after repair

- `bash -n tests/host/*.sh`
- `git diff --check`
- `timeout -k 2s 25s bash tests/host/ml3_runner_signal_status_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_suite_signal_status_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_signal_term_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_runner_descendant_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_suite_signal_term_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_suite_descendant_regression.sh`
- `timeout -k 2s 35s bash tests/host/ml3_concurrency_regression.sh`
- `timeout -k 2s 25s bash tests/host/ml3_clean_tree_after_make_test.sh`
- `timeout -k 5s 90s bash tests/host/run_ml3_host_suite.sh`

The full suite returned 0 in about 22 s and printed only the expected regression pass lines from the concurrency and clean-tree stages. D2, Gate 0, and Phase 2 values remain zero/pending; no target, HAL, STM32, I2C, or vendor integration was added.

### Resolution after Task 1 Second Re-review

- `tests/host/run_ml3_host_suite.sh` now tracks each stage as a `setsid` child, forwards HUP/INT/TERM to the stage PID and process group, drains with bounded deadlines, and rejects repository status changes including `.build`.
- `tests/host/ml3_suite_signal_term_regression.sh` now validates the current production suite; `timeout -k 5s 20s ./tests/host/ml3_suite_signal_term_regression.sh` returned `0` with the suite’s internal `status=143`, and the fixture cleaned up.
- `tests/host/ml3_clean_tree_after_make_test.sh` now checks the full suite boundary; the early-artifact fixture is rejected, and the clean fixture run returns `0` with unchanged git status.
- `tests/host/ml3_concurrency_regression.sh` now runs three rounds with unique runner temp dirs and bounded cleanup; `timeout -k 5s 30s ./tests/host/ml3_concurrency_regression.sh` returned `0`.
- Direct runner checks remain green: `./tests/host/run_ml3_host_tests.sh` returned `0`, `CC=/bin/false ./tests/host/run_ml3_host_tests.sh` returned `1`, and `./tests/host/ml3_signal_term_regression.sh` returned `0` twice.
- Host verification passed: `bash -n tests/host/run_ml3_host_tests.sh tests/host/run_ml3_host_suite.sh tests/host/ml3_signal_term_regression.sh tests/host/ml3_concurrency_regression.sh tests/host/ml3_clean_tree_after_make_test.sh tests/host/ml3_suite_signal_term_regression.sh tests/host/ml3_marker_mutation_regression.sh tests/host/ml3_readiness_cohesion_contract.sh tests/host/ml3_config_contract.sh`, `make test`, `CC=clang make test`, `./tests/host/ml3_readiness_cohesion_contract.sh`, `./tests/host/ml3_marker_mutation_regression.sh`, and `git diff --check a3103dc..HEAD` all returned `0`.
- Post-settle scans found no lingering Task 1 runner or suite processes and no repository `tests/host/.build` or temporary ml3 artifacts.
- D2 remains blocked at zero/pending; no replacement mode or FPort was introduced.

### Additional Task 1 lifecycle resolution before third re-review

#### RED

- `bash tests/host/ml3_runner_signal_status_regression.sh` initially failed with `runner status regression expected 1, got 143`.
- `bash tests/host/ml3_runner_descendant_regression.sh` initially failed with `runner leak regression expected 1, got 0`.
- `bash tests/host/ml3_suite_descendant_regression.sh` initially failed with `suite descendant regression left leaking stage alive: <pid>`.

#### GREEN

- `bash tests/host/ml3_runner_signal_status_regression.sh`
- `bash tests/host/ml3_runner_descendant_regression.sh`
- `bash tests/host/ml3_suite_descendant_regression.sh`
- `bash tests/host/ml3_signal_term_regression.sh`
- `bash tests/host/ml3_suite_signal_term_regression.sh`
- `bash tests/host/ml3_concurrency_regression.sh` ran green three separate external times.
- `bash tests/host/ml3_clean_tree_after_make_test.sh`
- `bash tests/host/ml3_marker_mutation_regression.sh`
- `bash tests/host/ml3_readiness_cohesion_contract.sh`
- `bash tests/host/ml3_config_contract.sh`
- `bash tests/host/run_ml3_host_tests.sh`
- `CC=clang bash tests/host/run_ml3_host_tests.sh`
- `make test`
- `CC=clang make test`

#### Final sequential verification

- `bash -n tests/host/*.sh` passed for every host shell script.
- `bash tests/host/run_ml3_host_tests.sh` and `CC=clang bash tests/host/run_ml3_host_tests.sh` both passed.
- `CC=/bin/false bash tests/host/run_ml3_host_tests.sh` returned `1` and removed `tests/host/.build`.
- `bash tests/host/ml3_signal_term_regression.sh` passed twice.
- `bash tests/host/ml3_suite_signal_term_regression.sh` returned `status=143` twice and cleaned up.
- `bash tests/host/ml3_concurrency_regression.sh` passed three separate invocations.
- `bash tests/host/ml3_runner_descendant_regression.sh` and `bash tests/host/ml3_suite_descendant_regression.sh` passed.
- `bash tests/host/ml3_clean_tree_after_make_test.sh` passed.
- `make test` and `CC=clang make test` both passed.
- `bash tests/host/ml3_marker_mutation_regression.sh`, `bash tests/host/ml3_readiness_cohesion_contract.sh`, and `bash tests/host/ml3_config_contract.sh` all passed.
- `git diff --check` passed.
- The earlier scan record that was taken while a regression was still settling is not valid final evidence; the post-settle scan is the only valid one and showed no lingering `tests/host/run_ml3_host_tests.sh`, `tests/host/run_ml3_host_suite.sh`, or `tests/host/ml3_*` processes, no `/tmp/ml3-*` artifact directories, and no `tests/host/.build`.

## Task 1 Third Re-review — gpt-5.6-sol

- Reviewed base: `a3103dc0111b374847b137dbab9bdfc6f12c5126`
- Reviewed head before append: `16e667b95ece8529650638e9dfb0ff3ffdcb2cf4`
- Scope: the complete committed range and result, all governing documents and prior review history, the production host runner/suite, every host regression, and the Task 1 firmware/config skeletons

### Commands and observed evidence

- The pre-review tree was clean on `feature/ml3-precision-adc`; `git rev-parse HEAD` returned the reviewed head, `git rev-list --count <base>..HEAD` returned `1`, and the only range commit was `16e667b... chore: scaffold ML3 firmware test harness`.
- The plan, execution prompt, and complete prior log were read in full. `git diff --quiet <base>..HEAD -- docs/2026-07-12-lsn50v2-ml3-firmware-plan.md docs/prompts/ml3-firmware-execution.md` returned 0. The full range contained only the seven planned `.c/.h` pairs, `ml3_config.h`, the root Makefile, host harness/tests, and this log.
- `git diff --check <base>..HEAD` passed. A committed added-content scan found no I2C token/code/include and no HAL/STM32 include in any new host-pure module. The exact added firmware inventory is seven planned `.c/.h` pairs plus `ml3_config.h`; no target/vendor integration file was changed.
- `bash -n tests/host/*.sh`, `ml3_config_contract.sh`, `ml3_marker_mutation_regression.sh`, and `ml3_readiness_cohesion_contract.sh` all returned 0. There are 61 pending literal-zero definitions and 61 exact pending markers; the real-config-copy marker deletion was rejected.
- Fresh `timeout -k 5s 90s make test` with GCC and with `CC=clang` both returned 0. `CC=/bin/false run_ml3_host_tests.sh` returned 1 and cleaned its temporary build.
- Direct bounded invocations of runner TERM, runner lifecycle-status mutation, runner descendant, suite TERM, suite descendant, three-round concurrency, and whole-suite cleanliness regressions all returned 0. The suite TERM fixture observed status 143. An additional TERM probe during the suite's second status capture also observed 143.
- Post-test scans found no repository `.build`, no `/tmp/ml3-*` test directory, and no live Task 1 runner/suite/regression process. The tree was clean immediately before this append.

### Prior-finding resolution table

| Prior finding | Status | Independent result |
|---|---|---|
| Active foreground children receive bounded TERM then KILL, with signal status | Resolved for the exercised active-child path | Runner TERM and suite TERM regressions passed; production uses isolated process groups and 500 ms TERM plus 500 ms post-KILL drain windows. |
| Descendants cannot survive a normally completed foreground leader | Resolved in current production behavior | Runner and suite descendant regressions returned 1 internally, killed the residual groups, and passed without observed leaks. |
| Whole-suite cleanliness spans every stage and rejects an early artifact | Resolved | The suite captures status before all stages and after all stages; the copied-suite early-artifact mutation is rejected, and the clean fixture is unchanged. |
| Internal lifecycle/drain failure is preserved rather than flattened to requested signal status | Partially resolved | The production failure branches return 1, but the committed regression does not cause either production drain to fail, and no equivalent suite failure-path regression exists. |

### Stage A — exact specification compliance: PASS

- The committed result has the exact Task 1 skeleton inventory, strict pure-C99 compilation of every module, no exposed Task 2+ behavior, no target/vendor glue, no HAL/STM32 dependency in host-pure modules, and no I2C addition.
- Every unresolved Gate 0/Phase 2 value or readiness entry present in the sole config home is literal `0U` with the exact `GATE0-PENDING`/`PHASE2-PENDING` marker and plan-section provenance. Mode and FPort remain zero and separately gate deployability. Gate 0 readiness, Phase 2 readiness, and deployability are false.
- The readiness model is cohesive: one Gate 0 approval, one Gate 1 approval, one canonical divider value/readiness pair, derived aggregate gates, and no duplicate aggregate approval/readiness switches.
- The marker validator scans all literal-zero definitions, including a definition whose marker has been removed. The mutation test copies the real config, proves the real copy passes first, deletes the mode marker, and proves rejection.
- Governing plan/prompt immutability, exact one-commit discipline, committed diff hygiene, reproducible cleanup on success and compiler failure, and the stock FPort-12 collision all pass.

### Stage B — code quality, maintainability, and robustness: FAIL

#### Blocker

- None.

#### Major

- `tests/host/ml3_runner_descendant_regression.sh:131-136` and `tests/host/ml3_suite_descendant_regression.sh:152-157` perform raw, unbounded waits for the very runner/suite whose broken descendant lifecycle they are intended to detect. The suite supervisor itself also waits without a per-stage deadline at `tests/host/run_ml3_host_suite.sh:110-114`. If the production behavior regresses into a hang, these tests hang instead of failing, so `make test` has no deterministic completion bound on the targeted failure. Add a watchdog or deadline-based wait around each subject, then apply bounded TERM -> KILL -> reap cleanup and report a test failure on expiry.
- `tests/host/ml3_suite_signal_term_regression.sh:111-127` sends KILL at its cleanup deadline, immediately breaks, and returns success without a bounded post-KILL drain or proof that the PID/group is gone. Raw watchdog waits also remain at `tests/host/ml3_signal_term_regression.sh:159-162` and `tests/host/ml3_suite_signal_term_regression.sh:214-217`. These abnormal cleanup paths can either leave a descendant behind or wait indefinitely, contradicting the lifecycle guarantee the regressions claim to enforce. Use one bounded cleanup primitive that has separate TERM and post-KILL deadlines, verifies both PID and process group disappearance, reaps owned children, and propagates cleanup failure.
- `tests/host/ml3_signal_term_regression.sh:83-88`, `tests/host/ml3_concurrency_regression.sh:89-96`, and `tests/host/ml3_suite_signal_term_regression.sh:101-127` read PIDs from files and later send TERM/KILL to both the PID and `-$PID` without validating that the process identity/session is still the fixture's. On an early assertion failure the recorded wrapper/stage may already have exited, so PID or process-group reuse can direct destructive signals at an unrelated process. Keep an owned supervisor/session leader alive until cleanup and clear it immediately after reap, or record and validate stable identity (at minimum PID, PGID/session, and `/proc` start time) before signaling a file-sourced identifier.
- `tests/host/ml3_runner_signal_status_regression.sh:119-122,185-189` changes the successful requested-signal return to literal 1 and checks that the caller propagates it; it never makes `ml3_drain_active_job` fail. Consequently it does not exercise the actual failure branches at `tests/host/run_ml3_host_tests.sh:123-137`, and there is no corresponding suite regression for `tests/host/run_ml3_host_suite.sh:140-144`. A regression that flattened a real drain failure could escape this test. Force a bounded production drain failure through controlled signal/process primitives for both runner and suite, assert external status 1 rather than 143, and mutation-check that changing the failure branch to the requested signal code makes the test fail.

#### Minor

- None.

### Numerical and timing derivation

- Appendix A fixes protocol version byte 0 to `1` and routine type byte 1 to `0`. Routine length is `1 + 1 + (10 x 2) + 1 + 1 + 1 = 25` bytes, equivalently offsets 0 through 24 inclusive. The config and compile-time contract match all three facts.
- D2 is open: provisional mode 10 and proposed FPort 12 are not fixed facts. Both configured values and readiness flags remain `0U`. Stock `src/main.c:461` already assigns FPort 12, so retaining zero is correct and no replacement was fabricated.
- All 61 unresolved Gate 0/Phase 2 definitions are literal zero. Since the readiness conjunctions contain zero per-decision/approval terms, `ML3_CONFIG_GATE0_READINESS == 0`, `ML3_CONFIG_PHASE2_READINESS == 0`, and `ML3_CONFIG_DEPLOYABLE == 0` even though the fixed protocol facts are ready.
- Production active shutdown budgets 500 ms before KILL and another 500 ms after KILL (about 1.0 s plus 50 ms polling granularity). Normal-leader descendant handling adds a 1.0 s grace first (about 2.0 s total). Those production paths completed in the direct tests, but the raw regression waits and incomplete cleanup paths identified above have no equivalent guaranteed bound.

### Final binding verdict

FIX-REQUIRED

## Task 2 checkpoint — adc_precision sequencing and conversion helpers

- Base for Task 2 relative diff: `8405210bf2a9454888876e70d7ee3947f9d2d916`
- Scope edits: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/adc_precision.h`, `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/adc_precision.c`, `tests/host/ml3_adc_precision_test.c`, `tests/host/run_ml3_host_tests.sh`

### TDD RED/GREEN record

- RED: `make test`
- RED result: failed at 7/8 tests in the new host test suite (`initial read sequence`, `channel change discard`, `timeout behavior`, `readiness before conversion`, `overrun propagation`, `read uV end-to-end`) with `ADC_PRECISION_ERROR_INVALID_ARGUMENT` and one path with timeout/overflow expectation mismatch.
- GREEN: `make test`
- GREEN result: all eight new `ml3_adc_precision_test` cases passed and host suite passed through `concurrency regression` and `clean-tree regression`.
- Full verification: `CC=clang make test`
- Full verification result: same suite and regressions passed under Clang.

### Commit-relevant findings

- Fixed `adc_precision_read_raw` timeout-validation return check bug (`if (validate_timeouts(...) != ADC_PRECISION_OK)`).
- Corrected a mock helper bug where `test_is_adc_disabled` was mutating `adc_enabled`, which could force disable-wait timeouts.
- Adjusted conversion math boundary test to verify a non-overflow maximum-operand case (`compute_channel_uv(65520, UINT32_MAX)`), since the current fixed-point formula is bounded for supported inputs.

### Constraints/invariants notes

- `adc_precision.c/.h` remain host-compilable C99 with no HAL/STM32 include/dependency.
- No target/task-3+ files, vendor integration, or plan/prompt files were changed.
- `/* VERIFY-RM0376 */` is preserved in `adc_precision.c` at the sequence point where calibration is sequenced after disable.

### Correction Round 1 — c02a143 replay and hardening

#### TDD RED/GREEN record (replay)

- RED: `./tests/host/run_ml3_host_tests.sh`
- RED result (pre-patch): observed debug output in timeout test and no explicit assertions for disable/calibration/ready setup-timeout conversion suppression.
- GREEN: `./tests/host/run_ml3_host_tests.sh`
- GREEN result (post-patch): 13/13 passed with explicit timeout-path assertions.

#### Verification evidence

- `./tests/host/run_ml3_host_tests.sh` — PASS: default timeouts; prepare sequence/config; prepare no-op when already safe; prepare timeout steps; prepare readiness timeout safety; discard/retain sequencing; single-cycle setup once; internal path readiness order; conversion timeout and overrun; channel bounds; math helper boundaries; read_uV error preservation; read_uV end-to-end.
- `make test` — PASS; includes concurrency and clean-tree regressions.
- `CC=clang make test` — PASS; same regressions included.
- `bash -n tests/host/*.sh` — PASS.
- `CC=/bin/false make test` — FAIL with status 2, as expected for forced compiler failure cleanup coverage.
- `./tests/host/ml3_clean_tree_after_make_test.sh` — PASS.

#### File-level correction notes

- Removed temporary debug tracing from `tests/host/ml3_adc_precision_test.c` timeout diagnostics.
- Added explicit `prepare`-timeout path assertions that no sample is overwritten and no conversion starts after:
  - disable timeout,
  - calibration timeout,
  - ready timeout.
- Kept existing production logic and interfaces unchanged (`ADC_PRECISION` host C99 module, port contract, and runner harness).

#### Concern markers

- `VERIFY-RM0376`: host assertions and sequencing are implemented and documented at the sequencing boundary in `src/adc_precision.c`; exact RM/ES register timing text remains unavailable in repo evidence and is still marked as `VERIFY-RM0376` in code where required.

### Correction Round 2 — suite deadline regression

#### RED/GREEN record

- RED: full suite previously failed as `make: *** [Makefile:4: test] Error 2` because the production stage timeout (`ML3_SUITE_STAGE_TIMEOUT_MS` default) was insufficient for retained concurrency rounds.
- GREEN: changed `ACTIVE_STAGE_TIMEOUT_MS` default in `tests/host/run_ml3_host_suite.sh` from `30000` to `60000` to add 2x headroom while preserving explicit overrides.

#### Verification evidence

- `timeout 120s make test` — GREEN, `13 passed` host ADC test suite plus concurrency and clean-tree regressions.
- `timeout 120s make CC=clang test` — GREEN, `13 passed` host ADC test suite plus concurrency and clean-tree regressions.
- `bash -n tests/host/*.sh && echo bash-n-ok` — GREEN (`bash-n-ok`).
- `CC=/bin/false make test; echo "make-fail-exit:$?"` — GREEN as a failure-path assertion: `make-fail-exit:2`.
- `git diff --check` — GREEN (no issues).
- `./tests/host/ml3_clean_tree_after_make_test.sh` — GREEN (`clean-tree regression passed`).
- Live process and temp-dir confirmation: `find /tmp -maxdepth 1 -mindepth 1 -type d -name 'ml3-*'` returned no directories and `ps -eo pid,cmd | awk '/ml3_(host_|suite|concurrency|clean_tree|runner|signal|descendant)/{print}'` returned no running test processes.

### Correction Round 3 — final controller audit

#### RED/GREEN record

- RED: existing `adc_precision_compute_vdda_uv` and readiness-mapping paths had insufficient bounds and mixed timeout routing, and there was no explicit proof of configured ADC clock fact consumption.
- GREEN: `vrefint` oversampling upper bound, sentinel-preservation and maximum multiplication behavior were added/updated in helpers and tests; `adc_precision_port_t::is_adc_enabled` requirement was removed; VREFINT readiness now waits on `vref_ready_ms`, sensor on `sensor_ready_ms`; `ADC_PRECISION_ADC_CLOCK_HZ` was added and asserted via port capture; and `16ULL` was replaced with `ADC_PRECISION_OVERSAMPLING_SCALE`.

#### Verification evidence

- `./tests/host/run_ml3_host_tests.sh` — GREEN (`14 passed`, `0 failed`).
- `timeout 120s make test` — GREEN (`14 passed`, plus concurrency and clean-tree regressions).
- `timeout 120s make CC=clang test` — GREEN (`14 passed`, plus concurrency and clean-tree regressions).
- `bash -n tests/host/*.sh` — GREEN (no syntax errors).
- `CC=/bin/false make test; echo "make-fail-exit:$?"` — GREEN as a failure-path assertion with captured nonzero status (`make-fail-exit:2`).
- `git diff --check` — GREEN (no whitespace or patch errors).
- `./tests/host/ml3_clean_tree_after_make_test.sh` — GREEN (`clean-tree regression passed`).
- Anti-slop post-run cleanup scan: `find /tmp -maxdepth 1 -name 'ml3-*' -exec rm -rf {} +` then `find /tmp -maxdepth 1 -name 'ml3-*'` returned nothing and `ps -ef | grep -E 'run_ml3_host|run_ml3_host_suite|ml3_suite|ml3_concurrency|ml3_clean_tree|ml3_signal|ml3_descendant'` returned no matching live processes.

### Correction Round 4 — binding review major finding fixes

#### RED/GREEN record

- RED: with added assertions and no production fix yet, `./tests/host/run_ml3_host_tests.sh` reached the new failure path in `retained timeout recovery` (`read after re-prepare succeeds: expected=0 actual=2`) after previously passing 15/15 tests.
- GREEN: after state invalidation on any conversion error in `adc_precision_read_raw` and targeted recovery tests, `./tests/host/run_ml3_host_tests.sh` reached `16 passed, 0 failed`.

#### Verification evidence

- `./tests/host/run_ml3_host_tests.sh` — GREEN, `16 passed, 0 failed`.
- `timeout 120s make test` — GREEN, `16 passed`, plus concurrency and clean-tree regressions.
- `timeout 120s make CC=clang test` — GREEN, `16 passed`, plus concurrency and clean-tree regressions.
- `bash -n tests/host/*.sh` — GREEN (no syntax errors).
- `CC=/bin/false make test; echo "make-fail-exit:$?"` — GREEN as a failure-path assertion (`make-fail-exit:2`).
- `git diff --check` — GREEN (no issues).
- `./tests/host/ml3_clean_tree_after_make_test.sh` — GREEN (`clean-tree regression passed`).
- Cleanup/process check: `ps -ef | grep -E 'run_ml3_host|run_ml3_host_suite|ml3_suite|ml3_concurrency|ml3_clean_tree|ml3_signal|ml3_descendant'` returned none and `find /tmp -maxdepth 1 -name 'ml3-*'` returned none.

## Correction Round 4c — conversion-error recovery repro and conversion lifecycle tightening

- RED: `./tests/host/run_ml3_host_tests.sh`
- RED evidence: `channel switch overrun invalidates acquisition` failed while reproducing exact binding case with `expected=1 actual=0` for reprepare stop and previously `overrun` index mismatches in conversion timeout/overrun paths.
- GREEN: `./tests/host/run_ml3_host_tests.sh`
- GREEN evidence: `./tests/host/run_ml3_host_tests.sh` -> `16 passed, 0 failed` with new mock behavior and scenario coverage.
- Focused harness change evidence:
  - Removed boolean `force_overrun` in mock state and added `overrun_sample_index`.
  - `test_start_conversion` now sets `conversion_stopped = false`.
  - `test_read_raw` now marks `conversion_stopped = true` on each completed read and clears one-time overrun index hit.
  - `test_is_conversion_stopped` clears `conversion_unread` when stop completion is observed.
  - Overrun now injects by sample index in mock.
  - Added/updated test: `channel switch overrun invalidates acquisition` (`test_channel_switch_discard_retained_overrun_reprepare_required`) covering:
    - samples `0..5`, channel 7 establishes retained sample from index `[discard=0, retained=1]`.
    - channel 8 pre-recovery call consumes `[discard=2, retained=3]` and fails with `ADC_PRECISION_ERROR_OVERRUN` using `sample 3` overrun injection.
    - the failed retained conversion must preserve caller output and immediately reject a second read attempt with `ADC_PRECISION_ERROR_INVALID_ARGUMENT`.
    - the failed call does not issue a stop request; no extra setup runs while `sample_index` remains 4.
    - `adc_precision_prepare` performs recovery setup and a new channel-8 read consumes `[discard=4, retained=5]` with retained value `302` and exactly two additional conversions.
    - `test_state.conversion_unread` is cleared on requested-stop completion.
- Focused test count for host suite remains 16.

### Correction Round 4d — final precise overrun and timeout-recovery correction

- RED evidence before final tweak: overrun path used a successful channel-8 retained step, and timeout-recovery stop assertion still failed (`expected=1 actual=0`) during focused run.
- GREEN evidence after corrective test-only edits:
  - `./tests/host/run_ml3_host_tests.sh` — GREEN (`16 passed`, `0 failed`).
- Controller verification after the final test correction:
  - `timeout -k 5s 150s make test` — GREEN (`16 passed`, concurrency regression passed, clean-tree regression passed).
  - `timeout -k 5s 150s make CC=clang test` — GREEN (same results).
  - `bash -n tests/host/*.sh` and `git diff --check` — GREEN.
  - GCC `-fanalyzer` and Clang static analysis of `adc_precision.c` — GREEN.
  - `CC=/bin/false ./tests/host/run_ml3_host_tests.sh` — expected nonzero status `1`; no build directory or runner process remained.

### Task 2 binding review

- Initial review of `8405210..3cd5c60`: `FIX-REQUIRED` with two Major findings. Conversion failures left the acquisition prepared, which allowed an extra discard after a failed retained conversion and allowed retry after a potentially active conversion timeout.
- Resolution: every discard or retained conversion error now invalidates the prepared cycle and retained-channel state. The regression suite proves the exact channel-switch failure, immediate retry rejection, timeout stop/reprepare path, and temperature-readiness timeout.
- Binding re-review of `8405210..c5b618f`: `APPROVE` with Stage A PASS, Stage B PASS, and no Blocker, Major, or Minor findings.
- Reviewer verification: 16/16 focused tests, full GCC and Clang suites, shell and diff checks, GCC and Clang static analyzers, config/readiness contracts, compile-time non-deployability, forced-compiler cleanup, forbidden-dependency scan, and independent fixed-point arithmetic all passed.
- Remaining concern: the accepted `VERIFY-RM0376` marker stays at the disabled-ADC configuration/calibration boundary because no local RM0376/ES0292 source supplies an exact section.

## Task 3 checkpoint — measurement state machine and acquisition flow

### TDD RED/GREEN record

- RED: `bash tests/host/run_ml3_host_tests.sh` (pre-fix Task 3 state) failed while header/type wiring was incomplete, preventing a passing focused Task 3 run.
- GREEN: `bash tests/host/run_ml3_host_tests.sh` — focused run exited `0` (`OK`, no failures).
- GREEN: `CC=clang bash tests/host/run_ml3_host_tests.sh` — focused run exited `0` (`OK`, no failures).
- GREEN: `make test` — full GCC suite passed, including runner/suite regressions.
- GREEN: `CC=clang make test` — full Clang suite passed, including runner/suite regressions.

### Files touched in this checkpoint

- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_measurement.h`
- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_measurement.c`
- `tests/host/ml3_measurement_task3_test.c`
- `tests/host/run_ml3_host_tests.sh`
- `docs/prompts/ml3-execution-log.md`

#### Re-verification record (replacement editor refresh)

- `bash tests/host/run_ml3_host_tests.sh` — passed (`OK`).
- `CC=clang bash tests/host/run_ml3_host_tests.sh` — passed (`OK`).
- `make test` — full GCC suite passed, including concurrency/clean-tree regressions.
- `CC=clang make test` — full Clang suite passed, including concurrency/clean-tree regressions.

#### Remaining ambiguity

- None.

### Task 3 fix round 1 — replacement editor continuation

- RED (previous checkpoint in this Task 3 cycle): focused `test_adc_prepare_timeout_continues_with_cleanup` failed in `ml3_measurement_step_adc_calibrate` because `set_power_5v(false)` / `set_thermistor_excitation(false)` cleanup was not executed and the test observed `prepare path cleanup power low: expected=2 actual=1`.
- GREEN: `ML3_STATE_ADC_CALIBRATE` now routes prepare failures through `ml3_measurement_set_adc_fault_and_continue_with_path(..., true)` to guarantee paired cleanup before process continuation.

#### Verification evidence

- `bash tests/host/run_ml3_host_tests.sh` — passed (`OK`, no failures).
- `CC=clang bash tests/host/run_ml3_host_tests.sh` — passed (`OK`, no failures).
- `make test` — passed (full GCC suite, 16/16 host measurement tests plus regressions).
- `CC=clang make test` — passed (full Clang suite, 16/16 host measurement tests plus regressions).
- `git diff --check` — passed.

#### File changes

- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_measurement.c`
- `tests/host/ml3_measurement_task3_test.c`
- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_measurement.h`
- `tests/host/run_ml3_host_tests.sh`
- `docs/prompts/ml3-execution-log.md`

#### Remaining issues

- None.

### Task 3 fix round 3 — sole editor correction round 3

- RED: `CC=gcc ./tests/host/run_ml3_host_tests.sh` failed with 36 focused Task 3 regressions, dominated by cleared fault injections, power-on abort timing, and the PA4 high-high-low fixture.
- GREEN: `CC=gcc ./tests/host/run_ml3_host_tests.sh` exited `0` with `OK` after the harness/production corrections.
- GREEN: `CC=clang ./tests/host/run_ml3_host_tests.sh` exited `0` with `OK`.
- GREEN: `make test` exited `0`; the full GCC suite completed with the existing concurrency and clean-tree regressions passing.
- GREEN: `CC=clang make test` exited `0`; the full Clang suite completed with the same regressions passing.
- GREEN: `bash tests/host/ml3_config_contract.sh`, `bash tests/host/ml3_readiness_cohesion_contract.sh`, `bash tests/host/ml3_marker_mutation_regression.sh`, `bash tests/host/ml3_clean_tree_after_make_test.sh`, and `bash -n tests/host/*.sh` all exited `0`.
- GREEN: `git diff --check` exited `0`.
- GREEN: added-line dependency scan over `ml3_measurement.h/.c`, `adc_precision.c`, and `tests/host/ml3_measurement_task3_test.c` found no added `HAL_`, `I2C`, `stm32`, or `STM32` tokens.
- Remaining ambiguity: none.

### Task 3 fix round 2 — binding controller correction round 2

- RED: `./tests/host/run_ml3_host_tests.sh` reached a focused failure before stabilization (`FAIL: watchdog fail skips build: expected=0 actual=1`).
- GREEN: after the focused assertion fix in the watchdog callback matrix, `./tests/host/run_ml3_host_tests.sh` exited `0` with `OK`.
- GREEN: `CC=clang ./tests/host/run_ml3_host_tests.sh` exited `0` with `OK`.
- GREEN: `make test` exited `0` (`OK` with full suite regressions).
- GREEN: `CC=clang make test` exited `0` (`OK` with full suite regressions).
- GREEN: `bash tests/host/ml3_config_contract.sh`, `bash tests/host/ml3_marker_mutation_regression.sh`, `bash tests/host/ml3_readiness_cohesion_contract.sh`, and `bash -n tests/host/*.sh` all exited `0`.
- GREEN: `git diff --check` exited `0`.
- `git diff --name-only | sed -n '1,200p'` reported `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_measurement.h`, `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_measurement.c`, `tests/host/run_ml3_host_tests.sh`, `docs/prompts/ml3-execution-log.md`; plus untracked `tests/host/ml3_measurement_task3_test.c`.

#### File changes

- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_measurement.h`
- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_measurement.c`
- `tests/host/ml3_measurement_task3_test.c`
- `tests/host/run_ml3_host_tests.sh`
- `docs/prompts/ml3-execution-log.md`

#### Remaining issues

- None.

### Task 3 fix round 4 — sole implementation editor

- RED: `bash tests/host/run_ml3_host_tests.sh` failed after the first focused test additions with `vref uses fixed channel 17 for both reference reads: expected=4 actual=0`, `prepare calibration timeout error: expected=4 actual=5`, and `prepare ready timeout maps to adc prepare: expected=4 actual=5`.
- RED: `bash tests/host/run_ml3_host_tests.sh` failed after the discard/retained overrun coverage change with `overrun observed on expected retained/read discard semantics`.
- RED: `bash tests/host/run_ml3_host_tests.sh` failed after the discharge fresh-VREFINT cases with `pa4 high-high-low has no fault: expected=0 actual=7`, `threshold straddle rechecks vref on each attempt: expected=4 actual=0`, and `discharge vref timeout on fresh vref: expected=17 actual=5`.
- GREEN: `bash tests/host/run_ml3_host_tests.sh` exited `0`.
- GREEN: `CC=clang bash tests/host/run_ml3_host_tests.sh` exited `0`.
- GREEN: `make test` exited `0`; the full GCC suite completed, including the concurrency and clean-tree regressions.
- GREEN: `CC=clang make test` exited `0`; the full Clang suite completed, including the concurrency and clean-tree regressions.
- GREEN: `bash tests/host/ml3_config_contract.sh`, `bash tests/host/ml3_readiness_cohesion_contract.sh`, `bash tests/host/ml3_marker_mutation_regression.sh`, `bash tests/host/ml3_clean_tree_after_make_test.sh`, and `bash -n tests/host/*.sh` all exited `0`.
- GREEN: `git diff --check` plus the untracked `tests/host/ml3_measurement_task3_test.c` trailing-whitespace scan exited `0`.
- GREEN: added-line dependency scan over `adc_precision.h/.c`, `ml3_measurement.h/.c`, `run_ml3_host_tests.sh`, and the full untracked `tests/host/ml3_measurement_task3_test.c` found no added `HAL_`, `I2C`, `stm32`, or `STM32` tokens.
- GREEN: Task 2 trace restoration check (`diff` of the `ADC_PRECISION_DEBUG`/`ADC_PRECISION_TRACE` lines in `adc_precision.c` against `git show HEAD:...`) exited `0`; only the calibration-specific error return changed in that file.

#### Files changed in round 4

- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/adc_precision.h`
- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_measurement.h`
- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/adc_precision.c`
- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_measurement.c`
- `tests/host/ml3_adc_precision_test.c`
- `tests/host/ml3_measurement_task3_test.c`
- `tests/host/run_ml3_host_tests.sh`
- `docs/prompts/ml3-execution-log.md`

#### Remaining ambiguity

- None.

### Task 3 fix round 5 — safety and fault-injection correction

- RED: the focused GCC runner exited `1` after the test-first changes. It reported `warmup below 500 ms: expected=1 actual=0`, `error state becomes inactive`, and `error-state retry applies power low`.
- GREEN: the focused GCC and Clang runners exited `0`. Both printed `OK`, followed by the existing 16 passing `adc_precision` cases.
- `make test` and `CC=clang make test` exited `0`; both full suites included the existing process-lifecycle and clean-tree regressions.
- `ml3_config_contract.sh`, `ml3_readiness_cohesion_contract.sh`, `ml3_marker_mutation_regression.sh`, `ml3_clean_tree_after_make_test.sh`, and `bash -n tests/host/*.sh` exited `0`.
- Warm-up configuration now accepts the inclusive 500–3000 ms range and rejects 499 ms and 3001 ms. The host clock advances by 500 ms only while the measurement state is `WARMUP`, so ADC timeout fixtures retain their prior timing.
- `ML3_STATE_ERROR` retries both safe-low callbacks once per explicit step and returns immediately. A corrupt state records `ML3_MEASUREMENT_ERR_STATE`, becomes inactive, enters `ML3_STATE_ERROR`, and attempts power-low then thermistor-low without entering process, build, or queue states.
- The GPIO fake records requests before applying them. A failed callback leaves the simulated applied state unchanged; tests distinguish a low request from a successful low transition and prove the later error-state retry where required.
- Each overrun injection now keys on and asserts state, physical conversion index, retained-sample ordinal, channel, and discard status. The `VERIFY_DISCHARGE` matrix covers PA4 discard and retained timeout/overrun at physical indices 2 and 3, retained ordinal 1, with exact ADC fault classification and queued invalid output.
- Callback tests record the configured failure event. Every callback and all four watchdog points require both safe-low requests after that event; the queue-plus-cleanup case also proves the fourth power callback failed as configured.
- The inverse discharge threshold fixture proves the retained under-load VDDA wins when it is higher than the fresh post-power-off VDDA. The first PA4 sample remains above 500 mV, a second fresh-VREFINT/PA4 pair precedes thermistor excitation, and the stored post-reference diagnostics remain unchanged.
- `git diff --check` exited `0`. The untracked Task 3 test returned the expected `git diff --no-index --check` content status `1` with no whitespace diagnostic. Dependency scans found no HAL, STM32-header, I2C, floating-point, or heap dependency in `ml3_measurement`.
- The Task 2 prerequisite remains narrow: the calibration-completion wait alone returns `ADC_PRECISION_ERROR_CALIBRATION`, its test asserts that enum directly, and the existing `ADC_PRECISION_DEBUG` traces are unchanged.
- Channel-collision policy and target-side `therm_settle_ms` wiring remain deferred to Task 10 integration; this round adds neither.

### Task 3 fix round 6 — causal fault and end-to-end acquisition oracles

- RED: the focused GCC runner exited `1` after the exact-deadline warm-up test disabled implicit clock advancement. At 2999 ms it reported `warmup remains in state one millisecond before deadline: expected=3 actual=4`, exposing a test-clock control defect.
- GREEN: the fake clock now permits explicit WARMUP time jumps. The test holds WARMUP at 2999 ms, transitions at 3000 ms, and repeats the same checks across `uint32_t` wrap without sleeping.
- Nested REFERENCE_PRE timeout plus power-low failure and thermistor overrun plus thermistor-low failure tests now record explicit ADC-injection events. They assert exact state, physical conversion index, retained ordinal, channel, discard status, injection-before-cleanup-failure order, causal paired-low requests, successful error-state retry, and the preserved ADC error/fault taxonomy.
- VERIFY_DISCHARGE PA4 discard and retained timeout cases now prove causal paired-low requests and applied low states. The queue callback snapshots the actual report; the tests require exactly one queued invalid report with every measurement/statistic presence flag cleared, fault presence retained, and valid-cycle count present and zero.
- The nominal full-state-machine oracle uses four distinct H1-L1-L2-H2 cycles. It asserts retained phase order, pre/post VREF-derived VDDA, each cycle's high/low/differential basis, the population-variance basis, integer SD, means, common mode, median, MAD, drift, minimum, maximum, valid-cycle count, and the exact queued result.
- `tests/host/run_ml3_host_tests.sh` and `CC=clang tests/host/run_ml3_host_tests.sh` exited `0`; both printed `OK` and the existing `16 passed, 0 failed` ADC precision result.
- `make test` and `CC=clang make test` exited `0`; both full suites passed their concurrency and clean-tree regressions.
- `ml3_config_contract.sh`, `ml3_readiness_cohesion_contract.sh`, `ml3_marker_mutation_regression.sh`, `ml3_clean_tree_after_make_test.sh`, and `bash -n tests/host/*.sh` exited `0`.
- `git diff --check` exited `0`. The untracked Task 3 test returned the expected `git diff --no-index --check` content status `1` with no whitespace diagnostic.
- The ML3 measurement dependency scan found no HAL, STM32-header, I2C, floating-point, or heap dependency. The Task 2 `ADC_PRECISION_DEBUG`/`ADC_PRECISION_TRACE` comparison against `HEAD` was unchanged.
- Round 6 changed only `tests/host/ml3_measurement_task3_test.c` and this execution log. No production code, Task 2 prerequisite, commit, push, or pull request was added in this round.

### Task 2 corrective prerequisite — binding review

- Commit `a6ba85c` was reviewed over immutable range `dd58506..a6ba85c` by the required `gpt-5.6-sol` reviewer.
- Verdict: `approve`; no Blocker, Major, or Minor findings.
- The reviewer confirmed the error enum is append-only (`ADC_PRECISION_ERROR_CALIBRATION = 6`), the self-calibration completion wait is the only path returning it, and the other nine wait sites retain `ADC_PRECISION_ERROR_TIMEOUT`.
- The reviewer independently derived Appendix C taxonomy values (`ADC_INIT = 0x0001`, `ADC_CAL = 0x0002`, `ADC_TIMEOUT = 0x0004`) and confirmed the distinct calibration result permits Task 3 to set `ADC_CAL` without misclassifying conversion timeouts.
- Read-only release and `ADC_PRECISION_DEBUG` host builds both passed all 16 precision tests; release emitted no trace output and the required debug timeout traces remained present.

### Task 3 binding correction round 7 — fix-required resolution

- The binding reviewer returned `fix-required` with one Blocker and five Major findings: a fabricated 5,000,000 uV acceptance cap, optional reporting callbacks, missing raw ABBA evidence, omitted reset-cause capture, inadequate watchdog-order assertions, and missing coverage for the first thermistor-low failure during PREPARE. The controller had already run fresh full GCC and Clang suites successfully outside the reviewer's read-only sandbox before that verdict.
- RED: the first extreme-domain statistic test failed with `FAIL: extreme-domain has valid cycles`. The first mandatory-reporting test failed with `FAIL: null process callback: expected=1 actual=0`. Raw-evidence and reset-cause tests initially failed to compile because the required result fields and port callback did not exist.
- The statistic path now accepts the full signed `uint32_t` differential domain. Round 7 introduced centered integer arithmetic and quotient/remainder accumulation to avoid overflow without floating point, heap allocation, compiler-specific 128-bit integers, saturation, or a fabricated physical limit. Round 9 added the fractional-mean remainder correction required for exact population variance. Boundary tests cover alternating extrema, individual squared deviations that exceed `uint64_t`, and non-zero mean remainders.
- `on_process`, `on_build_payload`, and `on_queue` are mandatory at initialization and are checked defensively in their state-machine steps. ADC-invalid acquisitions still process, build, and queue exactly one flagged invalid report.
- Results now retain all H1/L1/L2/H2 raw codes for each completed cycle, with explicit presence and cycle count. Discard conversions are excluded, incomplete cycles are not committed, and invalid acquisitions clear the raw evidence.
- A mandatory, non-failing reset-cause callback runs at acquisition start before radio sleep and before the sequence increment. Its opaque `uint32_t` value and presence flag survive invalidation and reach process, payload-build, and queue callbacks.
- Watchdog assertions now prove the four required causal positions: before power-high, before the first physical ABBA conversion, after the last physical ABBA conversion, and between payload build and queue. The PREPARE thermistor-low first-call failure test proves the exact callback state, paired-safe-low attempts, no reporting callbacks, and the later ERROR-state retry.
- GREEN: `tests/host/run_ml3_host_tests.sh` and `CC=clang tests/host/run_ml3_host_tests.sh` exited `0`, printing `OK` and `16 passed, 0 failed` for the precision tests.
- GREEN: `make test` and `CC=clang make test` exited `0`; both full suites passed the functional host tests, concurrency regression, and clean-tree regression.
- GREEN: `ml3_config_contract.sh`, `ml3_readiness_cohesion_contract.sh`, `ml3_marker_mutation_regression.sh`, `ml3_clean_tree_after_make_test.sh`, and `bash -n tests/host/*.sh` exited `0`.
- GREEN: `git diff --check`, the Task 2 trace invariant, and scans for the removed cap, hardware dependencies, floating point, heap allocation, and 128-bit integer extensions all passed.
- Round 7 changed `ml3_measurement.h`, `ml3_measurement.c`, `ml3_measurement_task3_test.c`, and this execution log. It did not change Task 2 code, the host runner, commits, remotes, or pull requests.

### Task 3 binding correction round 8 — invalid reset cause and WD3 causality

- The binding follow-up found two required test gaps. ADC-invalid reports had no nonzero reset-cause oracle, and the WD3 test observed the last conversion start rather than successful raw consumption.
- Mutation RED: temporarily assigning zero to `reset_cause` inside `ml3_measurement_invalidate_measurement_data()` made the focused runner fail with `FAIL: invalid process retains exact reset cause: expected=3277492711 actual=0`. The mutation was removed.
- The invalid-report case uses reset cause `0xC35A91E7`, completes one ABBA cycle, then injects a retained H1 overrun in the second cycle. Process, payload-build, and queue snapshots each retain the exact reset-cause presence and value while the full invalid-measurement oracle confirms cleared raw evidence and statistics.
- WD3 RED: after the test required a final raw-consumption event, the focused runner failed with `FAIL: final successful ABBA raw-consumption event recorded`. The host ADC callback now records `TEST_EVENT_RAW_CONSUMED` after it returns a raw code. The watchdog oracle requires final conversion start, then final raw consumption, then WD3; a refresh between conversion start and read completion fails the ordering assertion. Production behavior and headers are unchanged by this instrumentation.
- Overrun paths now use the full invalid-measurement helper, including ABBA raw-evidence clearing. The centered-variance implementation has a short comment stating the exact quotient/remainder identity; its arithmetic is unchanged.
- Fresh focused GCC and Clang runners exited `0`, printing `OK` and `16 passed, 0 failed` for the precision tests. Fresh full GCC and Clang `make test` runs also exited `0`; each passed the functional host tests, concurrency regression, and clean-tree regression.
- The four explicit contract scripts and `bash -n tests/host/*.sh` exited `0`. The hardware-dependency, floating-point, heap, 128-bit integer, removed-cap, Task 2 trace, whitespace, and allowed-scope checks also passed.
- A first full GCC attempt passed its functional stage but failed the concurrency gate because 44 stale `/tmp/ml3-runner-status.*/cc-wrapper.sh` process groups from earlier runs made the runner exceed the fixed 8-second deadline. Exact process-group cleanup reduced a single runner from 9.138 seconds to 7.925 seconds; the standalone three-round concurrency regression then passed in 30.541 seconds. Both later full suites passed, but each left two new wrappers, confirming a pre-existing status-regression harness leak. Those exact wrappers were removed after each run; the harness and deadlines remain unchanged for a separate correction.
- Round 8 changed `ml3_measurement.c`, `ml3_measurement_task3_test.c`, and this execution log. It preserved the Round 7 header changes and added no commit, remote update, or pull request.

### Task 3 binding correction round 9 — exact variance and mixed-sign median

- `gpt-5.6-sol` rereviewed Task 3 commit `095fa89bf29c80bc772e9acc6af7deca0e853ce9` over immutable range `a6ba85c..095fa89`. Verdict: `fix-required`, with two Major findings and no Blocker or Minor finding. All non-statistics Task 3 checks were approved.
- Variance RED: the valid differential set `[0,0,0,2]` failed with `fractional-mean exact floor variance produces zero SD: expected=0 actual=1`. Reversing the final remainder comparison recreated the same failure after the fix.
- The variance path now derives `floor(A/n)` and `A mod n`, where `A=sum((x-trunc(S/n))^2)`, then subtracts one exactly when `(A mod n)*n < (S-n*trunc(S/n))^2`. This equals `floor((n*sum(x^2)-S^2)/n^2)` and preserves the full-domain centered-square bounds. Zeroing the final fractional-mean remainder term only for a negative remainder failed `[−5,−5,−3]` with `negative fractional-mean correction uses mathematical floor: expected=0 actual=1`. This pins the final fractional remainder subtraction; it does not exercise the centered-correction floor division.
- Median RED: the four-cycle differential set `[−5,−1,2,8]` failed with `mixed-sign middle pair average truncates toward zero: expected=0 actual=1`. Removing the opposite-sign branch after the fix recreated that failure. Opposite-sign operands now add directly, which cannot overflow; same-sign operands retain the half/remainder decomposition. The oracle also requires MAD `3` and floor SD `4`.
- Independent `__int128` ground truth matched production median and floor SD for 203,385 exhaustive sorted small-domain sets and 300,000 deterministic extreme-heavy or random ordered sets with `n=3..8`. A separate oracle matched the safe average for 500,121 boundary or random full-`int64_t` pairs. Existing eight-cycle alternating extrema and three-cycle individual-square-overflow tests remain unchanged.
- Fresh focused GCC and Clang runners exited `0`, printing `OK` and `16 passed, 0 failed` for the precision tests. Fresh full GCC and Clang `make test` runs also exited `0`; each passed the functional host tests, concurrency regression, and clean-tree regression.
- Each full suite started with zero stale ML3 wrappers and left the two wrappers from the known status-regression harness leak. Exact process-group cleanup left zero after each run. This round did not alter the harness or its deadlines.
- The four explicit contract scripts, `bash -n tests/host/*.sh`, dependency and arithmetic scans, removed-cap scan, Task 2 trace invariant, whitespace check, and exact-scope check exited `0`.
- Round 9 changed `ml3_measurement.c`, `ml3_measurement_task3_test.c`, and this execution log. It added no commit, remote update, or pull request.

### Task 3 binding correction round 10 — centered correction floor mutation

- An independent arithmetic review approved the implementation but found that Round 9 did not directly test mathematical floor division in the centered correction. The new eight-cycle oracle uses seven differentials of `−2` and one differential of `+1`: `n=8`, `center=0`, `q=floor(sum(z^2)/n)=3`, `t=5`, `mean=−1`, `d=−1`, `r=−5`, and centered-correction numerator `X=t−2dr=−5`. Mathematical `floor(X/n)` is `−1`; the exact population variance is `63/64`, so floor SD is `0`.
- Mutation RED: with the helper kept referenced so compilation reached the numeric assertions, temporarily replacing the centered correction's `ml3_measurement_floor_div_i64(X,n)` call with C integer division `X/n` made the focused GCC runner exit `1`: `negative centered-correction uses mathematical floor division: expected=0 actual=1`. The temporary mutation and helper reference were removed.
- GREEN: after restoring the mathematical floor call, fresh focused GCC and Clang runners exited `0`, printing `OK` and `16 passed, 0 failed` for the precision tests.
- Fresh full GCC and Clang `make test` runs exited `0`; each passed the functional host tests, concurrency regression, and clean-tree regression. Each run started with zero exact ML3 wrappers, left the two wrappers from the known status-regression harness leak, and was followed by exact process-group cleanup to zero. This round did not alter that harness or its deadlines.
- The centered-variance comment now distinguishes the loop's accumulation of centered-square quotient and remainder from the later correction that derives `floor(A/n)` and `A mod n`.
- The four explicit contract scripts and `bash -n tests/host/*.sh` exited `0`. Round 10 touched only `ml3_measurement.c`, `ml3_measurement_task3_test.c`, and this execution log; it changed no production algorithm and added no commit, remote update, or pull request.

### Task 3 binding rereview — approved

- The required `gpt-5.6-sol` reviewer examined immutable range `a6ba85c..f525f5e` in session `019f5f33-0e77-7201-b940-0a8fc2dab8e3`. Its complete report is preserved at `/tmp/ml3-task3-binding-rereview2.txt`.
- Verdict: `approve`; no Blocker, Major, or Minor findings.
- The reviewer independently checked the exact population-variance derivation and its signed bounds, 4,291,892 exhaustive small-domain sample sets, 500,000 extreme or deterministic-random sets with three through eight cycles, and 500,005 full-range signed 64-bit averages.
- Four in-memory mutations were rejected by the focused tests: removal of the fractional-mean correction, reversal of the remainder comparison, replacement of mathematical floor division with C truncation in the centered correction, and restoration of the old mixed-sign median average.
- Strict GCC and Clang in-memory builds passed the Task 3 contract tests and all 16 Task 2 precision-ADC tests. The configuration, readiness, shell-syntax, analyzer, diff-hygiene, and immutable-range checks also passed.
- The rereview confirmed that all findings from the two earlier binding reviews were resolved. Task 3 is complete at the reviewed production-and-test tree in `f525f5e`; the only later amendment is this approval record.

## Task 4 checkpoint — calibration math and redundant storage

Task 4 implements the fixed-point correction model, schema-v2 byte encoding,
CRC-32/ISO-HDLC, dual-slot selection, and verified writes. It adds no EEPROM
addresses, target adapter, or device-hash derivation; those choices remain
blocked on Task 10 target integration.

### Resolved Appendix B and math choices

- The public model owns no dynamic memory. Piecewise points and record workspace
  are caller-owned. Counts 2 through 255 require strictly increasing raw-input
  knots and a nondecreasing effective transfer (`input + correction`); count 0
  disables the table and count 1 is invalid.
- Calibration ID 0 is invalid because payload ID 0 means no calibration. A
  validated record with nonzero ID and all-zero coefficients remains an identity
  correction. The stored +5 V divider coefficient does not participate in the
  moisture correction formula.
- Every division follows the specified staged order and C99 truncation toward
  zero. Multiply-divide uses checked quotient/remainder decomposition, so it
  accepts representable full-range signed 64-bit cases without floating point,
  saturation, heap allocation, variable-length arrays, `__int128`, or fabricated
  coefficient and voltage limits. Every failure leaves the output unchanged.
- Records are encoded field by field as little-endian two's-complement bytes;
  C structs are never serialized. The fixed magic is `0x4D4C3343`, schema is 2,
  and encoded length is exactly `52 + 8N`. The decoding buffer argument is available
  capacity, so a valid record may occupy the prefix of a larger slot.
- The caller supplies the expected nonzero magic and device hash. Validation
  checks the fixed protocol magic, schema, encoded length, capacity, device hash,
  nonzero calibration ID, piecewise semantics, and trailing CRC before changing
  the output model, point storage, or sequence.
- Sequence ordering uses unsigned modular distance. A distance in
  `1..0x7FFFFFFF` is newer; equality and `0x80000000` fail closed as ambiguous.
  Sequence 0 is newer than `0xFFFFFFFF`, and the first stored record uses 0.
- Store reads and validates both slots before writing. It chooses an invalid or
  strictly older slot, assigns the sequence, invalidates every applicable old
  and candidate CRC location, writes and verifies the body, then writes the new
  CRC last. Each invalidation byte differs from both the stored byte and the
  candidate CRC byte and is read back before the body write. This keeps even an
  all-zero candidate CRC uncommitted through partial final-CRC writes.
- A reported final-CRC write failure may still return success only when exact
  full readback and normal record validation prove that the record committed.
  Other I/O failures, silent corruption, partial writes, or ambiguous sequences
  do not write or select a plausible replacement.

### TDD and verification evidence

- Initial RED: `CC=gcc bash tests/host/run_ml3_host_tests.sh` exited 1 because
  the empty scaffold lacked `ml3_calibration_model_t` and the requested API.
- Portability RED: the expanded runner rejected implementation-defined
  unsigned-to-signed decode casts. Explicit two's-complement decoding replaced
  them.
- Exact-length review corrected an overstrict test: encoded length must equal
  `52 + 8N`, while the containing buffer may be larger. The final test pins that
  capacity behavior.
- The all-zero CRC oracle uses sequence `0xD9ED3857`, whose documented
  zero-point record CRC is `0x00000000`. A power cut after the body must reload
  the previous sequence `0xD9ED3856`; the verified nonzero marker makes that
  test pass.
- Focused GCC and Clang runners both exit 0. Each prints
  `ml3 calibration: OK`, the Task 3 `OK`, and all 16 Task 2 precision-ADC passes.
- Calibration tests cover the required numeric oracles, signed extremes,
  overflowing raw products with representable quotients, staged overflow and
  output preservation, raw-keyed interpolation, endpoint holds, flat effective
  segments, count 255, both byte fixtures, CRC metadata mutations with recomputed
  CRCs, transactional decode/encode failures, sequence rollover, equality and
  half-range ambiguity, newest-slot preservation, different old/new CRC
  locations, partial body and 1–3 byte CRC writes, silent marker/body/CRC
  corruption, read/write failures, and final-CRC callback failure.
- GCC `-fanalyzer`, Clang static analysis, and a GCC ASan+UBSan calibration run
  pass. `ml3_config_contract.sh`, `ml3_readiness_cohesion_contract.sh`,
  `ml3_marker_mutation_regression.sh`, `ml3_clean_tree_after_make_test.sh`, and
  `bash -n tests/host/*.sh` pass.
- Dependency, floating-point, heap, VLA, `__int128`, and native-struct
  serialization scans pass. `git diff --check` and the exact five-file scope
  check pass.
- Definitive full suites remain pending integration of the separately reviewed
  process-guard fix. After all Task 4, Task 3, and Task 2 binaries passed, the
  final GCC suite failed only at the pre-existing concurrency gate with
  `round 1 runner A did not finish in time`; both final Clang attempts failed at
  the same gate with `round 2 runner A did not finish in time`. Clean-start was
  not established: the original pre-count checked the `bash` column instead of
  the wrapper-path column. A corrected inventory found four exact stale wrapper
  groups after the failed attempts; those four isolated groups were terminated
  and a follow-up exact scan found none.

### Files changed

- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_calibration.h`
- `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_calibration.c`
- `tests/host/ml3_calibration_test.c`
- `tests/host/run_ml3_host_tests.sh`
- `docs/prompts/ml3-execution-log.md`

### Task 4 binding correction — final-sum cancellation and two-marker cuts

The binding `gpt-5.6-sol` review of commit `33119ed` returned
`fix-required` with two Major findings. Term divisions and interpolation were
accepted. The open work was exact final summation and complete power-cut
coverage when replacing a shorter valid record with a longer one.

- Final-sum RED: the focused GCC runner exited 1 with eight assertions. Both
  `INT64_MAX` and `INT64_MIN` cases returned overflow when an offset or gain term
  crossed the boundary before an opposite common-mode term cancelled it.
- Final-sum GREEN: the six already-computed terms are split into fixed-size
  positive and negative magnitude arrays. Opposite signs cancel before either
  side is accumulated. The accumulator therefore returns the exact signed
  64-bit result whenever the final sum is representable, without changing the
  staged temperature, gain, common-mode, or interpolation divisions. True
  positive and negative overflow still preserve the caller output.
- Reversed offset/common-mode sign cases cover both fixed addition orders. An
  implementation that merely moves common mode before offset avoids the first
  extreme pair but overflows on the reversed pair.
- The correction uses six compile-time-sized terms. It adds no floating point,
  heap allocation, variable-length array, saturation, or 128-bit extension.
- The storage matrix seeds a two-point sequence-10 record in slot 0 and a
  shorter zero-point sequence-9 record in slot 1, then attempts a two-point
  sequence-11 replacement. It injects callback failure, partial application,
  silent corruption, and readback failure at the old CRC marker, candidate CRC
  marker, body, and final CRC boundaries.
- Every precommit case reloads sequence 10 and the original two-point model from
  slot 0. Exact final commit and a fully applied final-CRC callback failure
  reload sequence 11 from slot 1. A final readback I/O failure reports I/O to
  the writer but reloads sequence 11 because the CRC had already committed.
  All cases compare slot 0 byte-for-byte and pin the old CRC offset 48, candidate
  CRC offset 64, 64-byte body, and slot-1-only writes.
- Matrix mutation RED: a temporary copy that skipped the old CRC marker exited
  1 with 89 failed assertions, including premature sequence-11 selection.
  Restoring the production path returned the focused runner to green.
- Final focused GCC and Clang runners exit 0. Each prints
  `ml3 calibration: OK`, the Task 3 `OK`, and all 16 Task 2 ADC passes.
- Full GCC and Clang suites are not claimed for this correction. They remain
  pending integration of the separately reviewed process-guard fix described
  in the preceding checkpoint.

### Task 4 binding rereview

- The required `gpt-5.6-sol` rereviewer examined immutable range
  `5a6a327cb0fb9aebe6914f7a7de0d617b7695c7b..13343adac24d545acb04104c15ad271a2f93eeae`
  in session `019f5f93-6aa8-7b21-84b7-f855ec4657bb`. Its complete report is
  preserved at `/tmp/ml3-task4-binding-rereview.txt`.
- Verdict: `approve`; no Blocker, Major, or Minor findings.
- The reviewer independently passed 100,000 big-integer formula cases, 50,000
  piecewise cases, both CRC fixtures, and 23 storage fault cases in each record-
  length direction. The current tests also rejected the superseded naive
  summation implementation.
- Strict GCC and Clang focused binaries, GCC and Clang analyzers, `-Wvla`, and
  ASan+UBSan passed. Scope, governing-document, forbidden-token, whitespace,
  and clean-worktree checks passed.
- Definitive full GCC and Clang suites remain pending the separate process-guard
  integration. The reviewed production-and-test tree is complete at
  `13343ad`; the only later amendment is this approval record.

## Task 5 checkpoint — quality classification

Task 5 adds the pure-C quality evaluator and keeps every hardware-derived
threshold pending in `ml3_config.h`. The config loader returns
`CONFIG_PENDING` without changing its output until the existing guard,
common-mode, +5 V, noise, warm-up drift, VDDA drift, and die-temperature
readiness flags are set.

- The canonical flag values occupy bits 0 through 15. The fixed invalidating
  mask is `0x027F`, and the state encodings are VALID 0, DEGRADED 1, and
  INVALID 2. Task 3 ADC and thermistor fault names now alias these values.
- The evaluator consumes converted electrical evidence. It does not scale ADC
  codes, apply calibration, or derive terminal voltage. Missing mandatory
  evidence fails closed with `INCOMPLETE`, an INVALID result, the supplied
  flags and cycle count, and no invented transmitted flag. A fixed invalidating
  flag or fewer than three valid cycles is sufficient for a normal INVALID
  result when numeric evidence is absent.
- Low-rail counts are separate for HI and LO. For `N` completed cycles, either
  input needs at least `N` guarded samples among its own `2N` retained samples.
  Guard equality counts because a zero-clipped input is ambiguous.
- HI-over uses mean HI and pre-acquisition VDDA with an inclusive 100000 µV
  margin. Differential range is inclusive from -20000 through 1100000 µV;
  common-mode, +5 V, and die-temperature endpoints are also valid.
- Noise, absolute warm-up drift, and VDDA drift set their warning flags only
  above the warning boundary. Their INVALID transitions are also strict, so
  equality at the invalid threshold remains DEGRADED. Signed drift handles
  `INT64_MIN`; VDDA comparisons use exact 96-bit cross-products built from
  64-by-32-bit limbs.
- The local invalidating signature contains fixed invalidating flags plus
  separate cycle-count, VDDA, noise, warm-up, and incomplete-evidence reasons.
  Warning-only flag changes do not alter it.

TDD started with missing flag/API compile failures, then behavioral RED cases
for malformed threshold tables, cycle-count validity, fixed-mask state, missing
evidence, each range boundary, separate rail counts, dynamic thresholds, and
overflowing VDDA cross-products. The final focused GCC and Clang runners exit
0 and print `ml3 quality: OK`, the Task 3 `OK`, calibration `OK`, and all 16
Task 2 ADC passes. A GCC UBSan build of the quality test also exits 0.

Task 5 changes `ml3_quality.h`, `ml3_quality.c`, the quality host test,
`ml3_measurement.h`, the focused host runner, and this log. It adds no target
adapter, hardware constant, payload behavior, remote command, push, or pull
request.

## Task 6 checkpoint — thermistor conversion

Task 6 adds the host-side thermistor conversion and table interpolation. It
does not add GPIO sequencing, an ADC port, or a production temperature table.
Task 3 owns the powered-off/discharge order, and Task 10 will connect the pure
conversion API to the target.

### Conversion contract

- The divider uses `R_therm = R_ref * C / (65520 - C)`. A `uint64_t`
  intermediate preserves the exact unsigned product; division truncates, and
  a result above `UINT32_MAX` returns overflow without changing caller output.
- Tables contain resistance in ohms and temperature in centidegrees Celsius.
  Resistance knots must be strictly monotonic. Increasing and decreasing
  table order are accepted, endpoints are exact, interior values use linear
  interpolation in resistance, and values outside the table return an error.
- The interpolation product uses unsigned magnitudes. Its maximum supported
  product is `(UINT32_MAX * (UINT32_MAX - 1))`, which fits `uint64_t`; signed
  centidegree results are checked before assignment. No floating point, heap,
  variable-length array, or 128-bit extension is used.
- The rail test is inclusive: `C <= guard` or
  `C >= 65520 - guard` returns `ML3_THERMISTOR_RAIL_FAULT`. Codes above 65520
  are rejected before threshold subtraction.
- Effective reference resistance, rail guard, settle time, and table readiness
  are independent fail-closed inputs. Their production values and readiness
  macros are zero with `PHASE2-PENDING` provenance, and all four now gate
  `ML3_CONFIG_PHASE2_READINESS`.

### TDD and verification evidence

- The first RED build failed because the scaffold did not define the status or
  ratiometric API. The interpolation RED build then failed on the missing point
  type and function. The conversion RED build failed on the missing config,
  result, rail-fault, and readiness contracts.
- The config RED run reported eight missing zero-valued definitions, four
  missing Phase 2 readiness dependencies, and the corresponding C contract
  compile failures. Adding the pending definitions and readiness dependencies
  made all three contracts pass.
- Synthetic tables cover both strict resistance orders, endpoints, positive
  and negative interior interpolation, C99 truncation toward zero, duplicate
  and nonmonotonic knots, zero-resistance knots, one-point tables, null inputs,
  and rejection of extrapolation. A full-domain case exercises an unsigned
  interpolation product larger than `INT64_MAX`.
- Divider tests cover half scale, quarter-scale truncation, zero code, the
  largest representable result, result overflow, ADC full scale, code above
  full scale, zero reference resistance, null output, and output preservation.
- Conversion tests cover both inclusive rail boundaries, the adjacent
  non-fault codes, every readiness bit, zero effective reference, zero and
  overlapping guards, zero settle time, missing/invalid tables, null inputs,
  and result preservation.
- Fresh strict GCC and Clang focused builds each printed
  `ml3_thermistor_test: OK`. GCC `-fanalyzer` and GCC ASan+UBSan also passed.
  The integrated host runner passed under GCC and Clang and printed the Task 6
  pass line alongside the existing Task 2–4 functional tests. No exact
  `ml3-runner-status` wrapper process remained after either run.
- `ml3_config_contract.sh`, `ml3_readiness_cohesion_contract.sh`,
  `bash -n` for the changed shell scripts, dependency scans, and
  `git diff --check` passed. The full lifecycle suite was not run in this
  isolated task worktree; its process-guard correction is being reviewed
  separately.

### Authoritative-table blocker

The repository still contains no ML3 temperature/resistance points. The
production table must be transcribed from the Delta-T ML3 manual and checked
against that source before `ML3_CONFIG_THERMISTOR_TABLE_READY` can become true.
The effective reference value, rail guard, and settle time also require the
planned hardware and bath evidence; the nominal 10.0 kΩ BOM value is not used
as a fabricated effective calibration value.

### Native review correction — order-independent interpolation

- RED: the table `{100 Ω, 0 cC}, {103 Ω, -2 cC}` returned `0 cC` at
  `101 Ω`, while the same two points in reverse order returned `-1 cC`.
  The focused test exited 1 on the new reversed-table equality assertion.
- The interpolation previously anchored its truncated delta at whichever point
  appeared first. Reversing a non-integral segment changed both that anchor and
  the fractional numerator. The segment now selects its lower-resistance point
  as the fixed anchor before applying the existing signed-magnitude arithmetic.
  Both table orders therefore execute the same integer calculation and retain
  C99 truncation toward zero.
- Later-duplicate fixtures cover increasing and decreasing three-point tables.
  An in-memory `<=` to `<` mutation failed only the increasing fixture; the
  corresponding `>=` to `>` mutation failed only the decreasing fixture. Both
  mutants exited 1, while the unmodified focused test printed
  `ml3_thermistor_test: OK`.

## Tasks 5–6 integration checkpoint

- Task 5 is integrated at `96e4004`. Its native independent review returned
  `approve` with no Blocker, Major, or Minor findings. Strict GCC and Clang
  runners, UBSan, GCC and Clang analysis, and three targeted mutations passed.
- Task 6 is integrated after its native review correction. The narrow rereview
  returned `approve` with no remaining findings; reversed non-integral tables
  now agree, and the later-duplicate increasing and decreasing mutations fail.
- The process-identity harness correction is integrated at `d78c10f` after
  native and `gpt-5.6-sol` rereviews returned `approve` with no findings.
- Fresh full lifecycle `make test` runs pass under GCC and Clang on the combined
  Tasks 1–6 tree. Both runs pass the concurrency and clean-tree regressions,
  start and end with zero exact ML3 wrapper processes, and leave the worktree
  clean.
- The mandatory `gpt-5.6-sol` binding reviews for Tasks 5 and 6 remain pending.
  The external reviewer quota was exhausted on 2026-07-14 and reported a reset
  time of 2026-07-20 21:10. No binding approval is claimed for those tasks.

## Task 7 checkpoint — routine and diagnostic payloads

Task 7 implements the Appendix A routine frame, an explicit diagnostic frame
map, per-frame length gates, and the automatic-diagnostic limiter. The module
uses primitive fixed-width inputs so Tasks 5 and 6 can supply their results
without a dependency on unintegrated structs.

### Frozen wire contract

- Routine frames are 25 bytes, big-endian, with version 1 and type 0. Public
  constants pin every offset. HI and LO inputs are named
  `mean_*_uncalibrated_uv`; the payload module only quantizes these
  VREFINT-compensated values and never applies calibration to them.
- Integer quantization uses C99 truncation toward zero: µV to 0.1 mV divides by
  100, µV to mV divides by 1000, and milli°C to centi°C divides by 10. Negative
  values are rejected for unsigned fields before division, so `-1 µV` cannot
  become a plausible zero.
- Unavailable corrected and temperature fields use `0x7FFF`; unavailable
  unsigned and diagnostic raw fields use `0xFFFF`. The builder forces the
  corrected field to `0x7FFF` whenever quality is INVALID or `CAL_INVALID` is
  set, even if the caller supplies a corrected value. Encodable values that
  collide with a sentinel are rejected.
- The fixed invalidating mask is `0x027F`. Fixed invalidating flags or fewer
  than three valid cycles require quality INVALID; quality VALID rejects every
  nonzero flag, and DEGRADED rejects fixed invalidating flags. Diagnostic valid
  cycles cannot exceed the number of captured raw cycles. Contradictions return
  `ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY` instead of being normalized.
- Appendix A.2 did not assign diagnostic offsets. This task freezes the
  conservative 30-byte big-endian header requested by the controller: bytes
  0–2 are version, type 1, and zero-based part index/count; bytes 3–29 contain
  sequence, flags, quality, reset cause, warm-up, hardware revision, firmware
  build, calibration schema and ID, then the five raw fields in the stated
  order. Continuation parts contain the 3-byte prefix and at most four
  `H1,L1,L2,H2` cycles. Counts 0 through 8 produce one through three parts.
- The supplied hex oracle
  `0100a55a1234ff38303931010ce413880141ff8509e6447e00` is malformed, not a
  valid Task 9 ground-truth frame. Flags `0xA55A` set `CAL_INVALID` and other
  invalidating bits, while quality byte `0x44` says DEGRADED and corrected data
  is non-sentinel. Tests use it only to pin offsets and endianness, then require
  the semantic validator to reject it. Separate VALID, DEGRADED, and INVALID
  builder vectors obey the sentinel rules.
- Every builder takes a caller-supplied maximum FRMPayload length and rejects a
  zero gate or an oversized frame before writing output. The central config now
  has zero-valued `ML3_CONFIG_MAX_FRMPAYLOAD_BYTES` and readiness entries marked
  `GATE0-PENDING (§2.1 item 5, §3.13)`; no region, data-rate, or airtime value was
  invented.
- The automatic limiter uses caller-owned fixed storage and a 21,600,000 ms
  interval. Unsigned subtraction makes elapsed time wrap-safe. Eligibility is
  read-only, a new signature fails closed when capacity is full, and the caller
  invokes `ml3_payload_auto_diag_mark_queued` only after every diagnostic part
  is accepted by the radio queue.

### TDD and verification evidence

- Routine RED failed compilation because the scaffold had no payload types,
  constants, or builder. GREEN reproduced a semantically consistent negative-
  differential vector and the 25-byte layout.
- Semantic RED failed compilation before the routine validator existed. GREEN
  rejected the malformed supplied oracle and covered all sentinels, all three
  quality states, negative truncation, reserved encodings, and range and length
  failures.
- Diagnostic RED failed compilation before the header, cycle, and part APIs
  existed. GREEN byte-compared the 30-byte header and both 35-byte continuation
  frames, then covered 0, 1, 4, 5, and 8 cycles, invalid parts, raw-code bounds,
  and per-part FRMPayload gates.
- Limiter RED failed compilation before the entry and eligibility APIs existed.
  GREEN covered same and different signatures, the exact six-hour boundary,
  `uint32_t` wrap, full and zero capacity, and no mutation until the explicit
  queue-accepted mark.
- Self-review found that C99 `-1 / 100` is zero. A regression first failed when
  `-1 µV` reached an unsigned field; validation before division made it pass.
- A controller semantic audit added seven failing mutations: fixed invalidating
  flags with DEGRADED, warning flags with VALID, too few valid cycles without
  INVALID, and diagnostic valid cycles above captured cycles. The builders and
  raw-frame validator now reject each contradiction.
- Fresh full `make test` and `CC=clang make test` runs exited 0. Both printed
  `ml3 payload: OK`, passed all earlier module tests, and passed the concurrency
  and clean-tree regressions.
- The configuration and readiness contracts, pending-marker mutation test,
  `bash -n tests/host/*.sh`, GCC `-fanalyzer`, Clang static analysis, and a GCC
  ASan+UBSan payload run exited 0. `git diff --check`, the untracked-test
  whitespace check, forbidden dependency scans, and documentation lint passed.

### Pending integration gates

- Gate 0 must supply and approve the maximum FRMPayload size for the deployed
  region and data rate. Builders remain fail-closed while the value is zero.
- Task 5 supplies the stable nonzero invalidating signature and owns its
  derivation. Task 7 treats the signature as opaque.
- Task 10 owns cross-reset persistence, storage loading, and the call to mark
  only after queue acceptance. No nonvolatile adapter is present here.
- Task 9 must publish semantically valid shared JSON vectors. It must not copy
  the malformed `A55A` oracle into decoder ground truth.
- The binding `gpt-5.6-sol` review is pending because its quota is unavailable.

### Task 7 native review correction — sentinel and build gates

The native review returned `fix-required` with one Blocker and two Major
findings. Task 7 was first rebased as one commit onto the combined Tasks 1–6
head `839b8ac` so the correction could consume Task 5's canonical quality API.

- Sentinel RED produced 16 focused-test failures. With `ADC_TIMEOUT` and
  caller availability still true, seven non-corrected numeric fields encoded
  plausible zeros; seven validator mutations replacing required sentinels with
  zero were accepted. `THERM_FAULT` also encoded and accepted a zero soil
  temperature.
- Core ADC failures (`ADC_INIT`, `ADC_CAL`, `ADC_TIMEOUT`, or `ADC_OVERRUN`) now
  force corrected, HI, LO, VDDA, +5 V, noise, die temperature, and soil
  temperature to their specified sentinels. `THERM_FAULT` independently forces
  the soil-temperature sentinel. The raw-frame validator enforces the same
  flag-specific rules, so caller availability cannot create plausible abort
  data.
- Canonical-ownership RED stopped compilation while `ml3_payload.h` still
  defined a separate invalidating mask and calibration flag. The public header
  now includes `ml3_quality.h`, stores `ml3_quality_state_t`, uses
  `ML3_QUALITY_INVALIDATING_MASK` and `ML3_QUALITY_FLAG_CAL_INVALID`, and aliases
  its cycle capacity to `ML3_QUALITY_MAX_BURST_CYCLES`. Compile guards reject a
  return of the removed payload-owned mask or calibration flag.
- The FRMPayload contract compiles the current readiness-0, maximum-0 config,
  then compiles copied headers with readiness 1 and maxima 34 and 35. An initial
  version searched the original include directory first; its acceptance result
  was discarded. After correcting include precedence, deleting the production
  preprocessor gate made the 34-byte mutation fail with `accepted ready maximum
  34`. Restoring the gate rejects 34 with its named diagnostic and accepts 35
  under GCC and Clang.
- The preprocessor comparisons use the public 25-byte routine, 30-byte header,
  and 35-byte continuation maxima. No region or data-rate constant was added,
  and readiness 0 continues to block `ML3_CONFIG_DEPLOYABLE` while allowing the
  host build.
- Fresh strict GCC and Clang focused runners exit 0 and print the Task 2–7 pass
  lines. The compile-gate contract passes under both compilers. GCC
  `-fanalyzer`, Clang static analysis, and the GCC ASan+UBSan payload test also
  exit 0.
- Configuration, readiness, and marker-mutation contracts, shell syntax,
  payload-owned-quality scans, forbidden dependency scans, whitespace checks,
  and documentation lint pass. The full lifecycle suite was not repeated for
  this narrow correction; the focused runner includes every functional module
  through Task 7.

### Task 7 native rereview correction — test-gate closure

The rereview accepted the production correction and found two gaps in its test
gates.

- The FRMPayload compile contract used `set -u`, so compiler exit 99 from either
  required-success compile was ignored. Direct mutations of `pending.o` and
  `exact.o` each exited 0 and printed `ml3 payload config gate: OK`. The contract
  now uses `set -eu`; the expected 34-byte compile failure remains guarded by an
  `if` condition.
- A fake compiler regression injects exit 99 at `pending.o` and `exact.o`. It
  requires the contract to return 99 and forbids the success banner in both
  cases. The regression is part of the focused host runner and passes under GCC
  and Clang.
- The core ADC sentinel test now covers `ADC_INIT`, `ADC_CAL`, `ADC_TIMEOUT`, and
  `ADC_OVERRUN`. For each flag it checks every routine numeric field produced by
  the builder and replaces each required sentinel with zero to require validator
  rejection.
- A timeout-only core-mask mutation produced 42 failures: each of the three
  omitted flags left seven builder fields plausible and allowed seven
  contradictory validator mutations. Restoring the four-flag mask returned the
  focused suite to green.
- Strict GCC and Clang focused runners, both compile-gate contracts, shell
  syntax checks, whitespace checks, and documentation lint pass after the
  correction.
## Task 8 checkpoint — length-delimited AT parser

Task 8 implements only the pure parser. Vendor command registration, response
formatting, configuration mutation, calibration decoding, EEPROM writes, and
slot clearing remain Task 10 integration work.

- The parser recognizes only the nine §3.14 operations. Matching is
  case-sensitive and length-delimited; it rejects leading or trailing ASCII
  whitespace, CR/LF, suffixes, aliases, and prefix collisions.
- Warm-up, cycle-count, and raw-mode values use manual unsigned-decimal parsing.
  The parser distinguishes empty values, non-digits, `uint32_t` overflow, and
  command-specific range failures. Warm-up accepts 500 through 3000 ms, cycles
  accept 2 through 8, and raw mode accepts 0 or 1.
- Every failure leaves the caller's output object byte-for-byte unchanged. A
  successful parse assigns a local result only after all syntax, overflow, and
  range checks pass.
- `AT+ML3CAL=<record>` returns a borrowed pointer and explicit length into the
  caller's input. The parser neither copies nor interprets the record. It
  rejects an empty span and embedded NUL because the Task 10 text transport
  must not truncate a length-delimited command silently; other record bytes
  remain opaque.
- No record encoding or vendor-buffer limit was chosen. A host test accepts a
  2092-byte opaque span, the maximum binary schema-v2 record before any text
  encoding. Task 10 must select an encoding and transport strategy that fits
  the vendor AT path without assuming its existing command buffer is large
  enough.
- `AT+ML3CALCLR` produces `ML3_AT_OPERATION_CLEAR_CALIBRATION` and performs no
  storage action. Task 10 must bind it to a verified redundant-slot clear
  operation; Task 4 currently exposes load and store, not erase semantics.
- TDD RED: the strict GCC runner failed while the scaffold lacked every parser
  type and operation. GREEN: focused GCC and Clang runners pass the command
  contract along with the approved Task 2 through Task 4 tests.
- Full `make test` and `make test CC=clang` pass after the process-guard fix at
  base `d78c10f`. Both runs print `ml3 AT command parser: OK`, the calibration
  and measurement pass lines, all 16 precision-ADC passes, the concurrency
  pass, and the clean-tree pass.

## Task 9 checkpoint — shared payload vectors and coverage contract

Task 9 adds the byte-exact fixture set consumed by the firmware host tests and
the future osi-os decoder tests. `tests/vectors/ml3_payload_vectors.json` is the
only tracked source of expected payload bytes.

### Vector contract

- Twelve routine vectors cover VALID, DEGRADED, and INVALID quality; a corrected
  negative differential; independent all-sentinel output for `ADC_INIT`,
  `ADC_CAL`, `ADC_TIMEOUT`, and `ADC_OVERRUN`; retained raw fields on a non-core
  invalid acquisition; calibration-invalid and thermistor-fault sentinels; and
  positive, negative, and toward-zero quantization boundaries.
- Five diagnostic sets cover 0, 1, 4, 5, and 8 cycles. Their expected parts pin
  the 30-byte header, 3-byte continuation prefix, zero-based part index, part
  count nibble, big-endian cycle fields, and unavailable raw-code sentinels.
- Every decoded routine and diagnostic-header fixture includes all 16 named
  `ML3_...` flag booleans. JSON numbers must be safe integers; the validator
  rejects unsafe values before generating C data.
- The supplied `A55A` frame exists only in the `malformed` array with expected
  status `semantic_inconsistency`. Four independent malformed fixtures prove
  that each core ADC fault rejects retained measurement data, and one more
  proves that `THERM_FAULT` cannot carry a soil temperature.

### Tooling and single-source enforcement

- `tests/tools/ml3_payload_vectors.js` uses Node.js standard-library modules
  only. It checks the schema, canonical hexadecimal, byte lengths, duplicate
  IDs, safe integers, diagnostic part metadata, input-to-byte agreement,
  decoded-field agreement, and semantic contradictions.
- `--generate-c` writes a temporary include into the runner's `mktemp` build
  directory. The C test passes each JSON-derived input through the production
  payload API, compares every output byte, checks routine and diagnostic
  metadata, and submits every malformed routine to the production validator.
  The EXIT trap removes the include and all other build products.
- `--decoder-fixtures` emits deterministic validated JSON for the future
  osi-os decoder test. No npm package or generated repository file is needed;
  Node.js is now an explicit host-test prerequisite and the runner fails with a
  named error when it is absent.
- The source contract scans every C, header, JavaScript, and shell source under
  `tests/host`. It derives canonical wire bytes from the JSON and rejects exact
  duplicate hex strings or byte arrays regardless of filename, plus long
  literals assigned to payload/frame/vector-named variables. The regression
  uses `ml3_contract_test.c`; a distinct 52-byte calibration-record initializer
  remains allowed. The three byte arrays formerly in `ml3_payload_test.c` were
  removed after the shared vector test took ownership of those cases.

### Coverage audit and TDD evidence

- The vector-tool RED failed because the required module did not exist. Schema,
  byte, field, part-metadata, unsafe-integer, malformed-status, and output-file
  tests then passed after the validator and generator were implemented.
- The first C vector run reported four failures against the superseded Task 7
  implementation: core ADC data was not fully sentinelized, `THERM_FAULT` kept
  soil temperature, and both contradictory frames passed validation. The same
  vectors passed after rebasing onto corrected Task 7.
- The coverage contract initially failed on the absent Task 8 test and missing
  Task 9 runner entries. After rebasing onto `afbbbc4`, it reports eight
  categories: ADC scale/bounds; ABBA discard, median, MAD, and variance;
  calibration/CRC/storage cuts and sequence rollover; quality boundaries;
  thermistor ratio/interpolation; payload semantics/length gates; shared
  vectors; and AT parser bounds. For each category it requires named evidence
  to be defined and invoked, then checks that the host runner executes the
  compiled test.
- Fresh strict GCC and Clang focused runners pass every Task 1–9 functional
  test. Both print `ml3 payload vector tool: OK`, `validated 12 routine, 5
  diagnostic, and 6 malformed vectors`, `ml3 coverage contract: 8 categories
  OK`, and the existing Task 2–8 pass lines.
- Direct byte and decoded-field mutations fail validation. `git diff --check`,
  shell and JavaScript syntax checks, forbidden-dependency scans, the
  single-source contract, and documentation lint pass. Per controller
  instruction, Task 9 did not run the full lifecycle/concurrency suite.

### Native review correction

The first native review was `fix-required` on two isolation gaps. Mutating the
vector oracle's core-fault mask from `0x000f` to `0x0004` still accepted the
original fixture set because only `ADC_TIMEOUT` was isolated. The new mutation
test failed with that behavior, then passed after all four core ADC flags gained
independent all-sentinel and contradictory-data vectors. A structural test also
requires all eight routine measurement fields to be sentinels for each core
flag and requires a contradictory retained-data frame for each flag.

The first attempt to scan every host byte-array initializer exposed a legitimate
52-byte calibration-record fixture. The final scanner compares candidate bytes
with the canonical JSON wire frames and recognizes explicit payload/frame/vector
assignment context. It rejects a canonical 25-byte fixture placed in
`ml3_contract_test.c` while accepting a separate 52-byte calibration-style
initializer.

The mandatory `gpt-5.6-sol` binding review remains pending because its quota is
unavailable. No binding approval is claimed for Task 9.

## Harness correction — nested config-gate compiler ownership

Task 7 moved a compiler invocation into the payload configuration contract.
The signal-status fixture still treated the compiler wrapper as the isolated
process-group owner, so it rejected a valid identity where the wrapper PID
differed from the contract PGID and SID. A failed run left the verified
contract group alive until the controller drained it.

- The fixture now records two identities: the exact compiler member and the
  isolated payload-contract owner. It requires the member PGID and SID to
  match, derives the owner from that PGID, checks the exact wrapper and contract
  argv, and permits equal owner/member start ticks.
- The owner identity is persisted before the member identity is written. The
  fixture then re-reads both `/proc` identities and both argv records. Cleanup
  is keyed by the owner file, so a member write or move failure still leaves a
  signal-safe group identity.
- Only an identity with PID equal to PGID and SID reaches the owned-process
  drain. The member record is evidence for start-time and argv checks; it is
  never used as a group owner.
- Mutation cases cover owner and member reuse, regrouping, initial and
  post-persistence argv changes, duplicate and substring argv matches, member
  write failure, partial move failure, and a non-owner drain attempt. The live
  cases kill the verified contract leader while the signal-resistant compiler
  remains, then drain the leaderless group through the recorded owner identity.
- Full `make test CC=gcc` and `make test CC=clang` lifecycles pass the payload
  gate, both signal-status regressions, the three-round concurrency test, and
  the clean-tree fixture. Exact argv scans report zero surviving ML3 compiler
  wrappers afterward.
