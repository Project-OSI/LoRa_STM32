# ML3 port adapter specification (Task 10 target integration)

The ML3 modules are pure logic behind three function-pointer port structs; no code on this branch touches ADC, EEPROM, or GPIO registers for acquisition. This document specifies what the concrete STM32L072 adapters must implement so that `BSP_ML3_Service` can drive a real measurement once Gate 0 supplies the pending `ml3_config.h` values. Register references are RM0376 (reference manual) and ES0292 (errata); plan §3.7–§3.8 governs sequencing.

Prerequisite: Gate 0 complete and its macros filled (see `ml3-gate0-bench-runbook.md`). Writing these adapters earlier means inventing hardware constants, which the execution process explicitly forbids.

## 1. `adc_precision_port_t` — the ADC seam

Declared in `inc/adc_precision.h:55-79` (23 callbacks + `port_ctx`). The core (`adc_precision.c`) owns all sequencing, timeout, and discard logic; each callback is a thin register accessor and must not block, poll, or sleep — polling loops live in the core, driven by `now_ms`.

| Callback | STM32L072 implementation |
|---|---|
| `now_ms` | `HW_RTC_GetTimerValue`-derived milliseconds or SysTick counter; monotonic across the acquisition |
| `request_stop_conversion` | set `ADC_CR_ADSTP` |
| `is_conversion_stopped` | `ADC_CR_ADSTART == 0` |
| `request_disable_adc` | set `ADC_CR_ADDIS` |
| `is_adc_disabled` | `ADC_CR_ADEN == 0` |
| `configure` | write `ADC_CFGR1/2`, sampling time 160.5 cycles (`SMPR`), oversampling ×256 shift 4 (`CFGR2.OVSR/OVSS/OVSE`), clock PCLK/4 → 8 MHz. Only ever called by the core with the ADC disabled (ES0292: never write `CFGR1/2` with `ADEN` set) |
| `request_self_calibration` | set `ADC_CR_ADCAL` (ADC disabled) |
| `is_calibration_complete` | `ADC_CR_ADCAL == 0` (read `ADC_DR`/`CALFACT` per RM0376 note) |
| `request_enable_adc` | clear `ADC_ISR_ADRDY` then set `ADC_CR_ADEN` |
| `is_adc_ready` | `ADC_ISR_ADRDY == 1` |
| `enable_vrefint_gate` / `enable_temperature_gate` | `ADC_CCR.VREFEN` / `ADC_CCR.TSEN` |
| `enable_vrefint_buffer_gate` / `enable_temperature_buffer_gate` | `SYSCFG_CFGR3.ENBUF_VREFINT_ADC` / `ENBUF_SENSOR_ADC` (verify exact bit names against RM0376 at implementation — plan §3.7) |
| `is_vrefint_ready` / `is_temperature_ready` / buffer variants | the matching `SYSCFG_CFGR3` readiness flags (`VREFINT_RDYF` et al.) |
| `is_reference_settled` | readiness flags plus the datasheet start-up time bound, whichever is later |
| `select_channel` | write `ADC_CHSELR` single-bit for channels 0 (PA0 HI), 1 (PA1 LO), 4 (PA4 V5 monitor), 17 (VREFINT), 18 (die temp), plus the Gate 0 thermistor channel |
| `start_conversion` | set `ADC_CR_ADSTART` |
| `is_conversion_complete` | `ADC_ISR_EOC == 1` |
| `read_raw` | read `ADC_DR` immediately, report `ADC_ISR_OVR` in `*overrun`, clear OVR |

Contract points the adapter must honor:

- Timeout values come from `adc_precision_timeouts_t`; the adapter never enforces its own.
- The core performs one discard conversion after every channel change and re-establishes state after a recoverable conversion fault; the adapter needs no memory of previous channels.
- VREFINT factory calibration word: `VREFINT_CAL` at `0x1FF80078` (3.0 V, 25 °C) — read once at init, passed to the measurement config as `vrefint_calibration_word`.
- Die-temperature scaling uses `TSENSE_CAL1` (`0x1FF8007A`) and `TSENSE_CAL2` (`0x1FF8007E`); the raw IN18 code lands in `ml3_measurement_result_t.die_temp_raw` and the centi-°C conversion happens in the `on_process` callback (see §3), not in the adapter.

## 2. `ml3_measurement_port_t` — board control seam

Declared in `inc/ml3_measurement.h:101-113`. These callbacks do touch board state:

| Callback | Implementation |
|---|---|
| `now_ms` | same time base as the ADC port |
| `read_reset_cause` | `RCC->CSR` reset flags, then clear (`RMVF`) |
| `request_radio_sleep` | put SX1276 to sleep via the existing radio driver before any analog work |
| `configure_analog_pins` | PA0/PA1/PA4 + thermistor pin to analog mode, no pull (plan §3.2: "GPIO analog mode, no pull") |
| `set_power_5v` | drive PB5 per the Gate 0 measured polarity (`ML3_CONFIG_PB5_ACTIVE_LOW`); the hardware default must already be fail-safe off — firmware is the second layer, not the only one |
| `set_thermistor_excitation` | the Gate 0 excitation GPIO, push-pull, high only during the read |
| `watchdog_refresh` | `IWDG_Refresh`; the core calls it at the plan §3.8 points (before +5 V on, before/after the ABBA burst, before TX) |
| `on_process` | see §3 |
| `on_build_payload` | see §3 |
| `on_queue` | hand the built frame to the LoRaWAN stack on FPort `ML3_CONFIG_FPORT` (13) |

Fail-safe invariant: every error path in the core already forces `set_power_5v(false)` and `set_thermistor_excitation(false)`; the adapter must make both calls unconditionally safe to invoke in any state, including before init completes.

## 3. Result-stage callbacks — where the remaining wiring lives

The measurement core hands `on_process` / `on_build_payload` / `on_queue` a `ml3_measurement_result_t` (raw ABBA codes, µV statistics, VDDA pre/post, raw die-temp and thermistor codes, fault flags). Task 10 implements this chain in `bsp.c`:

1. **Process**: die-temp raw → centi-°C via `TSENSE_CAL1/2`; thermistor raw → soil temperature via `ml3_thermistor_convert` (needs the Phase 2 manual lookup table, `ML3_CONFIG_THERMISTOR_TABLE_*`); median differential → `ml3_calibration_apply` against the EEPROM record; V5 monitor raw → mV via the measured divider ratio.
2. **Quality**: assemble `ml3_quality_input_t` from the result + processed values, call `ml3_quality_evaluate` with thresholds from `ml3_quality_thresholds_from_config` (Phase 2 macros).
3. **Payload**: `ml3_payload_build_routine` (25 bytes, big-endian, sentinels per Appendix A); on first invalidating condition, `ml3_payload_auto_diag_is_eligible` / `_mark_queued` gate the multi-frame diagnostic (6 h per fault signature). The rate-limiter table is RAM-only today; decide whether it must survive a watchdog reset (plan is silent — a reset re-arms one diagnostic, which is acceptable within the ≤0.1 % watchdog budget of §4.4).
4. **Queue**: submit via the vendor `lora_AppData_t` path; transmission must never overlap analog acquisition (the state machine guarantees ordering; the adapter must not re-enter `ml3_measurement_step` from the radio IRQ).

## 4. `ml3_calibration_storage_port_t` — EEPROM seam

Declared in `inc/ml3_calibration.h:65-70` (`read`, `write`, `context`, `slot_capacity`). Implementation: STM32L0 data EEPROM via `HAL_FLASHEx_DATAEEPROM_Unlock/Program/Lock`, word-aligned writes.

Open decisions that block this adapter (deliberately unresolved — no fabricated constants):

| Decision | Constraint |
|---|---|
| Slot base addresses and capacity | two slots, each ≥ `ML3_CALIBRATION_MAX_RECORD_SIZE` (2092 B), inside the 6 KB data EEPROM (`0x08080000`–), clear of Dragino's existing `EEPROM_Store_Config` usage — map the vendor layout first |
| `device_id_hash` algorithm | must be reproducible by the bench tooling that generates records (`AT+ML3CAL=` chunks); propose FNV-1a over the 96-bit unique ID (`0x1FF80050/54/64`), record the choice in this file when taken |
| Commit trigger | `BSP_ML3_CalibrationChunk` today stages bytes in RAM and CRC-checks them; Task 10 adds the `ml3_calibration_store` call once both above are decided |

The store path's two-phase commit (poison target CRC → write body → write CRC → read-back verify) was fault-injection tested on host at every write index; the adapter must preserve write granularity such that the final CRC word is the last thing written.

## 5. `BSP_ML3_Service` wiring

The service loop (called every `main()` iteration) currently no-ops behind `ML3_CONFIG_ACQUISITION_READY`. Task 10 replaces the no-op body with: instantiate the two port structs (static, once), `ml3_measurement_init` with config from UCI-equivalent settings (`BSP_ML3_SetWarmup/SetCycles` already maintain them), then on `ml3_request_pending` → `ml3_measurement_start` and drive `ml3_measurement_step` to completion across service calls, keeping `ml3_active` true so `main()` blocks low-power mode during acquisition. `BSP_ML3_Abort` must call `ml3_measurement_abort` (which runs the fail-safe cleanup).

## 6. Verification obligations

- Host: the existing suite (`tests/host/run_ml3_host_tests.sh`) must stay green; adapter code itself is target-only and out of host scope.
- Target: bench-verify the ES0292 sequence with a logic analyzer or register trace on first bring-up (stop → disable → configure → calibrate → enable → convert order; no `CFGR` writes with `ADEN` set).
- The §4.3 interference matrix and §4.4 10,000-cycle soak run on this adapter; their acceptance numbers are already fixed in the plan.
- Reproducible build (§3.5): record toolchain, archive the map file, second-party hash reproduction before any release tag.
