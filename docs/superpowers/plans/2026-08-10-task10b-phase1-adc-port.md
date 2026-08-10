# Task 10B Phase 1 ADC port implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an unregistered STM32L072 implementation of `adc_precision_port_t` that compiles in both target builds while every ML3 acquisition gate remains false.

**Architecture:** `ml3_stm32_adc_port.c` is the only module that writes ADC1, ADC common-control, and SYSCFG ADC-buffer registers. It returns a static `adc_precision_port_t` and accepts a small context that records the RTC tick at `VREFEN` and carries its millisecond clock across an RTC wrap. Phase 1 does not include the port in `BSP_ML3_Service`, so no callback is reachable in the default or bench firmware.

**Tech Stack:** C99, STM32L072 CMSIS register definitions, existing `hw_rtc` tick clock, GCC and MDK project manifests, shell integration contract.

---

### Task 1: Add a red target-integration contract

**Files:**

- Modify: `tests/host/ml3_target_integration_contract.sh`
- Test: `tests/host/ml3_target_integration_contract.sh`

- [ ] **Step 1: Require the new adapter in each target manifest**

Add variables for `src/ml3_stm32_adc_port.c`, `inc/ml3_stm32_adc_port.h`, and the GCC Makefile. Require each file to exist; require the source once in the strict target-adapter list and once in `MDK-ARM/STM32L072CZ-Nucleo/Lora.uvprojx`. Require the source to include the explicit VREFINT timestamp and two-tick settlement guard, `ADC_CFGR2_OVSE`, `ADC_OVERSAMPLING_RATIO_256`, `ADC_RIGHTBITSHIFT_4`, `ADC_SAMPLETIME_160CYCLES_5`, `ADC_CR_ADSTP`, `ADC_CR_ADDIS`, `ADC_CR_ADCAL`, `ADC_CR_ADEN`, `ADC_ISR_ADRDY`, `ADC_ISR_EOC`, and `ADC_ISR_OVR`.

- [ ] **Step 2: Run the contract and observe RED**

Run: `bash tests/host/ml3_target_integration_contract.sh`

Expected: failure naming the absent `ml3_stm32_adc_port.c` and its missing GCC/MDK registration. The existing build-only compile assertion must still pass, proving this is a missing-port failure rather than an activation-gate change.

### Task 2: Implement the unregistered STM32L072 ADC port

**Files:**

- Create: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_stm32_adc_port.h`
- Create: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_stm32_adc_port.c`

- [ ] **Step 1: Declare the context and factory**

Expose only:

```c
typedef struct {
  uint32_t vrefint_enable_tick;
  uint32_t last_rtc_tick;
  uint32_t rtc_epoch_ms;
  bool rtc_tick_initialized;
} ml3_stm32_adc_port_context_t;

const adc_precision_port_t *ml3_stm32_adc_port_get(void);
bool ml3_stm32_adc_port_init(ml3_stm32_adc_port_context_t *context);
```

`init` rejects a null context before touching hardware, zeros an accepted context, and owns both the SYSCFG and ADC peripheral clocks. It must not configure, enable, or start ADC1. The callbacks reject any context that was not accepted by `init`.

- [ ] **Step 2: Implement thin callbacks with no loops or delays**

Use the RTC tick conversion for `now_ms`, carrying milliseconds across the raw 32-bit tick wrap so the core's unsigned elapsed-time arithmetic remains valid. Implement stop, disable, calibration, enable, ready, channel selection, start, completion, and read callbacks with one register operation or status read each. Clear `ADRDY` before setting `ADEN`; capture `OVR` before reading `DR`, then acknowledge `OVR`. Channel selection maps only ADC channels 0, 1, 2, 4, 17, and 18 to their named `ADC_CHSELR_CHSEL*` bits; unsupported channels map to zero.

- [ ] **Step 3: Apply the fixed ADC configuration only while disabled**

In `configure`, write 12-bit/right-aligned/single-trigger/no-DMA/no-continuous `CFGR1`, PCLK/4 plus ×256/right-shift-4 oversampling in `CFGR2`, and 160.5-cycle `SMPR`. The core calls this only after `ADEN` is clear; this function must not poll or delay.

- [ ] **Step 4: Make the VREFINT delay explicit and non-blocking**

`enable_vrefint_gate` sets `ADC_CCR_VREFEN` and records `HW_RTC_GetTimerValue()` immediately afterwards. The four readiness predicates read the appropriate `SYSCFG_CFGR3` flags. `is_reference_settled` returns true only after `(HW_RTC_GetTimerValue() - vrefint_enable_tick) >= 2U`. Two 1024-Hz ticks are about 1.95 ms, exceed the STM32L072 10 µs VREFINT minimum, and allow the production core's existing first-conversion discard to begin after the residual settling tail. This callback never waits; `adc_precision_prepare` owns the bounded polling.

- [ ] **Step 5: Keep the port unreachable**

Do not include the new header in `bsp.c`, call the factory, instantiate the context, alter `ML3_CONFIG_*`, or modify any of the seven ML3 modules. `BSP_ML3_Service` remains the false-gate no-op.

### Task 3: Register and verify the build-only object

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc/Makefile`
- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/MDK-ARM/STM32L072CZ-Nucleo/Lora.uvprojx`
- Modify: `tests/host/ml3_target_integration_contract.sh`

- [ ] **Step 1: Compile the adapter under the existing strict CFLAGS**

Create a `TARGET_ADAPTER_SRCS` list containing `src/ml3_stm32_adc_port.c`, derive its objects alongside `ML3_OBJS`, add them to `ALL_OBJS`, and compile them with `ML3_STRICT_CFLAGS`. Add exactly one matching MDK source entry. Do not add the adapter to the seven-module `ML3_SRCS` list.

- [ ] **Step 2: Run the Phase 1 green gates**

Run:

```sh
bash tests/host/ml3_target_integration_contract.sh
bash tests/host/run_ml3_host_tests.sh
make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -B
make -C 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc' -B BENCH=1
```

Expected: each command exits zero. The default ELF must still lack reachable measurement-service entry use because `ML3_CONFIG_ACQUISITION_READY` remains zero.

- [ ] **Step 3: Commit Phase 1**

```sh
git add tests/host/ml3_target_integration_contract.sh \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_stm32_adc_port.h' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/ml3_stm32_adc_port.c' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/gcc/Makefile' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/MDK-ARM/STM32L072CZ-Nucleo/Lora.uvprojx' \
  docs/superpowers/plans/2026-08-10-task10b-phase1-adc-port.md
git commit -m 'feat: add STM32 ML3 ADC port'
```

Phase 2 uses the current Task 10B brief's PB5-powered rail values as owner hardware observations, not bench measurements. Their comments must preserve that basis and explain that the fitted divider is required to keep PA4 from floating into a false supply-failure result.
