# LSN50V2 Chameleon dual-power implementation plan

> **Execution note:** Follow this plan task by task with the
> `superpowers:executing-plans` skill. Use TDD for behavior changes and run the
> listed command after every task.

**Goal:** Produce two independently named EU868 LSN50V2 firmware builds that
share a bounded VIA Chameleon I2C2 acquisition path and differ only in whether
PB12 switches an external VCC P-MOSFET or PB5 switches the LSN50 +5 V rail.

**Architecture:** Keep vendor register semantics in `via_chameleon.c`. Put the
power/I2C lifecycle and its injectable state machine in a dedicated LSN50
hardware module. Give Chameleon a private I2C2 handle and make stock PB14 EXTI
paths unreachable in Chameleon builds. Build each power backend in a separate
object directory from the same source revision.

**Toolchain:** STM32Cube HAL, ARM GNU embedded toolchain, C11 host tests, shell
build script, EU868 LoRaMAC configuration.

## Global constraints

- Do not merge `master` or edit the user's primary checkout.
- Do not alter HX711 behavior; it is outside this work.
- Do not initialize or use PB6/PB7 in Chameleon mode.
- Keep the 44-byte version-1 payload layout and raw readings.
- Treat equal raw and compensated readings as valid.
- Keep payload bit 7 reserved and zero.
- Retry at most once, and only after full I2C/bus/power cleanup.
- On every exit, deinitialize I2C2, put PB13/PB14 in analog/no-pull state, and
  switch the selected rail off.

## Task 1: Replace unsupported protocol expectations with vendor fixtures

**Files:**

- Modify: `tests/mock_chameleon_i2c.h`
- Modify: `tests/mock_chameleon_i2c.c`
- Modify: `tests/test_chameleon_comp_pending.c`
- Modify: `tests/test_chameleon_settle_and_retry.c`
- Modify: `tests/Makefile`
- Reference: `/home/phil/kDrive/OSI OS/Hardware/Chameleon/VIAChameleonI2CMaster/Example output.txt`

1. Add mock trace support for command bytes, requested lengths, returned
   lengths, transport failures, monotonic milliseconds, and delays.
2. Replace the `raw == compensated` pending test with the vendor equality
   example. Assert success, no retry, no extra delay, preserved raw/comp values,
   and payload bit 7 clear.
3. Replace the fixed-settle test with a 50 ms status-poll/absolute-deadline
   test. Add short-read and transport-error cases.
4. Run `make -C tests clean test`. Confirm the new assertions fail against the
   old implementation for the expected equality/retry/deadline reasons.
5. Commit the red tests:

   ```bash
   git add tests
   git commit -m "test: encode VIA Chameleon protocol semantics"
   ```

## Task 2: Make the Chameleon module protocol-only

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/via_chameleon.h`
- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/via_chameleon.c`
- Modify: `tests/Makefile`

1. Define an internal result enum that distinguishes no device, trigger
   failure, status I/O failure, busy timeout, read failure, partial sample, and
   valid sample with sentinel flags.
2. Introduce a small bus interface for address probe, write, repeated-start
   write/read, delay, and monotonic time. Keep STM32 GPIO and HAL handle details
   out of this module.
3. Implement trigger `0x40`, status `0x41` polling every 50 ms against an
   absolute two-second deadline, and complete little-endian register reads.
4. Reject short reads. Preserve temperature, all raw/compensated resistances,
   and ID. Record open-channel, missing-temperature, and invalid-ID sentinels
   without requesting a retry.
5. Remove fixed 250 ms settle, equality inference, and `COMP_PENDING` behavior.
6. Run `make -C tests clean test`; all protocol tests must pass.
7. Commit:

   ```bash
   git add STM32CubeExpansion_LRWAN tests
   git commit -m "fix: align Chameleon reads with vendor protocol"
   ```

## Task 3: Add and test the bounded acquisition lifecycle

**Files:**

- Create: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/chameleon_lsn50_hw.h`
- Create: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_lsn50_hw.c`
- Create: `tests/test_chameleon_lifecycle.c`
- Modify: `tests/Makefile`

1. Write a host-side operation-trace test for both backend policies. Expected
   order is off/high-impedance, on, stabilization, I2C2 init, bounded probe,
   protocol attempt, I2C deinit, analog isolation, off.
2. Inject failure at power/probe/trigger/status/read boundaries. Assert cleanup
   on each path.
3. Assert recoverable communication failure performs exactly one cold retry;
   success and sentinel-bearing samples perform none.
4. Run only the new lifecycle test and confirm it fails because the module is
   absent.
5. Implement the portable lifecycle policy around injected low-level
   operations, then add the STM32-backed operations behind the production
   compile path.
6. Enforce exactly one of `CHAMELEON_POWER_EXTERNAL_PMOS` and
   `CHAMELEON_POWER_LSN50_5V` at compile time.
7. Implement PB12 open-drain/released OFF and LOW ON for the P-MOS backend.
   Implement PB5 HIGH OFF and LOW ON for the switched-5 V backend.
8. Use a private static I2C2 handle. Use short ACK transaction timeouts and an
   absolute startup probe deadline. Never assign HAL's internal state field.
9. Run `make -C tests clean test`; all host tests must pass.
10. Commit:

   ```bash
   git add STM32CubeExpansion_LRWAN tests
   git commit -m "feat: add bounded Chameleon I2C2 lifecycle"
   ```

## Task 4: Isolate PB14 and integrate acquisition with MOD3

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/bsp.c`
- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/main.c`
- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/at.c`
- Modify: `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/stm32l0xx_it.c`
- Modify: `tests/test_chameleon_payload.c`
- Modify: `tests/Makefile`

1. Add source-scanning or compile tests that fail if Chameleon paths arm
   EXTI14, read PB14 as digital MOD3 status, initialize I2C1 for Chameleon, or
   use generic `power_time` rail timing around acquisition.
2. Branch `HAL_I2C_MspInit` and `HAL_I2C_MspDeInit` by handle instance. Preserve
   I2C1 stock behavior; configure I2C2 on PB13/PB14 AF5 open-drain with no MCU
   pulls.
3. Remove the old shared-handle repointing and Chameleon-ready dependency.
   Route MOD3 reads through the lifecycle module.
4. Compile out PB14 EXTI initialization, IRQ handling, downlink rearming, and
   digital payload reads when `USE_CHAMELEON` is set. Preserve stock behavior
   in non-Chameleon builds.
5. Map exact internal failures conservatively to existing payload error bits.
   Keep payload length, offsets, raw values, compensated values, and reserved
   bit 7 unchanged.
6. Run `make -C tests clean test` and inspect the integration diff for any HX711
   changes. Tests must pass and HX711 code must be unchanged.
7. Commit:

   ```bash
   git add STM32CubeExpansion_LRWAN tests
   git commit -m "fix: isolate PB14 for Chameleon I2C2"
   ```

## Task 5: Create reproducible, separate firmware builds

**Files:**

- Modify: `build/build.sh`
- Modify: `build/cflags.rsp`
- Modify: `build/.gitignore`
- Create by build: `build/LSN50-chameleon-i2c2-vcc-pmos.{elf,map,hex,bin}`
- Create by build: `build/LSN50-chameleon-i2c2-5v-reg.{elf,map,hex,bin}`

1. Add a build smoke test or shell preflight that demonstrates the old script's
   absolute checkout path and shared objects are rejected.
2. Make the build repository-relative. Compile common source once per target in
   distinct object directories, including `chameleon_lsn50_hw.c`.
3. Add explicit targets `chameleon-i2c2-vcc-pmos` and
   `chameleon-i2c2-5v-reg`, each with one backend macro plus `USE_CHAMELEON`.
4. Build both targets from a clean state:

   ```bash
   build/build.sh clean
   build/build.sh chameleon-i2c2-vcc-pmos
   build/build.sh chameleon-i2c2-5v-reg
   ```

5. Run `arm-none-eabi-size` and `sha256sum` on both ELF/BIN pairs. Confirm all
   files exist, names differ, binaries differ, and neither old v1.5 artifact is
   overwritten.
6. Inspect maps or disassembly to confirm each ELF contains only its selected
   power path and contains I2C2 support.
7. Commit source/build-system changes and intentionally tracked release
   artifacts:

   ```bash
   git add build STM32CubeExpansion_LRWAN
   git commit -m "build: produce two Chameleon power variants"
   ```

## Task 6: Document wiring and bench validation

**Files:**

- Modify: `README-chameleon-v1.6-switched-power.md`
- Modify: `docs/superpowers/specs/2026-08-09-lsn50-chameleon-dual-power-design.md` only if implementation evidence requires a correction

1. Describe both wiring variants without unverified connector terminal numbers.
2. State that the 5 V variant requires an external inline 3.3 V regulator and
   that the VCC variant requires an external P-channel MOSFET plus gate pull-up.
3. Document PB13/PB14, AF5, no internal pulls, PB14 R14/C1 timing risk, 400 kHz
   rise-time measurement, 100 kHz fallback build policy, power polarity, and
   the exact artifact-to-wiring mapping.
4. Include bench steps for rail decay/back-power, active/sleep current, reduced
   battery, disconnects, open channels, missing DS18B20, bus faults, 100 rapid
   cycles, and a 12-24 hour soak. Mark Variant B as the first bench candidate.
5. Run:

   ```bash
   node /home/phil/Repos/osi-os/.claude/skills/anti-slop-writing/slop-check.js README-chameleon-v1.6-switched-power.md docs/superpowers/specs/2026-08-09-lsn50-chameleon-dual-power-design.md
   git diff --check
   ```

6. Commit:

   ```bash
   git add README-chameleon-v1.6-switched-power.md docs
   git commit -m "docs: add Chameleon dual-power bench guide"
   ```

## Task 7: Run final verification and adversarial review

**Files:**

- Review all changes since the branch's original `9a268d5` revision.

1. Run a clean host test suite and both clean firmware builds.
2. Run the OSI decoder golden fixture against a captured 44-byte payload:

   ```bash
   node /home/phil/Repos/osi-os/scripts/verify-lsn50-chameleon-codec.js
   ```

3. Run `git diff --check`, verify no absolute clone paths remain in the build
   files, inspect `git status`, and record hashes/sizes.
4. Invoke `superpowers:verification-before-completion` and evaluate every
   success claim against fresh command output.
5. Spawn the required second independent `gpt-5.6-sol` reviewer at `xhigh` with
   the approved design, this plan, initial findings, final diff, commands,
   outputs, artifacts, and documentation. Require a written report with
   protocol, lifecycle, GPIO/power, payload, build, test, documentation, and
   bench blockers.
6. Resolve every software blocker and repeat affected tests/builds. Clearly
   separate any remaining physical bench gates from software readiness.
7. Hand off the two BIN/HEX paths, hashes, sizes, verification evidence,
   reviewer disposition, and explicit bench-only caveats.
