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
