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

### Decisions taken 2026-08-07 (independent consult; arithmetic and vendor ranges re-verified in-session)

**Slot map.** Two 2048 B slots: **slot 0 at `0x08080100`**, **slot 1 at `0x08080C00`**. Runtime `port.slot_capacity` = **512 B**, deliberately below the 2092 B format maximum.

| Region | Range | Note |
|---|---|---|
| Vendor (erased on every `AT+FDR`) | `0x08080000`–`0x080800D7` | `EEPROM_USER_START/END_ADDR_CONFIG` = base+0x04*24 … +0x04*30 (`Drivers/BSP/Components/flash_eraseprogram/flash_eraseprogram.h:85-86`); the erase loop runs 40 bytes **past** the vendor's highest write, so allocating immediately after the last written byte would be silently wiped in the field |
| Slot 0 | `0x08080100`–`0x080808FF` | wholly inside EEPROM bank 1 (`0x08080000`–`0x08080BFF`) |
| Slot 1 | `0x08080C00`–`0x080813FF` | wholly inside bank 2 (`0x08080C00`–`0x080817FF`) |

Each slot is bank-contained so the A/B commit can always read the intact slot while the other is being written. The 512 B runtime cap is a RAM constraint, not an EEPROM one: full-size records would need buffers approaching the free RAM budget. **If the calibration model outgrows 512 B, revisit by streaming the write rather than raising the buffer.**

**`device_id_hash`.** **CRC-32/ISO-HDLC**, 32-bit — reusing the module's existing `ml3_calibration_crc32` rather than adding FNV-1a, so firmware and bench tooling share one already-tested implementation. Computed over the 12-byte little-endian memory image of the UID words at `0x1FF80050 / 0x1FF80054 / 0x1FF80064`, with a result of `0` remapped to `0xA5A5A5A5` so zero stays available as "unset". This binds a record to the node it was produced for; it is **not** a security control and no adversary model applies.

⚠️ **Do not use ST's `LL_GetUID_Word2()`** (`Drivers/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_utils.h:207`): it reads `UID_BASE + 8` (`0x1FF80058`), not the correct `0x1FF80064`. Verified in-tree. Read the three words directly.

**Commit trigger.** `BSP_ML3_CalibrationChunk` stages and CRC-checks in RAM today; Task 10 adds the `ml3_calibration_store` call now that both decisions above are settled.

### Mandatory: the HAL does not wait for EEPROM writes to complete

`HAL_FLASHEx_DATAEEPROM_Program` issues the store and then calls `FLASH_WaitForLastOperation` **only on its error path** (`Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_flash_ex.c:770-774`, verified in-tree). `HAL_OK` therefore means "write issued", not "word programmed". A naive adapter would let the two-phase commit's read-back verify race an in-flight word and report success for a record that is not durably committed — surfacing later as a silently wrong calibration on a live node.

**The adapter must wait for completion explicitly after every program call, before any read-back.** Vendor files must not be edited, so the wait belongs in the adapter.

The store path's two-phase commit (poison target CRC → write body → write CRC → read-back verify) was fault-injection tested on host at every write index; the adapter must preserve write granularity such that the final CRC word is the last thing written.

## 5. `BSP_ML3_Service` wiring

The service loop (called every `main()` iteration) currently no-ops behind `ML3_CONFIG_ACQUISITION_READY`. Task 10 replaces the no-op body with: instantiate the two port structs (static, once), `ml3_measurement_init` with config from UCI-equivalent settings (`BSP_ML3_SetWarmup/SetCycles` already maintain them), then on `ml3_request_pending` → `ml3_measurement_start` and drive `ml3_measurement_step` to completion across service calls, keeping `ml3_active` true so `main()` blocks low-power mode during acquisition. `BSP_ML3_Abort` must call `ml3_measurement_abort` (which runs the fail-safe cleanup).

## 6. Verification obligations

- Host: the existing suite (`tests/host/run_ml3_host_tests.sh`) must stay green; adapter code itself is target-only and out of host scope.
- Target: bench-verify the ES0292 sequence with a logic analyzer or register trace on first bring-up (stop → disable → configure → calibrate → enable → convert order; no `CFGR` writes with `ADEN` set).
- The §4.3 interference matrix and §4.4 10,000-cycle soak run on this adapter; their acceptance numbers are already fixed in the plan.
- Reproducible build (§3.5): record toolchain, archive the map file, second-party hash reproduction before any release tag.
