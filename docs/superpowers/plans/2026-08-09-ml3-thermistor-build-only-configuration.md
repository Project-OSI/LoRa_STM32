# ML3 thermistor build-only configuration implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Select PA2/ADC_IN2 and PB4 for the ML3 thermistor circuit, prove the selection in the host and target contracts, and produce default and BENCH firmware builds without enabling an ML3 acquisition.

**Architecture:** `inc/ml3_config.h` remains the only firmware source for the selected thermistor channel and excitation-line identity. The compile-time host contract and the target integration script assert those identities while asserting that their hardware readiness flags, `ML3_CONFIG_ACQUISITION_READY`, and `ML3_CONFIG_DEPLOYABLE` remain zero. Mode/FPort protocol authorization remains independently true. This plan does not add an STM32 ADC adapter, a GPIO write, a PB5 write, a service registration, or a calibration write.

**Tech Stack:** C compile-time contracts, Bash integration contract, GNU Make GCC target build, STM32L0 firmware.

---

## File map

| File | Responsibility |
|---|---|
| `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h` | Defines PA2 as ADC channel 2 and PB4 as excitation line 4 while retaining zero readiness. |
| `tests/host/ml3_contract_test.c` | Compiles the selected identities and all inactive gates into the host contract. |
| `tests/host/ml3_target_integration_contract.sh` | Checks the target configuration text for the exact selected identities and the inactive gates. |
| `docs/2026-07-12-lsn50v2-ml3-firmware-plan.md` | Distinguishes permitted build-only work from Gate 0 and deployment. |
| `docs/ml3-port-adapter-spec.md` | Defines the build-only boundary for the future STM32 adapters. |
| `docs/task10-agent-prompt.md` | Splits Task 10A build-only work from Task 10B hardware activation. |
| `docs/superpowers/specs/2026-08-09-ml3-thermistor-pa2-pb4-design.md` | Records the PA2 serial hand-off, PB4 legacy-mode exclusivity, ratiometric conversion, and remaining physical activation conditions. |

The configuration is intentionally direct: it records only the two selected target values, rather than adding a new pin-description abstraction for one STM32 target. The selected identity and its safety state are duplicated only in executable contracts, where duplication detects drift.

### Task 1: Build-only thermistor configuration contract

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h:24-27`
- Modify: `tests/host/ml3_contract_test.c:24-29`
- Modify: `tests/host/ml3_target_integration_contract.sh:25-34`
- Test: `tests/host/run_ml3_host_tests.sh`
- Test: `tests/host/ml3_target_integration_contract.sh`

- [ ] **Step 1: Replace the two zero-value host assertions with the required selected identities.**

  In `tests/host/ml3_contract_test.c`, replace the two value assertions with compile-time checks. Retain the zero assertions for their readiness flags.

  ```c
  typedef char ml3_thermistor_adc_channel_is_pa2[
      (ML3_CONFIG_THERMISTOR_ADC_CHANNEL == 2U) ? 1 : -1];
  ASSERT_ZERO_MACRO(ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY);
  typedef char ml3_thermistor_excitation_is_pb4[
      (ML3_CONFIG_THERMISTOR_EXCITATION_GPIO == 4U) ? 1 : -1];
  ASSERT_ZERO_MACRO(ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY);
  ```

- [ ] **Step 2: Run the host test to verify the new contract fails.**

  Run: `bash tests/host/run_ml3_host_tests.sh`

  Expected: the `ml3_contract_test.c` compilation fails because the two configuration values are still `0U`; the diagnostics name `ml3_thermistor_adc_channel_is_pa2` and/or `ml3_thermistor_excitation_is_pb4`.

- [ ] **Step 3: Set the two build-only identities in the configuration header.**

  In `inc/ml3_config.h`, replace only these two pending values and update their comments to identify the STM32 pin and build-only status.

  ```c
  #define ML3_CONFIG_THERMISTOR_ADC_CHANNEL      2U /* BUILD-ONLY: PA2 / ADC_IN2 */
  #define ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY 0U /* GATE0-PENDING (§3.3; board PA2 pull check) */
  #define ML3_CONFIG_THERMISTOR_EXCITATION_GPIO  4U /* BUILD-ONLY: PB4 */
  #define ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY 0U /* GATE0-PENDING (§3.3; circuit fitted and checked) */
  ```

  Do not change `ML3_CONFIG_THERMISTOR_SETTLE_TIME_MS`, any table or rail-guard value, any PB5 macro, or any readiness formula.

- [ ] **Step 4: Run the host test to verify the configuration passes and remains inert.**

  Run: `bash tests/host/run_ml3_host_tests.sh`

  Expected: exit 0 and the final line `adc precision tests: 18 passed, 0 failed`. The contract compiles only if the selected values match PA2/PB4 and their readiness flags remain zero.

- [ ] **Step 5: Add text-level target contract checks for the selected but inactive wiring.**

  Immediately after the mode/FPort checks in `tests/host/ml3_target_integration_contract.sh`, add these checks:

  ```bash
  require '^#define[[:space:]]+ML3_CONFIG_THERMISTOR_ADC_CHANNEL[[:space:]]+2U[[:space:]]*/\\*' "$CONFIG" \
    'build-only thermistor ADC channel is not PA2/ADC_IN2'
  require '^#define[[:space:]]+ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY[[:space:]]+0U[[:space:]]*/\\*' "$CONFIG" \
    'thermistor ADC channel readiness is not held at zero'
  require '^#define[[:space:]]+ML3_CONFIG_THERMISTOR_EXCITATION_GPIO[[:space:]]+4U[[:space:]]*/\\*' "$CONFIG" \
    'build-only thermistor excitation is not PB4'
  require '^#define[[:space:]]+ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY[[:space:]]+0U[[:space:]]*/\\*' "$CONFIG" \
    'thermistor excitation readiness is not held at zero'
  require '^#define[[:space:]]+ML3_CONFIG_ACQUISITION_READY' "$CONFIG" \
    'acquisition readiness definition is missing'
  ```

  Also compile a temporary gate assertion in `mktemp -d`, remove that directory with a `trap` on exit, and fail if either readiness formula becomes nonzero. Use the host compiler (`${CC:-cc}`), not the ARM compiler:

  ```bash
  if ! contract_tmp=$(mktemp -d "${TMPDIR:-/tmp}/ml3-target-integration.XXXXXX"); then
    fail 'cannot create temporary directory for build-only gate contract'
    exit 1
  fi
  trap 'rm -rf "$contract_tmp"' EXIT
  if ! printf '%s\n' \
    '#include "ml3_config.h"' \
    'typedef char thermistor_adc_is_pa2[(ML3_CONFIG_THERMISTOR_ADC_CHANNEL == 2U) ? 1 : -1];' \
    'typedef char thermistor_excitation_is_pb4[(ML3_CONFIG_THERMISTOR_EXCITATION_GPIO == 4U) ? 1 : -1];' \
    'typedef char thermistor_adc_ready_stays_off[(ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY == 0U) ? 1 : -1];' \
    'typedef char thermistor_excitation_ready_stays_off[(ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY == 0U) ? 1 : -1];' \
    'typedef char acquisition_stays_off[(ML3_CONFIG_ACQUISITION_READY == 0U) ? 1 : -1];' \
    'typedef char deployment_stays_off[(ML3_CONFIG_DEPLOYABLE == 0U) ? 1 : -1];' \
    'int main(void) { return 0; }' | \
    "${CC:-cc}" -std=c99 -Werror -I"$APP_DIR/inc" -x c - -c \
      -o "$contract_tmp/ml3_build_only_gate.o"; then
    fail 'build-only configuration or readiness assertion failed to compile'
  fi
  ```

- [ ] **Step 6: Run the target contract.**

  Run: `bash tests/host/ml3_target_integration_contract.sh`

  Expected: `ml3 target integration contract: OK`.

- [ ] **Step 7: Review the task diff and commit only the configuration and contracts.**

  Run: `git diff --check && git diff -- STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h tests/host/ml3_contract_test.c tests/host/ml3_target_integration_contract.sh`

  Expected: no whitespace errors; no changed readiness value or acquisition formula.

  Commit:

  ```bash
  git add STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN\(AT\)/inc/ml3_config.h \
    tests/host/ml3_contract_test.c \
    tests/host/ml3_target_integration_contract.sh
  git commit -m "feat: select PA2 and PB4 for ML3 thermistor"
  ```

### Task 2: Align the task documents with the build-only boundary

**Files:**

- Modify: `docs/2026-07-12-lsn50v2-ml3-firmware-plan.md:48-65, 103-105, 184-197, 216-228`
- Modify: `docs/ml3-port-adapter-spec.md:1-6, 24-30, 45-56`
- Modify: `docs/task10-agent-prompt.md:1-8, 28-33, 52-58`
- Modify: `docs/superpowers/specs/2026-08-09-ml3-thermistor-pa2-pb4-design.md:7-30`
- Test: `/home/phil/Repos/osi-os/.claude/skills/anti-slop-writing/slop-check.js`

- [ ] **Step 1: Amend the governing firmware plan without weakening Gate 0.**

  Replace the open D3 diagram entry with `PA2 / ADC_IN2` and name PB4 as the controlled divider GPIO. In the Phase 0 introduction, permit configuration and compile-only adapter work before Gate 0 only when every readiness macro remains zero and no service path can touch ADC, PB5, thermistor excitation, EEPROM, or the LoRa queue. Keep Gate 0 as the condition for any reachable acquisition, PB5-powered ML3 test, calibration write, or deployment. In §3.3, state that the manual lookup table, fitted-circuit settle time, rail guard, and bath qualification remain pending; do not assign a numeric firmware value.

- [ ] **Step 2: Split the port-adapter specification into build-only and activation conditions.**

  Replace the absolute prerequisite with two conditions: Task 10A may compile target adapter objects and configure known pin identities while all Gate 0/Phase 2 hardware readiness macros are zero and acquisition/deployability remain false; Task 10B may register the adapters or make hardware access reachable only after Gate 0 supplies the required values. Replace the thermistor-channel placeholder with PA2/ADC_IN2 and the excitation placeholder with PB4. State that no adapter may call the legacy two-pin `vcom_IoDeInit` helper because it changes PA3 too; a later adapter must wait for LPUART transmission completion and change PA2 alone.

- [ ] **Step 3: Stage Task 10 in the execution prompt.**

  Add a short status paragraph before the phases: Task 10A is configuration and object-compilation only, with Gate 0/Phase 2 hardware readiness macros false and acquisition/deployability false. It permits no registration or reachable ADC access, PB5 control, thermistor excitation, EEPROM write, LoRa queue, sample processing, or physical sample. Rename the listed hardware phases as Task 10B and state that they remain blocked by Gate 0. Keep the existing hard constraints and no-core-module-edit rule unchanged.

- [ ] **Step 4: Complete the design record with the external-review corrections.**

  Add that PA2 is LPUART1 TX in the stock firmware and that the future adapter must wait for transmission completion, switch PA2 alone into analog mode for the sample, then restore LPUART1 TX; PA3 must remain untouched. Add that PB4 is the legacy oil-float control and ML3 mode excludes that reader. State that the thermistor conversion is ratiometric, so VREFINT compensation must not be applied to the thermistor raw code, and that the ML3 manual table needs an independent transcription check. Retain the physical PA2 pull check, the unpowered-probe sequence, and the multi-point bath requirement.

- [ ] **Step 5: Check the documentation.**

  Run:

  ```bash
  node /home/phil/Repos/osi-os/.claude/skills/anti-slop-writing/slop-check.js \
    docs/2026-07-12-lsn50v2-ml3-firmware-plan.md \
    docs/ml3-port-adapter-spec.md \
    docs/task10-agent-prompt.md \
    docs/superpowers/specs/2026-08-09-ml3-thermistor-pa2-pb4-design.md
  git diff --check
  ```

  Expected: the checker reports `slop-check: PASS (no tier-1 findings)` and `git diff --check` emits no output.

- [ ] **Step 6: Review and commit only the four documentation files.**

  Run: `git diff -- docs/2026-07-12-lsn50v2-ml3-firmware-plan.md docs/ml3-port-adapter-spec.md docs/task10-agent-prompt.md docs/superpowers/specs/2026-08-09-ml3-thermistor-pa2-pb4-design.md`

  Expected: the text permits compile-only work while retaining the activation gate; it does not claim a PA2 pull modification, a PB5 brownout test, or a completed bath calibration.

  Commit:

  ```bash
  git add docs/2026-07-12-lsn50v2-ml3-firmware-plan.md \
    docs/ml3-port-adapter-spec.md \
    docs/task10-agent-prompt.md \
    docs/superpowers/specs/2026-08-09-ml3-thermistor-pa2-pb4-design.md
  git commit -m "docs: define ML3 build-only integration boundary"
  ```

### Task 3: Keep the suite drain fixture focused on the payload compiler

**Files:**

- Modify: `tests/host/ml3_suite_signal_status_regression.sh:1-230`
- Test: `tests/host/ml3_suite_signal_status_regression.sh`
- Test: `tests/host/run_ml3_host_suite.sh`

- [ ] **Step 1: Reproduce the suite-stage ownership failure.**

  Run: `timeout -k 2s 45s bash tests/host/ml3_suite_signal_status_regression.sh`

  Expected before the repair: exit 1 with `suite_real_drain_failure suite status regression did not retain owner after compiler member write failure`.

- [ ] **Step 2: Capture the real host compiler and identify the earlier contract stage.**

  Before the fixture sets `CC` to `CC_WRAPPER`, add these variables beside the existing path declarations:

  ```bash
  TARGET_INTEGRATION_CONTRACT="$ROOT_DIR/tests/host/ml3_target_integration_contract.sh"
  REAL_CC="${CC:-cc}"
  ```

  Export both variables with the existing compiler-wrapper environment. The outer suite supplies `CC_WRAPPER`; the nested target integration contract must instead use the captured real compiler.

- [ ] **Step 3: Pass the target-contract compiler invocation through the wrapper.**

  In the generated `CC_WRAPPER`, before it writes `CC_PID_FILE` or `CC_SENTINEL`, add this condition:

  ```bash
  if [ -r "/proc/$PPID/cmdline" ] && \
    tr '\\0' ' ' <"/proc/$PPID/cmdline" | grep -Fq "$TARGET_INTEGRATION_CONTRACT"; then
    exec "$REAL_CC" "$@"
  fi
  ```

  The target integration contract compiles its temporary gate assertion before the payload contract runs. Passing only that invocation through preserves the fixture's intended payload-compiler ownership and its forced drain-failure assertion; it does not change the production suite or the target contract.

- [ ] **Step 4: Run the focused regression to verify the original failure is fixed.**

  Run: `timeout -k 2s 45s bash tests/host/ml3_suite_signal_status_regression.sh`

  Expected: exit 0. Both `suite_real_drain_failure` and `suite_flattened_drain_failure` execute; the fixture still detects the intentionally flattened drain status.

- [ ] **Step 5: Run the complete host suite.**

  Run: `bash tests/host/run_ml3_host_suite.sh`

  Expected: exit 0. The target-integration stage uses the captured real compiler, and the later payload-config stage remains the wrapper process the regression owns and drains.

- [ ] **Step 6: Review and commit the fixture repair.**

  Run: `git diff --check && git diff -- tests/host/ml3_suite_signal_status_regression.sh`

  Expected: no production firmware, ML3 configuration, or documentation changes.

  Commit:

  ```bash
  git add tests/host/ml3_suite_signal_status_regression.sh
  git commit -m "test: keep suite drain regression on payload compiler"
  ```

### Task 4: Mirror the target-integration stage in the clean-tree suite fixture

**Files:**

- Modify: `tests/host/ml3_clean_tree_after_make_test.sh:22-45`
- Test: `tests/host/ml3_clean_tree_after_make_test.sh`
- Test: `tests/host/run_ml3_host_suite.sh`

- [ ] **Step 1: Reproduce the missing fixture-stage failure.**

  Run: `setsid bash tests/host/ml3_clean_tree_after_make_test.sh`

  Expected before the repair: exit 1 with `setsid: failed to execute .../ml3_target_integration_contract.sh: No such file or directory` and `suite failed without cleanliness diagnostic`.

- [ ] **Step 2: Add the missing no-op target-integration fixture stage.**

  In `make_fixture`, insert this stage after `ml3_readiness_cohesion_contract.sh` and before `run_ml3_host_tests.sh`:

  ```bash
  write_stage "$fixture_dir/fixture/tests/host/ml3_target_integration_contract.sh" ':'
  ```

  This mirrors the order of `STAGES` in `run_ml3_host_suite.sh`. The clean-tree fixture tests suite cleanliness, so its target-integration stage is intentionally a no-op rather than a copy of the real target contract.

- [ ] **Step 3: Run the focused clean-tree regression.**

  Run: `setsid bash tests/host/ml3_clean_tree_after_make_test.sh`

  Expected: exit 0 and `clean-tree regression passed` after both the early-artifact negative case and the clean fixture case.

- [ ] **Step 4: Run the complete host suite in an isolated session.**

  Run: `setsid bash tests/host/run_ml3_host_suite.sh`

  Expected: exit 0. The copied fixture now contains one placeholder for every suite stage, including target integration.

- [ ] **Step 5: Review and commit the fixture repair.**

  Run: `git diff --check && git diff -- tests/host/ml3_clean_tree_after_make_test.sh`

  Expected: only the missing test-fixture placeholder is added.

  Commit:

  ```bash
  git add tests/host/ml3_clean_tree_after_make_test.sh
  git commit -m "test: mirror target integration in clean fixture"
  ```

### Task 5: Mirror the target-integration stage in signal and descendant fixtures

**Files:**

- Modify: `tests/host/ml3_suite_signal_term_regression.sh:46-62`
- Modify: `tests/host/ml3_suite_descendant_regression.sh:40-60`
- Test: `tests/host/ml3_suite_signal_term_regression.sh`
- Test: `tests/host/ml3_suite_descendant_regression.sh`
- Test: `tests/host/run_ml3_host_suite.sh`

- [ ] **Step 1: Reproduce the signal fixture's missing-stage failure.**

  Run: `setsid bash tests/host/ml3_suite_signal_term_regression.sh`

  Expected before the repair: exit 1, with `failed to execute .../ml3_target_integration_contract.sh: No such file or directory` before the hanging-stage sentinel is observed.

- [ ] **Step 2: Add the no-op target-integration stage to both copied suite fixtures.**

  In each fixture's stage-writing function, insert this exact line after its `ml3_readiness_cohesion_contract.sh` placeholder and before `run_ml3_host_tests.sh`:

  ```bash
  write_stage "$FIXTURE_ROOT/tests/host/ml3_target_integration_contract.sh" ':'
  ```

  If the descendant fixture uses a function-specific root variable, use that existing root variable in the same path position. The placeholder must remain a no-op: these fixtures test process handling, not target integration.

- [ ] **Step 3: Run both focused regressions.**

  Run:

  ```bash
  setsid bash tests/host/ml3_suite_signal_term_regression.sh
  setsid bash tests/host/ml3_suite_descendant_regression.sh
  ```

  Expected: both exit 0. Their original signal/descendant assertions still execute after the added no-op stage.

- [ ] **Step 4: Run the complete host suite in an isolated session.**

  Run: `setsid bash tests/host/run_ml3_host_suite.sh`

  Expected: exit 0. Every copied-suite fixture now has a placeholder for target integration in the primary suite's order.

- [ ] **Step 5: Review and commit the paired fixture repair.**

  Run: `git diff --check && git diff -- tests/host/ml3_suite_signal_term_regression.sh tests/host/ml3_suite_descendant_regression.sh`

  Expected: two no-op fixture-stage additions, with no production code change.

  Commit:

  ```bash
  git add tests/host/ml3_suite_signal_term_regression.sh tests/host/ml3_suite_descendant_regression.sh
  git commit -m "test: mirror target integration in suite fixtures"
  ```

### Task 6: Verify default and BENCH firmware images remain non-activating

**Files:**

- Verify: `tests/host/run_ml3_host_suite.sh`
- Verify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc/Makefile`
- Verify: default and `BENCH=1` GCC build outputs

- [ ] **Step 1: Run the complete host and target contract suite.**

  Run: `bash tests/host/run_ml3_host_suite.sh`

  Expected: each host test reports its success line and the target integration script ends with `ml3 target integration contract: OK`.

- [ ] **Step 2: Produce the default image.**

  Run: `make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -j2`

  Expected: Make exits 0 and produces the default firmware image without `ML3_BENCH_TOOLS` in the compiler invocation.

- [ ] **Step 3: Produce the BENCH image.**

  Run: `make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' BENCH=1 -j2`

  Expected: Make exits 0 and the compiler invocation includes `-DML3_BENCH_TOOLS=1`; this image still has no registered production ML3 adapter.

- [ ] **Step 4: Inspect the build boundary and working tree.**

  Run:

  ```bash
  rg -n 'ML3_CONFIG_ACQUISITION_READY|BSP_ML3_Service|set_power_5v|set_thermistor_excitation' \
    STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN\(AT\)/src/bsp.c \
    STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN\(AT\)/src/main.c
  git diff --check
  git status --short --branch
  ```

  Expected: `BSP_ML3_Service` remains the existing inert implementation, mode-10 remote selection remains guarded by `ML3_CONFIG_ACQUISITION_READY`, and the PB5 envelope note remains separate from this task's commits.

## Plan self-review

**Spec coverage:** Task 1 selects PA2/ADC_IN2 and PB4 with executable zero-readiness checks. Task 2 resolves the plan-versus-port-spec conflict by allowing only compile-only work before Gate 0 and records the PA2 serial, PB4 legacy-reader, ratiometric, manual-table, and physical-pull constraints. Task 3 proves both target images compile while the service remains inactive.

**Intentional exclusions:** This plan does not assign a settle duration, a thermistor reference resistance, a rail guard, a table, V5 values, a PB5 brownout approval, a physical PA2 pull modification, or a deployment approval. Those require the stated Gate 0 or Phase 2 evidence.

**Type consistency:** ADC channel and excitation identities are unsigned configuration constants (`2U` and `4U`); their existing readiness macros stay unsigned zero values. The plan adds no new C type, port function, or runtime state.
