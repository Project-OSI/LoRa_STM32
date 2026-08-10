# Task 10B remediation implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the ADC port and trial common-mode envelope before Task 10B service wiring resumes.

**Architecture:** The STM32 ADC port owns the analog configuration of PA0, PA1, and PA4. PA2 remains LPUART1 TX because the vendor firmware configures it as alternate function and the trial samples ADC_IN2 without changing its GPIO mode. The port comments make explicit which STM32L072 reference-ready fact is observable and why the elapsed-time guard remains required.

**Tech stack:** C11, STM32L072 CMSIS/HAL, Bash integration contracts, host C contract test.

---

### Task 1: Pin and reference-settle port contract

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_stm32_adc_port.c`
- Modify: `tests/host/ml3_target_integration_contract.sh`

- [x] **Step 1: Write the failing port-contract checks**

  Require `ml3_stm32_adc_port_init` to configure PA0, PA1, and PA4 as `GPIO_MODE_ANALOG` with `GPIO_NOPULL`. Require the source to state that PA2 is preserved as LPUART1 TX and that all four generic reference-ready callbacks observe the STM32L072 single `SYSCFG_CFGR3_VREFINT_RDYF` bit. Require the two-tick VREFINT explanation and the RTC-wrap explanation.

- [x] **Step 2: Run the target contract and verify RED**

  Run: `bash tests/host/ml3_target_integration_contract.sh`

  Expected: failure naming the missing analog-pin setup and explanatory comments.

- [x] **Step 3: Implement the minimal port correction**

  In `ml3_stm32_adc_port_init`, enable GPIOA and call `HW_GPIO_Init` for PA0, PA1, and PA4 with analog/no-pull settings. Do not configure PA2: `stm32l0xx_hw_conf.h` assigns it to LPUART1 TX, and Task 10B’s trial thermistor path reads ADC_IN2 passively without taking over the UART pin.

  Add comments beside the four callbacks: their distinct names satisfy the generic `adc_precision_port_t` contract, but `stm32l072xx.h` aliases each ready name to bit 30. `is_reference_settled` supplies the independent elapsed-time check.

  Document that `N_PREDIV_S = 10` produces 1024 ticks/s, so two ticks are approximately 1.95 ms. State that this exceeds the documented 10 us VREFINT requirement and leaves margin for the observed residual tail. Keep the existing raw-tick epoch handling, with a comment explaining that `HW_RTC_Tick2ms(raw_tick)` wraps at the 32-bit raw-tick period while callers require a millisecond elapsed-time value.

- [x] **Step 4: Run the target contract and verify GREEN**

  Run: `bash tests/host/ml3_target_integration_contract.sh`

  Expected: `ml3 target integration: PASS`.

### Task 2: Trial common-mode envelope

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h`
- Modify: `tests/host/ml3_contract_test.c`
- Modify: `tests/host/ml3_target_integration_contract.sh`
- Modify: `docs/superpowers/plans/2026-08-10-task10b-phase2-configuration.md`

- [x] **Step 1: Write the failing configuration checks**

  Change the host C and integration-contract expectations from `3U`/`10U` to `0U`/`100U` while keeping `ML3_CONFIG_CM_RANGE_READY` at zero.

- [x] **Step 2: Run the narrow checks and verify RED**

  Run: `bash tests/host/ml3_target_integration_contract.sh`

  Expected: failure because the header still has the narrow 3–10 mV range.

- [x] **Step 3: Set the owner-approved envelope**

  Set `ML3_CONFIG_CM_RANGE_MIN_MV` to `0U` and `ML3_CONFIG_CM_RANGE_MAX_MV` to `100U`. State that this trial envelope flags a gross disconnected or shorted leg without repeatedly degrading valid probes across temperature. Do not change the readiness flag.

  Correct the older Phase 2 plan so it records the owner’s superseding 0–100 mV decision rather than claiming the 3–10 mV derivation is current.

- [x] **Step 4: Run the narrow checks and verify GREEN**

  Run: `bash tests/host/ml3_target_integration_contract.sh`

  Expected: `ml3 target integration: PASS`.

### Task 3: Full verification and commit

**Files:**

- Modify: the files from Tasks 1–2 only

- [x] **Step 1: Run all gates**

  Run from the worktree root:

  ```bash
  bash tests/host/run_ml3_host_tests.sh
  bash tests/host/ml3_target_integration_contract.sh
  make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -B
  make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -B BENCH=1
  git diff --check
  node /home/phil/Repos/osi-os/.claude/skills/anti-slop-writing/slop-check.js \
    docs/superpowers/plans/2026-08-10-task10b-remediation.md \
    docs/superpowers/plans/2026-08-10-task10b-phase2-configuration.md
  ```

  Expected: each command exits zero. Record the target image sizes and retain the vendor/newlib warnings only if they were present before this change.

- [ ] **Step 2: Commit the correction**

  ```bash
  git add \
    'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_stm32_adc_port.c' \
    'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h' \
    tests/host/ml3_contract_test.c \
    tests/host/ml3_target_integration_contract.sh \
    docs/superpowers/plans/2026-08-10-task10b-remediation.md \
    docs/superpowers/plans/2026-08-10-task10b-phase2-configuration.md
  git commit -m "fix: harden ML3 trial ADC port"
  ```

  Do not push.
