# Task 10B Phase 3 service implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the STM32L072 ML3 measurement service without enabling acquisition or changing the full Gate 0 readiness expression.

**Architecture:** `bsp.c` owns the target-side measurement context and supplies the pure engine with small hardware callbacks. The callbacks remain inside a compile-time `ML3_CONFIG_ACQUISITION_READY` guard, so the Phase 3 image keeps the ML3 rail, ADC, LoRa queue, and thermistor excitation unreachable. The future service reuses the ADC port's wrap-safe RTC clock, treats the absent thermistor table as an unavailable payload field, and reports an uncorrected reading with calibration ID zero.

**Tech stack:** C11, STM32L072 CMSIS/HAL, LoRaMac radio driver, Bash target-integration contract, strict GCC cross-build, host C tests.

---

## Decisions and boundaries

- Calibration readiness is not an activation gate. `ML3_CONFIG_ACQUISITION_READY` is exactly `ML3_CONFIG_GATE0_READINESS` at `inc/ml3_config.h:100-139`; no `ML3_CONFIG_CAL_*` symbol occurs there. The calibration flags occur only in `ML3_CONFIG_PHASE2_READINESS`, used by `ML3_CONFIG_DEPLOYABLE`. Do not add a calibration exclusion, a calibration readiness macro, or a trial macro in this phase.
- The RTC epoch implementation stays unchanged. `HW_RTC_Tick2ms()` converts raw RTC ticks before wrapping, so unsigned subtraction of converted milliseconds is wrong across the raw counter boundary. No code action is required for this review item.
- The owner approved `ML3_QUALITY_DIFF_MAX_UV = 1200000` because the one-node record observed 1110 mV. This is the only authorized change to a protected ML3 module in this phase.
- The thermistor table remains unready and `ml3_thermistor_convert()` must not be called. That converter returns `ML3_THERMISTOR_CONFIG_NOT_READY` for the deliberately absent table. The core requires a nonzero thermistor settle period, so use `1U` ms as a state-machine yield only; it does not claim electrical settling.
- Do not set radio region, data-rate, airtime, or maximum-payload configuration values. The routine builder receives `ML3_PAYLOAD_ROUTINE_LENGTH` as its local output-capacity proof, not a radio configuration value. The vendor LoRaWAN stack remains responsible for radio configuration and duty-cycle control.
- Quality-threshold readiness remains pending. `ml3_quality_thresholds_from_config()` must make the future `on_process` callback return false while the threshold configuration is absent. The core then takes its existing error cleanup path and disables controls. Do not invent provisional thresholds in this phase.

## File map

| File | Responsibility |
|---|---|
| `inc/ml3_quality.h` | Owner-approved 1200 mV differential ceiling. |
| `src/ml3_stm32_adc_port.c` and `inc/ml3_stm32_adc_port.h` | Export the existing wrap-safe elapsed-time clock for the measurement port; do not alter its epoch algorithm. |
| `src/bsp.c` | Target-only service state, the measurement callbacks, payload assembly, guarded lifecycle, and abort cleanup. |
| `tests/host/ml3_quality_test.c` | Ceiling boundary regression. |
| `tests/host/ml3_target_integration_contract.sh` | Structural contract for the guarded target integration and the no-thermistor path. |
| `docs/superpowers/plans/2026-08-10-task10b-phase3-service.md` | Phase decisions, test evidence, and committed scope. |

### Task 1: Raise the authorized differential ceiling

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_quality.h:32-35`
- Modify: `tests/host/ml3_quality_test.c:77-114,460-487`

- [x] **Step 1: Write the failing ceiling test**

  Change the public-contract assertion to require:

  ```c
  CHECK(ML3_QUALITY_DIFF_MAX_UV == INT64_C(1200000));
  ```

  Keep the existing boundary test and add its explicit intent: 1,200,000 uV is in range and 1,200,001 uV sets `ML3_QUALITY_FLAG_DIFF_RANGE`.

- [x] **Step 2: Run the focused test and verify RED**

  Run:

  ```bash
  bash tests/host/run_ml3_host_tests.sh
  ```

  Expected: `ml3_quality_test` fails at the still-1100000-uV public assertion.

- [x] **Step 3: Make the minimal header change**

  Replace the single fixed ceiling definition with:

  ```c
  #define ML3_QUALITY_DIFF_MAX_UV INT64_C(1200000)
  ```

  Add a concise source comment citing the owner decision and the 1110 mV observation in `docs/gate0-records/M013437-A840412D385E7D00/section4-adc-characterization.md:54`. Do not change the invalidating-mask semantics.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run:

  ```bash
  bash tests/host/run_ml3_host_tests.sh
  ```

  Expected: the full host runner exits 0 and the new 1.2 V boundary is covered.

### Task 2: Expose the ADC port's elapsed-time clock

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_stm32_adc_port.h:12-21`
- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_stm32_adc_port.c:58-81`
- Modify: `tests/host/ml3_target_integration_contract.sh`

- [x] **Step 1: Add a failing target-contract requirement**

  Require a public declaration and definition of:

  ```c
  uint32_t ml3_stm32_adc_port_now_ms(void *port_ctx);
  ```

  Require it to retain the existing active-context guard, raw-tick-wrap detection, epoch increment, and `HW_RTC_Tick2ms()` conversion. The contract must reject a second BSP-side epoch counter.

- [x] **Step 2: Run the target contract and verify RED**

  Run:

  ```bash
  bash tests/host/ml3_target_integration_contract.sh
  ```

  Expected: failure stating that the elapsed-time function is not public.

- [x] **Step 3: Export the existing implementation without changing its behavior**

  Remove only `static` from the existing clock function, add the matching header declaration, and preserve this return path:

  ```c
  return context->rtc_epoch_ms +
    (uint32_t)HW_RTC_Tick2ms(now_tick);
  ```

  Do not introduce a new time base, alter `ML3_STM32_ADC_RTC_WRAP_MS`, or modify the epoch comments approved in the RTC review.

- [x] **Step 4: Run the target contract and verify GREEN**

  Run:

  ```bash
  bash tests/host/ml3_target_integration_contract.sh
  ```

  Expected: `ml3 target integration contract: OK`.

### Task 3: Add the guarded STM32 measurement service

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/bsp.c:65-260`
- Modify: `tests/host/ml3_target_integration_contract.sh`

- [x] **Step 1: Add failing structural integration checks**

  Extend the target contract to require all of the following before changing `bsp.c`:

  - the implementation-only portion is guarded by `#if ML3_CONFIG_ACQUISITION_READY`, while `BSP_ML3_Service`, request functions, and `BSP_ML3_Abort` preserve their existing false-gate behavior;
  - `BSP_ML3_Init` uses four ABBA cycles, channels `0U`, `1U`, `4U`, and `2U`, a 1500-ms configuration value, a 250-mV PA4 discharge threshold (the 500-mV rail threshold after the nominal 0.5 divider), a 2000-ms timeout, a 1-ms no-op thermistor state yield, and a conversion timeout of at least 10 ms;
  - the measurement port reuses `ml3_stm32_adc_port_now_ms`, calls `Radio.Sleep()` only after rejecting a busy `LoRaMacState`, configures PA0/PA1/PA4 through `ml3_stm32_adc_port_init`, and drives PB5 active-low with `PWR_OUT_PORT` and `PWR_OUT_PIN`;
  - no target service code writes PB4, changes PA2 configuration, calls `vcom_IoDeInit`, or calls `ml3_thermistor_convert`;
  - `on_process` marks the thermistor status valid for quality completeness but builds the payload with `soil_temperature_available = false` and `calibration_id = 0U`;
  - payload construction passes `ML3_PAYLOAD_ROUTINE_LENGTH`, sets FPort 13 from `ML3_CONFIG_FPORT`, and queueing calls `LORA_send(..., LORAWAN_UNCONFIRMED_MSG)`;
  - the default preprocessed build has no reachable PB5 enable, ADC-port initialization, `Radio.Sleep`, or `LORA_send` path when acquisition readiness is false.

- [x] **Step 2: Run the target contract and verify RED**

  Run:

  ```bash
  bash tests/host/ml3_target_integration_contract.sh
  ```

  Expected: failures for the absent service callbacks, gate, and no-thermistor contract.

- [x] **Step 3: Implement only the target adapter layer**

  Add static target state in `bsp.c` for the STM32 ADC context, `adc_precision_context_t`, `ml3_measurement_ctx_t`, quality result, and one persistent 25-byte routine frame. Keep it local to `bsp.c`; do not add a storage port, a calibration model, a thermistor table, or any soil-moisture conversion.

  Under `#if ML3_CONFIG_ACQUISITION_READY`, construct this port and configuration. `BSP_ML3_Init` must use an automatic, mutable `ml3_measurement_config_t`, because reading the STM32 factory-memory word is not a C constant expression. Initialize the fixed fields, then assign the factory word before `ml3_measurement_init()`:

  ```c
  ml3_measurement_config_t config = {
    0U, 1U, 4U, 2U, (uint16_t)ml3_cycles,
    0U,
    (uint32_t)ml3_warmup_ms,
    ML3_CONFIG_DISCHARGE_THRESHOLD_MV / 2U,
    ML3_CONFIG_DISCHARGE_TIMEOUT_MS,
    1U
  };
  config.vrefint_calibration_word = *ML3_TARGET_VREFINT_CAL_ADDR;
  ```

  Define `ML3_TARGET_VREFINT_CAL_ADDR` locally as the STM32L072 factory word at `0x1FF80078UL`, matching the in-tree bench adapter. Get default ADC timeouts, then set `conversion_ms = 10U`; the x256, 160.5-cycle conversion is about 5.54 ms, so the existing 5-ms core default is not adequate.

  The callbacks must have these semantics:

  ```c
  static bool ml3_target_configure_analog_pins(void *context) {
    return ml3_stm32_adc_port_init((ml3_stm32_adc_port_context_t *)context);
  }

  static bool ml3_target_set_power_5v(void *context, bool enabled) {
    (void)context;
    HAL_GPIO_WritePin(PWR_OUT_PORT, PWR_OUT_PIN,
      enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return true;
  }

  static bool ml3_target_set_thermistor_excitation(void *context, bool enabled) {
    (void)context;
    (void)enabled;
    return true;
  }
  ```

  The radio callback must reject an active `LoRaMacState` before it calls `Radio.Sleep()`. The watchdog callback calls the existing `IWDG_Refresh()` and returns true. The reset callback snapshots `RCC->CSR` without clearing it. Use the exported ADC-port clock for `now_ms`; do not duplicate RTC-wrap state in `bsp.c`.

  `on_process` must convert each complete ABBA raw pair to microvolts using the pre-VREF VDDA, derive the two PA4 voltages using their respective VDDAs and the configured 500000-ppm divider, derive die temperature from the STM32L072 factory `TEMPSENSOR_CAL1_ADDR` and `TEMPSENSOR_CAL2_ADDR` after normalizing the raw code to 3.0 V, then call `ml3_quality_thresholds_from_config()` and `ml3_quality_evaluate()`. If the thresholds are pending or required measurement evidence is absent, return false so the existing core cleanup takes the error path. Set `has_calibration_status = true` and `calibration_valid = true`: ID zero deliberately means uncorrected, not a corrupt calibration record. Set `has_thermistor_status = true` and `thermistor_valid = true`; the deliberately ignored PA2 raw conversion is not a thermistor fault.

  `on_build_payload` must make `corrected_diff_available = false`, set all uncorrected signal/VREF/V5/noise/die-temperature availability fields from the processed result, set `soil_temperature_available = false`, and set `calibration_id = 0U`. Build only the 25-byte routine payload. `on_queue` sends its persistent frame at `ML3_CONFIG_FPORT` as an unconfirmed uplink and returns whether `LORA_send` returned `LORA_SUCCESS`.

  Keep `BSP_ML3_Service` cooperative: start once when a request is pending, call `ml3_measurement_step()` once per service iteration, mirror the core active state into `ml3_active`, and clear the pending request when the engine returns DONE or ERROR. Make `BSP_ML3_Abort` call `ml3_measurement_abort()` before it clears its public state. Retain every existing false-gate return and do not change `ML3_CONFIG_GATE0_READINESS`, `ML3_CONFIG_ACQUISITION_READY`, or `ML3_CONFIG_DEPLOYABLE`.

  The settings API defaults to 1500 ms and four cycles. Accepted idle settings
  update the stored setting and the initialized guarded core configuration;
  `BSP_ML3_SetCycles()` supports the documented three-to-eight cycle range.
  Quality input requires the selected cycle count as well as a complete
  valid-cycle count. `BSP_ML3_SetRaw(0U)` remains a no-op, while a nonzero raw
  request returns false because Phase 3 has no diagnostic payload path.

- [x] **Step 4: Run the narrow contract and verify GREEN**

  Run:

  ```bash
  bash tests/host/ml3_target_integration_contract.sh
  ```

  Expected: `ml3 target integration contract: OK`, including the no-PA2/no-PB4 and default-unreachable assertions.

### Task 4: Full verification and phase commit

**Files:**

- Modify: only the files listed in Tasks 1–3 and this plan

- [x] **Step 1: Run the full evidence set**

  Run from the worktree root:

  ```bash
  bash tests/host/run_ml3_host_tests.sh
  bash tests/host/ml3_target_integration_contract.sh
  make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -B
  make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -B BENCH=1
  git diff --check
  node /home/phil/Repos/osi-os/.claude/skills/anti-slop-writing/slop-check.js \
    docs/superpowers/plans/2026-08-10-task10b-phase3-service.md
  ```

  Expected: every command exits 0. Record text/data/bss and available flash/RAM. Report only baseline vendor/newlib warnings if they occur.

- [x] **Step 2: Commit the reviewed phase**

  Run:

  ```bash
  git add \
    'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_quality.h' \
    'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_stm32_adc_port.h' \
    'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_stm32_adc_port.c' \
    'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/bsp.c' \
    tests/host/ml3_quality_test.c \
    tests/host/ml3_target_integration_contract.sh \
    docs/superpowers/plans/2026-08-10-task10b-phase3-service.md
  git commit -m "feat: wire guarded ML3 measurement service"
  ```

  Do not push. Stop after independent specification and code-quality reviews. Phase 4 decides the reduced trial readiness expression and activation, and must not treat this build-only commit as a bench observation.

## Execution evidence

- RED: `bash tests/host/run_ml3_host_tests.sh` rejected the changed quality
  expectation while `ML3_QUALITY_DIFF_MAX_UV` was still 1100000. The boundary
  assertion for 1200000 also failed, as required.
- GREEN: the same host runner passed after the authorized header change. The
  quality test now accepts 1200000 and rejects 1200001 with
  `ML3_QUALITY_FLAG_DIFF_RANGE`.
- RED: `bash tests/host/ml3_target_integration_contract.sh` first rejected the
  non-public ADC elapsed-time interface, then rejected 53 missing Phase 3
  service requirements. GREEN: it passed after the clock export and guarded
  target wiring.
- Negative contract mutations removed `payload.soil_temperature_available =
  false` and inverted the PB5 active-low ternary. Each made the target
  contract exit 1 with the corresponding missing-payload or polarity failure,
  then was restored.
- Review correction: a new executable 3.3-V die-temperature fixture made the
  inverse raw normalization fail. The target now scales the oversampled raw
  code by VDDA divided by the factory 3.0-V supply before reducing it to the
  12-bit factory scale. The fixture gives 50.00 C for its chosen values rather
  than the inverse ratio's 11.80 C.
- Review correction: the guarded service now aborts an active or pending core
  acquisition if ML3 mode is lost before it clears the public request state.
  The contract traces core abort to its PB5-off cleanup callback. A negative
  mutation that cleared the public state first was rejected, then restored.
- Contract hardening: all ML3 BSP regions are now scanned for forbidden
  PB4/PA2/thermistor operations. The contract compiles the unchanged default
  configuration to a temporary BSP object and verifies that every public ML3
  entry point has no effective ADC, PB5, radio, or queue call. Temporary
  false-gate PB5 enable, PB4 write, and Init PB5-enable mutations were
  rejected. It requires an unconditional valid-cycle mismatch rejection before
  rail evidence is published; a fault-conjoined mutation was rejected. The
  temporary-file trap now cleans both contract artifacts, confirmed by stable
  before/after counts, and the contract checks the pre-VREF ABBA and pre/post
  PA4 VDDA pairings.
- Settings correction: the target contract first rejected hard-coded init
  values, setting resets, missing guarded core updates, and accepted raw
  diagnostics. The service now initializes from the selected 1500-ms/four-cycle
  defaults, propagates accepted idle warmup and three-to-eight cycle settings
  into the guarded core configuration, and rejects nonzero raw requests.
- Strict enabled-path correction: a whole-`bsp.c` forced-ready compile was RED
  on pre-existing legacy diagnostics outside the Phase 3 regions. The contract
  now writes a temporary translation unit from the real target includes,
  target dependency includes, ML3 state, guarded target block, and extracted
  `BSP_ML3_Init`, service, abort, request, and setter functions. It obtains
  target includes and defines from `make -Bn`, makes every requested warning
  fatal, and checks source markers plus all seven readiness guards before
  compiling. Vendor include paths are system headers only for that narrow
  target-source check; no warning option is suppressed. Mutating the guarded
  `LORA_send` call to `LORA_send_BROKEN` made the strict harness exit 1 with an
  implicit-declaration error, then passed after restoration.
- `make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -B`
  exited 0. `build/lora.elf`: text 110760, data 820, bss 9600; 111580 B of
  192 KB flash and 10412 B of 20 KB RAM reported by the linker.
- `make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -B BENCH=1`
  exited 0. `build/lora-bench.elf`: text 112432, data 820, bss 9696; 113252 B
  of 192 KB flash and 10508 B of 20 KB RAM reported by the linker.
- Both target builds retain pre-existing vendor and newlib warnings. The
  Phase 3 code is excluded while `ML3_CONFIG_ACQUISITION_READY` is false, so
  the normal image has no reachable ML3 ADC-port initialization, PB5 enable,
  radio sleep, or uplink queue path.

Phase 4 must supply quality thresholds before activation. Until then,
`ml3_quality_thresholds_from_config()` returns configuration pending and the
Phase 3 process callback returns false, allowing the existing core cleanup to
turn the rail off. No hardware observation or bring-up procedure is recorded
by this build-only phase.
