# Agent prompt — Task 10: port adapters and first buildable production firmware

Hand this file to the implementing agent as its brief. Everything it needs is either here or reachable from the paths below. Written 2026-08-07, branch head `f60448d`.

---

## Mission

Task 10 has a build-only boundary before activation. The measurement engine is written, host-tested and reviewed; the thin layer connecting it to STM32L072 hardware remains Gate-0-gated for activation. Gate 0 hardware characterization is incomplete for activation, so a default image must not power the probe, take a physical reading, or queue a LoRaWAN frame.

You will not have hardware. Your deliverable is code that compiles clean for the target, keeps every host test green, and comes with a bring-up checklist precise enough for the project owner to execute at the bench.

## Where things are

- Repo: `Open-Smart-Irrigation/LoRa_STM32` (the `Project-OSI` URL redirects there), branch **`feature/ml3-precision-adc`**.
- Working copy: `/home/phil/.config/superpowers/worktrees/LoRa_STM32/ml3-precision-adc`
- App root: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/` — **the parenthesis is literal; quote every shell path**.
- Build: `make -C gcc` (deployable image) and `make -C gcc BENCH=1` (bench diagnostic image). Toolchain `arm-none-eabi-gcc` 16.1, already installed. See `gcc/README.md`.
- Host tests: `bash tests/host/run_ml3_host_tests.sh` — **run from the worktree root, not the app directory.**

## Read these first, in this order

1. **`docs/ml3-port-adapter-spec.md`** — your primary contract. Every port seam, the decisions already taken, and the mandatory EEPROM write-completion requirement.
2. **`docs/gate0-records/M013437-A840412D385E7D00/section4-adc-characterization.md`** — the incomplete hardware-characterization record. Treat it as constraints and evidence to verify, not authorization to activate the target.
3. **`docs/2026-07-12-lsn50v2-ml3-firmware-plan.md`** (v2.1) — the governing design. It wins on any conflict with other documents; if you find a genuine conflict, stop and report rather than choosing.
4. **`docs/ml3-firmware-status-and-roadmap.md`** — where this task sits in the wider programme.
5. The modules you are serving: `inc/adc_precision.h`, `inc/ml3_measurement.h`, `inc/ml3_calibration.h`, `inc/bsp.h`. Read the headers as contracts before reading implementations.

## What already exists — do not rebuild or redesign it

Seven modules are complete, host-tested under a strict `-Werror` set, and have passed multiple review rounds including independent fuzz-verification of their arithmetic: `adc_precision`, `ml3_measurement`, `ml3_calibration`, `ml3_thermistor`, `ml3_quality`, `ml3_payload`, `ml3_at_commands`. They are pure logic behind port structs. **Your job is to implement the ports, not to touch these modules.** If you believe one contains a defect, report it — do not fix it as a side effect of this task.

Also already done: the GCC build environment and a bench diagnostic image (`AT+ML3ADC`). Gate 0 hardware characterization remains incomplete for activation.

## Hard constraints — each of these was verified in-tree, not assumed

1. **The flash driver does not wait for EEPROM writes to complete.** `HAL_FLASHEx_DATAEEPROM_Program` calls `FLASH_WaitForLastOperation` **only on its error path** (`Drivers/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_flash_ex.c:770-774`). A return of `HAL_OK` means "write issued", not "word programmed". The calibration store's safety rests on writing the CRC last and verifying by read-back; without an explicit wait that verification can race an in-flight word and confirm a record that was never durably stored. **Your adapter must wait for completion after every program call, before any read-back.** Vendor files must not be edited, so the wait belongs in your adapter.
2. **Do not use ST's `LL_GetUID_Word2()`** (`Drivers/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_utils.h:207`). It reads `UID_BASE + 8` (`0x1FF80058`); the third unique-ID word is at `0x1FF80064`. Read the three words directly.
3. **The vendor's factory reset erases 40 bytes beyond anything it writes** (`0x08080000`–`0x080800D7`, from `EEPROM_USER_START/END_ADDR_CONFIG` in `Drivers/BSP/Components/flash_eraseprogram/flash_eraseprogram.h:85-86`). The allocated slots already clear this; do not "reclaim" the apparent gap.
4. **The vendor HAL omits the VREFINT stabilization delay.** It applies `ADC_TEMPSENSOR_DELAY_US` for the temperature sensor but never the documented `LL_ADC_DELAY_VREFINT_STAB_US` on the VREFINT path. On the bench tool this made the first reference sample read near full scale and skewed the derived supply voltage by 2.6%. **Verify whether the production `adc_precision` path is exposed to the same defect** — it already performs a channel-change discard, which may cover it, but the delay specifically must be confirmed. A 2.6% reference error would propagate into every calibrated reading. Report your finding either way.

## Safety invariants — non-negotiable, they protect field hardware

- Every abort path must disable the +5 V rail and thermistor excitation. `cleanup_controls` already does this; your adapter must not introduce a path that bypasses it.
- Do not move or remove watchdog refresh points.
- The +5 V control is **PB5, active-low** (`PWR_OUT_PORT`/`PWR_OUT_PIN` in `inc/stm32l0xx_hw_conf.h:265-267`; `bsp.c:522` writes `GPIO_PIN_RESET` to enable). Getting this inverted powers a probe continuously, which the manufacturer explicitly forbids.
- The ML3 tolerates 0.5–1 s of warm-up and must not be powered continuously.
- Preserve the ES0292 ADC sequencing the modules already encode (stop → disable → configure → calibrate → enable → convert; no `CFGR` writes while `ADEN` is set).

## The work

### Execution status

**Task 10A** permits target configuration and adapter-object compilation only while all Gate 0/Phase 2 hardware readiness macros remain zero, and `ML3_CONFIG_ACQUISITION_READY` and `ML3_CONFIG_DEPLOYABLE` remain false. It forbids adapter registration and must not make ADC access, PB5 control, thermistor excitation, EEPROM write, LoRa queue, sample processing, or physical sampling reachable.

**Task 10B** is Gate-0-gated. Do its phases only after Gate 0 supplies the required values, and stop for review between them rather than delivering one large change.

**Task 10B, Phase 1 — ADC port.** Implement `adc_precision`'s port struct against the STM32L0 registers/HAL. Mirror the vendor's proven configuration in `HW_AdcInit` (`src/stm32l0xx_hw.c`) where it is sound, but honour the module's own sequencing contract where they differ. Resolve constraint 4 here and say what you found.

**Task 10B, Phase 2 — EEPROM port.** Implement `ml3_calibration_storage_port_t` at the decided addresses: slot 0 `0x08080100`, slot 1 `0x08080C00`, 2048 B each, runtime `slot_capacity` **512 B**. Constraint 1 applies here and is the highest-risk item in this task. Implement `device_id_hash` as CRC-32/ISO-HDLC (reuse the module's existing `ml3_calibration_crc32`) over the 12-byte little-endian image of the UID words at `0x1FF80050 / 0x1FF80054 / 0x1FF80064`, remapping a result of `0` to `0xA5A5A5A5`. The exact byte and word order must be unambiguous enough that an external Node.js generator reproduces it without negotiation — write that ordering down in a comment and in your report.

**Task 10B, Phase 3 — service wiring.** Replace the no-op body of `BSP_ML3_Service` per spec §5: instantiate the port structs once, initialise the measurement engine from the existing settings, drive `ml3_measurement_step` to completion across service calls, keep `ml3_active` true so the main loop blocks low-power mode during acquisition, and route `BSP_ML3_Abort` to `ml3_measurement_abort`.

**Task 10B, Phase 4 — configuration.** Fill the `GATE0-PENDING` macros in `inc/ml3_config.h` only from completed Gate 0 evidence and the manufacturer specification. Every value must trace to a record line or source in the report. **Invent nothing.** If a macro has no traceable basis, leave it pending and say so.

**Task 10B, Phase 5 — gates.** Only once Gate 0 and phases 1–4 are complete and reviewed, flip the readiness macros so `ML3_CONFIG_ACQUISITION_READY` evaluates true, and confirm the acquisition engine is now linked into the default image (`arm-none-eabi-nm`). Note this deliberately changes the deployable image; the byte-identity gate that governed bench-tool work does not apply here and must not be cited as a reason to avoid the change.

## Verification gates — evidence required, not assertions

- `bash tests/host/run_ml3_host_tests.sh` green (from the worktree root).
- `bash tests/host/ml3_target_integration_contract.sh` green; extend it with grep-assertions for the new wiring, following its existing style.
- `make -C gcc` builds clean; report text/data/bss and the headroom against 192 KB flash / 20 KB RAM.
- `make -C gcc BENCH=1` still builds clean.
- Your new adapter sources must compile under the same strict flag set the seven ML3 modules use (see `gcc/Makefile`) — including `-Wconversion` and `-Wshadow`.
- After Phase 5, confirm via `arm-none-eabi-nm` that measurement-engine symbols are present in the default ELF (they are absent today by design).

## Deliverables

1. The code, in phase-sized commits with conventional-commit subjects. Do not push.
2. A written report per phase: what changed and where, your finding on constraint 4, the exact hash byte-ordering, per-macro citations for Phase 4, and all gate evidence.
3. **A bring-up checklist** the project owner can execute at the bench, in plain accessible language, one step at a time, with the expected observation at each step and an explicit stop condition if it disagrees. Assume a multimeter and a serial console (`picocom -b 9600 /dev/ttyUSB0`, `AT+ML3VER?` — no `=`), and that flashing is done with STM32CubeProgrammer's GUI. First-power-on safety comes first: the +5 V rail must behave before a probe is attached.

## What not to do

- Do not edit vendor sources. Work around vendor defects in your own adapter and document the workaround.
- Do not modify the seven ML3 modules, their tests, or the measurement/quality/payload logic.
- Do not invent configuration values, calibration constants, or timing figures.
- Do not claim hardware behaviour you have not observed — you have no hardware. Say "expected" where you mean expected.
- Do not push, and do not flip readiness gates before phases 1–4 are reviewed.
- If the plan and the port spec disagree, stop and report; the plan governs but the conflict is worth surfacing.
