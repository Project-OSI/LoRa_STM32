# GCC-ARM build environment (Dragino LSN50v2, STM32L072CZT6)

This directory is a from-scratch, GCC-only build environment for the
`DRAGINO-LRWAN(AT)` application in this repository, targeting the Dragino
LSN50v2 (STM32L072CZT6: Cortex-M0+, 192 KB flash @ `0x08000000`, 20 KB RAM
@ `0x20000000`, 6 KB data EEPROM @ `0x08080000`, accessed at runtime only,
never linked). It exists because until now this branch only had a Keil MDK
project and no machine here has Keil installed.

## Which Keil target this mirrors, and why

`MDK-ARM/STM32L072CZ-Nucleo/Lora.uvprojx` defines exactly **one** build
target: `sx1276mb1las`. There is nothing to disambiguate -- it is the only
`<Target>` in the file. Its `TargetCommonOption` sets `Device` to
`STM32L072CZTx` (the LSN50v2's MCU) and its `Cads` compiler settings
define:

```
STM32L072xx, USE_STM32L0XX_NUCLEO, USE_HAL_DRIVER, USE_SHT, REGION_EU868
```

`REGION_EU868` makes the EU868 selection explicit and unambiguous (Swiss
and Ugandan deployments are EU868). Cross-checked against
`docs/2026-07-12-lsn50v2-ml3-firmware-plan.md`, which states the target
platform in so many words: "Dragino LSN50v2 (STM32L072CZT6, SX1276/SX1278)".

Every source file, include path, and preprocessor define in `Makefile`
was read directly out of this `.uvprojx`'s XML (`<FilePath>`,
`<IncludePath>`, `<Define>` elements), resolved against the MDK-ARM
project directory (`../..` = the app root, `../../../../../..` =
`STM32CubeExpansion_LRWAN/`), not hand-picked or guessed. The 7 ML3-branch
modules (`adc_precision.c` + `ml3_measurement.c`, `ml3_calibration.c`,
`ml3_quality.c`, `ml3_payload.c`, `ml3_thermistor.c`,
`ml3_at_commands.c` -- group `Projects/End_Node/ML3` in the uvprojx,
added in commit `2b3baa8`) are present in that file list and build here.

The Keil project builds `ML3_CONFIG_ACQUISITION_READY` to `0` by default
(every underlying `..._READY` gate macro in `inc/ml3_config.h` is `0U`);
this Makefile does not define or override any of those macros, so the
image it produces stays gated-inert the same way.

## Build

```sh
make -C gcc            # build/lora.elf, build/lora.hex, build/lora.bin
make -C gcc size        # arm-none-eabi-size -A summary
make -C gcc clean
```

Toolchain: `arm-none-eabi-gcc` (developed and verified against 16.1.0),
plus the matching `binutils`/`newlib` (nano + nosys specs). No Keil/ARMCC
involved anywhere in this path.

## Bench variant

```sh
make -C gcc BENCH=1     # build/lora-bench.elf, build/lora-bench.hex, build/lora-bench.bin
make -C gcc BENCH=1 size
make -C gcc BENCH=1 clean   # or plain `make -C gcc clean`, which removes both variants
```

**Not for deployment.** `lora-bench.bin` adds a bench-only oversampled ADC
readout (`AT+ML3ADC`) for Gate 0 Section 4
(`docs/ml3-gate0-bench-runbook.md`) and self-identifies so it can never be
mistaken for a field image: `AT+ML3VER=?` appends a `,+BENCH` suffix on
this build only. **No unit whose `AT+ML3VER=?` response contains `+BENCH`
may ever be installed on a farm gateway.** The default `make -C gcc`
target (no `BENCH=`) is unaffected byte-for-byte -- everything the bench
variant adds is compiled out under `#if ML3_BENCH_TOOLS` (the Makefile
only defines `-DML3_BENCH_TOOLS=1` when `BENCH=1`), and the two variants'
object files live in separate `build/app-bench/` / `build/cube-bench/`
subdirectories so building one after the other never links a stale
object built with the other's flags.

The added module (`src/bench_adc.c` + `inc/bench_adc.h`) is a direct
STM32L0 HAL/register implementation -- it deliberately does not go
through `adc_precision.c` (the gated production acquisition port, still
blocked on the open Task 10 decisions) and touches none of the 7
ML3-branch modules, `main.c`'s mode dispatch, or the Keil project. It is
pure passive reads: no TX, no rail control, no dependence on ML3 mode 10,
safe to run at any time with a bench source driving the probe.

### Gate 0 Section 4 usage sketch

1. Flash `lora-bench.bin` to the bench unit; confirm `AT+ML3VER=?` reports
   `+BENCH`.
2. Fit the RC network per plan Section 3.2 and drive the bench voltage
   source onto the probe under test.
3. `AT+ML3ADC` (no argument) samples all three external channels
   (PA0/IN0, PA1/IN1, PA4/IN4) in one invocation; `AT+ML3ADC=0`,
   `AT+ML3ADC=1`, or `AT+ML3ADC=4` samples exactly one. Every invocation
   also self-calibrates the ADC and brackets the channel reads with two
   VREFINT reads (before and after), regardless of how many external
   channels were requested.
4. Repeat at each Section 4 injection point ({0, 2, 5, 10, 20, 50, 100} mV
   and {VDDA-100, VDDA-20} mV) and record the printed lines against the
   reference meter.

### Output format

Every line is prefixed `+ML3ADC:` for easy grepping. One line per
requested channel, followed by exactly one summary line:

```
+ML3ADC:CH=<0|1|4>,MEAN_X100=<raw code mean * 100>,MIN=<raw code>,MAX=<raw code>,UV=<microvolts vs ground>
+ML3ADC:VDDA_FIRST_MV=<mV>,VDDA_LAST_MV=<mV>,VREF_MEAN_X100_FIRST=<raw code mean * 100>,VREF_MEAN_X100_LAST=<raw code mean * 100>,CAL=<ADC calibration factor>
```

`MEAN_X100`/`VREF_MEAN_X100_*` are the mean of 64 software-averaged raw
12-bit ADC codes, fixed-point scaled by 100 (i.e. divide by 100 for the
code, or by 100*4095/VDDA_mV/1000 for volts) -- kept as an integer because
this build does not link `_printf_float` support for `%f` beyond what
`at.c`/`bsp.c` already required. `MIN`/`MAX` are the raw-code spread
across those 64 samples. `UV` is the channel's computed microvolts versus
ground, using `VDDA_FIRST_MV` (the VDDA computed from the VREFINT read
immediately before the channel-sampling loop -- the reading closest in
time to it). `CAL` is the value `HAL_ADCEx_Calibration_GetValue()`
returns from the one self-calibration run at the start of the
invocation. On any calibration or conversion failure/timeout (a bounded
10 ms poll, not `HAL_MAX_DELAY`, so a stuck ADC fails fast instead of
hanging the AT console) the invocation instead prints `+ML3ADC:ERROR` and
returns `AT_ERROR`.

## Warning policy

- **Vendor sources** (everything under `STM32CubeExpansion_LRWAN/Drivers`,
  `STM32CubeExpansion_LRWAN/Middlewares`, and the non-ML3 files in `src/`)
  build with `-Wall` only, no `-Werror`. They were never held to a
  stricter bar and do warn under GCC (see "Vendor warnings" below).
- **The 7 ML3-branch modules** build with the exact warning set
  `tests/host/run_ml3_host_tests.sh` uses for the same files on the host:
  `-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wvla
  -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations
  -Wundef`. A target-side regression in ML3 code cannot hide behind "it's
  cross-compiled differently."

## Compat shims added, and what they paper over

Two, both purely toolchain-compatibility, neither changes vendor
behavior:

1. **CMSIS intrinsics: no shim needed.** `Drivers/CMSIS/Include/
   core_cm0plus.h` already auto-detects `__GNUC__` and routes to
   `cmsis_gcc.h` for `__NOP()`/`__WFI()`/etc. `__asm` (used throughout the
   HAL for fences/NOPs) is a GCC alternate keyword available regardless
   of `-std=`, so it compiles as-is too. `gcc_compat.h` exists as the
   documented place to add a shim if a future rebase needs one, and is
   currently empty (force-included via `-include gcc_compat.h` on every
   translation unit).

2. **`iwdg_shim.h`** (force-included only when compiling
   `Drivers/BSP/Components/iwdg/iwdg.c`): works around a real vendor bug,
   not a Keil-ism -- see "Vendor warnings that look like real bugs" below
   for the underlying defect. `iwdg.h` declares
   `uint32_t GetLSIFrequency(void);` with external linkage; `iwdg.c`
   immediately defines it `static`. GCC treats that as an unconditional,
   non-suppressible hard error ("static declaration of 'GetLSIFrequency'
   follows non-static declaration") -- checked empirically that `-w`,
   `-std=gnu99`, and `-fpermissive` (a C++-only flag, no effect in C mode)
   all still error; there is no `-W` flag that gates this diagnostic. A
   first attempt renaming the symbol via `-D` did not work: since
   `iwdg.c` `#include`s `iwdg.h` in the same translation unit, the rename
   reached both the header's declaration and the .c's definition
   identically and reproduced the exact same conflict under the new name.
   The shim instead pre-arms `iwdg.h`'s own include guard (`__IWDG_H__`)
   so `iwdg.c` never sees the conflicting `extern` prototype, and supplies
   the same public declarations itself with `GetLSIFrequency` correctly
   declared `static`. `iwdg.c`'s logic is byte-for-byte unchanged; only
   which prototype it sees changes, and only for this one file -- every
   other translation unit that includes the real `iwdg.h` (`bsp.c`,
   `main.c`, `stm32l0xx_it.c`, `LoRaMac.c`) is unaffected, and none of
   them call `GetLSIFrequency` (checked with grep).

No vendor `.c`/`.h` file was edited to make this build work.

## Vendor warnings that look like real bugs (not style)

`-Wall` on the vendor tree surfaces a handful of genuine, pre-existing
defects, unrelated to the ML3 branch, not touched here per the "don't
edit vendor logic" rule:

- **`src/vcom.c:373`** -- a local `uint8_t responsetemp[1];` (uninitialized)
  shadows the file-scope, zero-initialized `uint8_t responsetemp[1] =
  {0x00};` at line 66, inside the function containing the loop at
  `vcom.c:392-415`. The shadowed local is read (`responsetemp[0]==0x59`,
  line 402) before it is ever written in that function, i.e. the first
  loop iteration compares against uninitialized stack memory instead of
  the intended persistent state. GCC flags this precisely:
  `'responsetemp[0]' is used uninitialized`.
- **`Drivers/BSP/Components/ds18b20/ds18b20.c:266-311`**
  (`DS18B20_ReadBit`) -- `uint8_t dat;` is declared uninitialized and only
  assigned inside an `if(num==1) / else if(num==2) / else if(num==3)`
  chain with no `else`; a call with any other `num` returns `dat`
  uninitialized. Flagged as `'dat' may be used uninitialized`.
- **`src/command.c:991-1003`** (`printf_all_config`) -- `char *cmd;` is
  declared and never initialized, then passed straight into
  `ATCommand[j].get((char *)cmd)`, an uninitialized pointer handed to a
  callback. Flagged as `'cmd' may be used uninitialized`.
- **`src/stm32l0xx_hw.c:330-350`** (battery-level read) -- computes the
  real reading into `batteryLevel_mV` (with an underscore), but the
  low-battery comparison a few lines down reads `batteryLevelmV` (no
  underscore) -- a different, never-assigned local that happens to share
  almost the same name. The low-battery branch is effectively comparing
  against uninitialized stack memory instead of the just-computed value.
  Flagged as `'batteryLevelmV' may be used uninitialized`.
- Several `GPIO_PinState`-vs-anonymous-`enum` comparison warnings
  (`bsp.c`, `ds18b20.c`, `ult.c`) look stylistic on inspection: both enums
  are `{RESET=0, SET=1}` under different vendor typedefs, so the
  comparisons are semantically correct despite the type mismatch GCC
  warns about. Not reported as bugs.

None of these are in the 7 ML3-branch modules, and none were introduced
by this GCC environment -- they are pre-existing in the vendor/Dragino
`.c` files this branch inherited (confirmed against `git log` predating
the ML3 branch's first commit `2b3baa8`).

## Known differences vs ARMCC

- **Linker script** (`STM32L072CZTx_FLASH.ld`): written for this build,
  since no CubeL0 GCC linker script exists anywhere in this tree (only
  STM32L1xx ones do, under
  `Drivers/CMSIS/Device/ST/STM32L1xx/Source/Templates/gcc/linker/`, which
  this script mirrors in structure for a different device/memory map).
  Keil's scatter-loading (in the `.uvprojx`/`.uvoptx`, not reviewed in
  detail here since it's a different linking model entirely) is not
  diffed against this line-by-line; the section layout follows the
  standard CubeL0 GCC convention (`.isr_vector`, `.text`, `.rodata`,
  `.data` loaded-from-flash, `.bss`, with `_sidata`/`_sdata`/`_edata`/
  `_sbss`/`_ebss`/`_estack` symbols the startup code expects). The 6 KB
  data-EEPROM region is deliberately not modeled as a linker region: the
  firmware addresses it at runtime via `DATA_EEPROM_BASE`
  (`0x08080000`, from the CMSIS device header), never as a linked
  section, so there is nothing for the linker to place there.
- **Startup file** (`startup_stm32l072xx.s`): not translated from the
  Keil ARMASM `.s` in `MDK-ARM/STM32L072CZ-Nucleo/`. ST ships its own
  GCC/GAS-syntax startup file for this exact device at
  `Drivers/CMSIS/Device/ST/STM32L0xx/Source/Templates/gcc/
  startup_stm32l072xx.s`; this is a verbatim copy of that vendor file
  (`sha256sum` identical), used as instructed rather than hand-translated.
  Its vector table has 48 entries (16 core exceptions + 32 peripheral
  IRQs, `WWDG_IRQHandler` through `USB_IRQHandler`), matching the
  STM32L072 reference manual.
- **C library**: `--specs=nano.specs --specs=nosys.specs` (newlib-nano +
  no-OS syscall stubs -- no semihosting, no host filesystem on target).
  ARMCC's C library (default or MicroLib, not specified in the `.uvprojx`
  beyond `<useUlib>1</useUlib>`, i.e. MicroLib) has different code-size
  and behavior characteristics; this was not compared byte-for-byte.
  `-u _printf_float` is included because it is actually needed: `bsp.c`
  and `at.c` format live sensor readings with `%.1f`/`%.3f`/`%.2f`-style
  specifiers through `PPRINTF`/`AT_PRINTF` -> `vcom.c`'s `TraceSend` ->
  libc `vsnprintf` (checked with grep, not assumed) -- nano's
  float-stripped printf would otherwise silently mis-render that
  diagnostic output.
- **Optimizer**: `-Os` here; the `.uvprojx` sets ARMCC `<Optim>4</Optim>`
  (`-O3`-equivalent "high level"). Not matched 1:1 -- `-Os` was chosen for
  flash headroom, and this branch is not yet performance-gated. Revisit
  if a future size or timing budget needs it.

## Where this stands

**ARMCC/Keil remains the historically-shipped toolchain** for this
product until a GCC-built image has been through hardware bring-up
(Gate 0, per `docs/ml3-gate0-bench-runbook.md`) on real LSN50v2 hardware.
This environment produces a byte-for-byte reproducible `.elf`/`.hex`/
`.bin` (verified: two independent clean builds produce identical
`sha256sum` for all three artifacts) that links, fits the STM32L072CZTx
flash/RAM budget, and passes the host test suite unmodified -- it has not
been flashed to or exercised on physical hardware, and `ML3_CONFIG_
ACQUISITION_READY` stays `0` (gated-inert) regardless, so this alone does
not change deployability.
