#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
APP_DIR="$ROOT_DIR/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)"
CONFIG="$APP_DIR/inc/ml3_config.h"
BSP="$APP_DIR/src/bsp.c"
MEASUREMENT_C="$APP_DIR/src/ml3_measurement.c"
AT="$APP_DIR/src/at.c"
COMMAND="$APP_DIR/src/command.c"
MAIN="$APP_DIR/src/main.c"
PROJECT="$APP_DIR/MDK-ARM/STM32L072CZ-Nucleo/Lora.uvprojx"
BENCH_ADC_C="$APP_DIR/src/bench_adc.c"
BENCH_ADC_H="$APP_DIR/inc/bench_adc.h"
RTC_C="$APP_DIR/src/hw_rtc.c"
LORA="$APP_DIR/src/lora.c"
RAIL_PROCEDURE="$ROOT_DIR/docs/gate0-rail-measurement-procedure.md"
ADC_PORT_C=${ML3_ADC_PORT_C:-"$APP_DIR/src/ml3_stm32_adc_port.c"}
ADC_PORT_H="$APP_DIR/inc/ml3_stm32_adc_port.h"
GCC_MAKEFILE_DIR="$APP_DIR/gcc"
GCC_MAKEFILE="$GCC_MAKEFILE_DIR/Makefile"
contract_tmp=''
default_bsp_object=''
failures=0

fail() {
  printf 'ml3 target integration: %s\n' "$1"
  failures=$((failures + 1))
}

cleanup_contract_temporary_files() {
  if [ -n "$default_bsp_object" ]; then
    rm -f -- "$default_bsp_object"
  fi
  if [ -n "$contract_tmp" ]; then
    rm -rf -- "$contract_tmp"
  fi
}

require() {
  local pattern=$1
  local file=$2
  local description=$3
  if ! grep -Eq "$pattern" "$file"; then
    fail "$description"
  fi
}

require_file() {
  local file=$1
  local description=$2
  if [ ! -f "$file" ]; then
    fail "$description"
  fi
}

extract_port_function() {
  local function_name=$1
  awk -v function_name="$function_name" '
      function opening_braces(line, copy) {
        copy = line
        gsub(/[^{]/, "", copy)
        return length(copy)
      }
      function closing_braces(line, copy) {
        copy = line
        gsub(/[^}]/, "", copy)
        return length(copy)
      }
      !capturing &&
          $0 ~ "^[[:space:]]*(static[[:space:]]+)?[^;]*[[:space:]]" \
            function_name "[[:space:]]*\\(" {
        capturing = 1
      }
      capturing {
        print
        opening = opening_braces($0)
        if (opening > 0) {
          saw_opening = 1
        }
        depth += opening - closing_braces($0)
        if (saw_opening && depth == 0) {
          exit
        }
      }
    ' "$ADC_PORT_C"
}

extract_bsp_function() {
  local function_name=$1
  awk -v function_name="$function_name" '
      function opening_braces(line, copy) {
        copy = line
        gsub(/[^{]/, "", copy)
        return length(copy)
      }
      function closing_braces(line, copy) {
        copy = line
        gsub(/[^}]/, "", copy)
        return length(copy)
      }
      !capturing &&
          $0 ~ "^[[:space:]]*[^;]*[[:space:]]" \
            function_name "[[:space:]]*\\(" {
        capturing = 1
      }
      capturing {
        print
        opening = opening_braces($0)
        if (opening > 0) {
          saw_opening = 1
        }
        depth += opening - closing_braces($0)
        if (saw_opening && depth == 0) {
          exit
        }
      }
    ' "$BSP"
}

compile_default_bsp() {
  local output_object=$1

  (
    cd "$GCC_MAKEFILE_DIR" || exit 1
    arm-none-eabi-gcc \
      -mcpu=cortex-m0plus -mthumb -std=c99 -Os -g \
      -ffunction-sections -fdata-sections -fno-common \
      -I../inc \
      -I../../../../../../Drivers/BSP/STM32L0xx_Nucleo \
      -I../../../../../../Drivers/STM32L0xx_HAL_Driver/Inc \
      -I../../../../../../Drivers/CMSIS/Device/ST/STM32L0xx/Include \
      -I../../../../../../Drivers/CMSIS/Include \
      -I../../../../../../Middlewares/Third_Party/Lora/Crypto \
      -I../../../../../../Middlewares/Third_Party/Lora/Mac \
      -I../../../../../../Middlewares/Third_Party/Lora/Phy \
      -I../../../../../../Middlewares/Third_Party/Lora/Utilities \
      -I../../../../../../Middlewares/Third_Party/Lora/Core \
      -I../../../../../../Drivers/BSP/Components/Common \
      -I../../../../../../Drivers/BSP/Components/sx1276 \
      -I../../../../../../Drivers/BSP/sx1276mb1las \
      -I../../../../../../Drivers/BSP/Components/flash_eraseprogram \
      -I../../../../../../Drivers/BSP/Components/ds18b20 \
      -I../../../../../../Drivers/BSP/Components/gpio_exti \
      -I../../../../../../Drivers/BSP/Components/oil_float \
      -I../../../../../../Drivers/BSP/Components/sht20 \
      -I../../../../../../Drivers/BSP/Components/pwr_out \
      -I../../../../../../Drivers/BSP/Components/sht31 \
      -I../../../../../../Drivers/BSP/Components/ult \
      -I../../../../../../Drivers/BSP/Components/lidar_lite_v3hp \
      -I../../../../../../Drivers/BSP/Components/weight \
      -I../../../../../../Drivers/BSP/Components/iwdg \
      -I../../../../../../Drivers/BSP/Components/bh1750 \
      -I../../../../../../Drivers/BSP/Components/tfsensor \
      -I. -DSTM32L072xx -DUSE_STM32L0XX_NUCLEO -DUSE_HAL_DRIVER \
      -DUSE_SHT -DREGION_EU868 -include gcc_compat.h -Wall -c ../src/bsp.c \
      -o "$output_object"
  )
}

extract_target_bsp_make_flags() {
  make -C "$GCC_MAKEFILE_DIR" -Bn | awk '
    /arm-none-eabi-gcc/ && /-c \.\.\/src\/bsp\.c/ {
      for (field_index = 1; field_index <= NF; ++field_index) {
        if ($field_index ~ /^-I/ || $field_index ~ /^-D/) {
          print $field_index
        } else if ($field_index == "-include" && (field_index + 1) <= NF) {
          print $field_index
          print $(field_index + 1)
          ++field_index
        }
      }
      exit
    }
  '
}

write_enabled_phase3_harness() {
  local harness_source=$1

  # This translation unit intentionally contains source extracts rather than
  # retyped target code.  Keep the markers below checked so a source movement
  # cannot silently drop one of the guarded lifecycle or setting paths.
  {
    printf '%s\n' '/* phase3-extract: includes */'
    sed -n '47,64p' "$BSP"
    printf '%s\n' '/* phase3-extract-end: includes */'
    printf '%s\n' '/* phase3-extract: target-dependencies */'
    sed -n '67,80p' "$BSP"
    printf '%s\n' '/* phase3-extract-end: target-dependencies */'
    printf '%s\n' '/* phase3-extract: ml3-state */'
    sed -n '118,125p' "$BSP"
    sed -n '127,129p' "$BSP"
    sed -n '/^extern uint8_t mode;/p' "$BSP"
    printf '%s\n' '/* phase3-extract-end: ml3-state */'
    printf '%s\n' '/* phase3-extract: guarded-target */'
    printf '%s\n' '#if ML3_CONFIG_ACQUISITION_READY'
    awk '
      /^#if ML3_CONFIG_ACQUISITION_READY$/ { in_block = 1; next }
      in_block && $0 == "#endif /* ML3_CONFIG_ACQUISITION_READY */" { exit }
      in_block { print }
    ' "$BSP"
    printf '%s\n' '#endif /* ML3_CONFIG_ACQUISITION_READY */'
    printf '%s\n' '/* phase3-extract-end: guarded-target */'
    printf '%s\n' '/* phase3-extract: mode-selected */'
    extract_bsp_function 'ml3_mode_selected'
    printf '%s\n' '/* phase3-extract-end: mode-selected */'
    for function_name in \
        BSP_ML3_Init BSP_ML3_Service BSP_ML3_Abort BSP_ML3_RequestRoutine \
        BSP_ML3_RequestDiagnostic BSP_ML3_SetWarmup BSP_ML3_SetCycles \
        BSP_ML3_SetRaw; do
      printf '/* phase3-extract: %s */\n' "$function_name"
      extract_bsp_function "$function_name"
      printf '/* phase3-extract-end: %s */\n' "$function_name"
    done
  } > "$harness_source"
}

validate_enabled_phase3_harness() {
  local harness_source=$1
  local source_guard_count
  local harness_guard_count
  local region

  for region in includes target-dependencies ml3-state guarded-target mode-selected \
      BSP_ML3_Init BSP_ML3_Service BSP_ML3_Abort BSP_ML3_RequestRoutine \
      BSP_ML3_RequestDiagnostic BSP_ML3_SetWarmup BSP_ML3_SetCycles \
      BSP_ML3_SetRaw; do
    if [ "$(grep -Fc "phase3-extract: $region" "$harness_source" || true)" -ne 1 ] \
        || [ "$(grep -Fc "phase3-extract-end: $region" "$harness_source" || true)" -ne 1 ]; then
      return 1
    fi
  done
  source_guard_count=$(grep -Ec '^#if ML3_CONFIG_ACQUISITION_READY$' "$BSP" || true)
  harness_guard_count=$(grep -Ec '^#if ML3_CONFIG_ACQUISITION_READY$' "$harness_source" || true)
  if [ "$source_guard_count" -ne 7 ] || [ "$harness_guard_count" -ne "$source_guard_count" ]; then
    return 1
  fi
  for required_source_evidence in \
      'ml3_stm32_adc_port_init' 'Radio.Sleep()' 'LORA_send' \
      'ml3_measurement_abort(&ml3_measurement_context)' \
      'ml3_measurement_context.config.warmup_ms' \
      'ml3_measurement_context.config.abba_cycles'; do
    if ! grep -Fq "$required_source_evidence" "$harness_source"; then
      return 1
    fi
  done
}

compile_enabled_phase3_harness() {
  local config_overlay=$1
  local harness_source=$2
  local output_object=$3
  local flag
  local -a make_flags
  local -a phase3_flags
  local -a compiler_command

  mapfile -t make_flags < <(extract_target_bsp_make_flags)
  if [ "${#make_flags[@]}" -eq 0 ]; then
    return 2
  fi
  for flag in "${make_flags[@]}"; do
    case "$flag" in
      -I../inc|-I.) phase3_flags+=("$flag") ;;
      -I*) phase3_flags+=(-isystem "${flag#-I}") ;;
      *) phase3_flags+=("$flag") ;;
    esac
  done
  compiler_command=(
    arm-none-eabi-gcc
    -mcpu=cortex-m0plus -mthumb -std=c99 -Os -g
    -ffunction-sections -fdata-sections -fno-common
    "${phase3_flags[@]}" -include "$config_overlay"
    -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wvla
    -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wundef
    -c "$harness_source" -o "$output_object"
  )
  (
    cd "$GCC_MAKEFILE_DIR" || exit 1
    printf '%q ' "${compiler_command[@]}"
    printf '\n'
    "${compiler_command[@]}"
  )
}

extract_object_function() {
  local object_file=$1
  local function_name=$2

  arm-none-eabi-objdump -dr "$object_file" | awk \
    -v function_name="$function_name" '
      $0 ~ "^[0-9a-f]+ <" function_name ">:$" {
        capturing = 1
      }
      capturing {
        print
        if ($0 ~ /^[0-9a-f]+ <[^>]+>:/ &&
            $0 !~ "<" function_name ">:$") {
          exit
        }
      }
    '
}

require_port_function_line() {
  local function_name=$1
  local required_line=$2
  local description=$3
  local body

  body=$(extract_port_function "$function_name")
  if ! printf '%s\n' "$body" | grep -Fq "$required_line"; then
    fail "$description"
  fi
}

require_port_context_guard() {
  local function_name=$1
  local description=$2

  require_port_function_line "$function_name" \
    'if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {' \
    "$description"
}

require_adapter_dry_run_evidence() {
  local commands=$1
  local object_path=$2
  local elf_path=$3
  local variant=$4
  local compile_count
  local link_count
  local strict_flag

  compile_count=$(printf '%s\n' "$commands" | grep -Ec -- \
    "-c[[:space:]]+\.\./src/ml3_stm32_adc_port\\.c[[:space:]]+-o[[:space:]]+$object_path" || true)
  if [ "$compile_count" -ne 1 ]; then
    fail "$variant build must compile ml3_stm32_adc_port.c exactly once into $object_path"
    return
  fi

  for strict_flag in -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion \
      -Wvla -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wundef; do
    if ! printf '%s\n' "$commands" | grep -E -- \
      "-c[[:space:]]+\.\./src/ml3_stm32_adc_port\\.c[[:space:]]+-o[[:space:]]+$object_path" | \
      grep -Fq -- "$strict_flag"; then
      fail "$variant adapter compile command omits strict flag $strict_flag"
    fi
  done

  link_count=$(printf '%s\n' "$commands" | grep -Ec -- \
    "arm-none-eabi-gcc[[:space:]].*$object_path[[:space:]].*-o[[:space:]]+$elf_path" || true)
  if [ "$link_count" -ne 1 ]; then
    fail "$variant build must link $object_path exactly once into $elf_path"
  fi
}

require_port_initializer_binding() {
  local field=$1
  local callback=$2
  if ! awk -v field="$field" -v callback="$callback" '
      /^static const adc_precision_port_t k_ml3_stm32_adc_port = \{/ {
        in_port = 1
        next
      }
      in_port && /^};/ {
        complete = 1
        exit
      }
      in_port {
        line = $0
        sub(/^[[:space:]]*/, "", line)
        if (line ~ "^\\." field "[[:space:]]*=") {
          total++
          if (line ~ "^\\." field "[[:space:]]*=[[:space:]]*NULL([[:space:]]*,)?[[:space:]]*$") {
            null_binding = 1
          }
          if (line ~ "^\\." field "[[:space:]]*=[[:space:]]*" callback "[[:space:]]*,?[[:space:]]*$") {
            expected_binding++
          }
        }
      }
      END {
        exit((complete && total == 1 && expected_binding == 1 &&
          !null_binding) ? 0 : 1)
      }
    ' "$ADC_PORT_C"; then
    fail "ADC port callback .$field must be bound exactly once to $callback and must not be NULL"
  fi
}

require '^#define[[:space:]]+ML3_CONFIG_MODE_ML3[[:space:]]+10U' "$CONFIG" \
  'authorized mode 10 missing'
require '^#define[[:space:]]+ML3_CONFIG_FPORT[[:space:]]+13U' "$CONFIG" \
  'authorized FPort 13 missing'
require '^#define[[:space:]]+ML3_CONFIG_MODE_ML3_READY[[:space:]]+1U' "$CONFIG" \
  'mode readiness flag missing'
require '^#define[[:space:]]+ML3_CONFIG_FPORT_READY[[:space:]]+1U' "$CONFIG" \
  'FPort readiness flag missing'
require '^#define[[:space:]]+ML3_CONFIG_THERMISTOR_ADC_CHANNEL[[:space:]]+2U[[:space:]]*/\*' "$CONFIG" \
  'build-only thermistor ADC channel is not PA2/ADC_IN2'
require '^#define[[:space:]]+ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY[[:space:]]+0U[[:space:]]*/\*' "$CONFIG" \
  'thermistor ADC channel readiness is not held at zero'
require '^#define[[:space:]]+ML3_CONFIG_THERMISTOR_EXCITATION_GPIO[[:space:]]+4U[[:space:]]*/\*' "$CONFIG" \
  'build-only thermistor excitation is not PB4'
require '^#define[[:space:]]+ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY[[:space:]]+0U[[:space:]]*/\*' "$CONFIG" \
  'thermistor excitation readiness is not held at zero'
require '^#define[[:space:]]+ML3_CONFIG_PB5_ACTIVE_LOW[[:space:]]+1U[[:space:]]*/\*' "$CONFIG" \
  'PB5 active-low configuration is not set'
require '^#define[[:space:]]+ML3_CONFIG_ZERO_AMBIGUITY_GUARD_MV[[:space:]]+2U[[:space:]]*/\*' "$CONFIG" \
  'zero-ambiguity guard is not 2 mV'
require '^#define[[:space:]]+ML3_CONFIG_CM_RANGE_MIN_MV[[:space:]]+0U[[:space:]]*/\*' "$CONFIG" \
  'common-mode minimum is not 0 mV'
require '^#define[[:space:]]+ML3_CONFIG_CM_RANGE_MAX_MV[[:space:]]+100U[[:space:]]*/\*' "$CONFIG" \
  'common-mode maximum is not 100 mV'
require '^#define[[:space:]]+ML3_CONFIG_V5_DIVIDER_RATIO_PPM[[:space:]]+500000U[[:space:]]*/\*' "$CONFIG" \
  'V5 divider ratio is not 500000 ppm'
require '^#define[[:space:]]+ML3_CONFIG_V5_MINIMUM_MV[[:space:]]+4500U[[:space:]]*/\*' "$CONFIG" \
  'V5 minimum is not 4500 mV'
require '^#define[[:space:]]+ML3_CONFIG_V5_MAXIMUM_MV[[:space:]]+5500U[[:space:]]*/\*' "$CONFIG" \
  'V5 maximum is not 5500 mV'
require '^#define[[:space:]]+ML3_CONFIG_DISCHARGE_THRESHOLD_MV[[:space:]]+500U[[:space:]]*/\*' "$CONFIG" \
  'discharge threshold is not 500 mV'
require '^#define[[:space:]]+ML3_CONFIG_DISCHARGE_TIMEOUT_MS[[:space:]]+2000U[[:space:]]*/\*' "$CONFIG" \
  'discharge timeout is not 2000 ms'
require '^#define[[:space:]]+ML3_CONFIG_WARMUP_TIME_MS[[:space:]]+1500U[[:space:]]*/\*' "$CONFIG" \
  'warm-up time is not 1500 ms'
# task-F1 Step 4 (2026-08): trial activation readied six of these seven
# Gate 0 hardware items (see each flag's basis comment in ml3_config.h).
# The seventh, the V5 divider ratio calibration, is deliberately excluded -
# it is an assumed, not measured, ratio, and tests/host/
# ml3_readiness_cohesion_contract.sh independently pins it at 0 (commit
# 6d70a8c, "reject computed ML3 divider values"). This loop was previously
# a single "everything here stays zero" check; it is now two, so a future
# edit to either group is a visible, deliberate diff rather than a silent
# regression either way.
for readiness_macro in \
  ML3_CONFIG_PB5_ACTIVE_LOW_READY \
  ML3_CONFIG_ZERO_AMBIGUITY_GUARD_READY \
  ML3_CONFIG_CM_RANGE_READY \
  ML3_CONFIG_V5_LIMITS_READY \
  ML3_CONFIG_DISCHARGE_READY \
  ML3_CONFIG_WARMUP_TIME_READY
do
  require "^#define[[:space:]]+$readiness_macro[[:space:]]+1U[[:space:]]*/\\*" "$CONFIG" \
    "$readiness_macro must be readied for the task-F1 trial"
done
require '^#define[[:space:]]+ML3_CONFIG_V5_DIVIDER_RATIO_READY[[:space:]]+0U[[:space:]]*/\*' "$CONFIG" \
  'V5 divider ratio readiness must remain zero (deliberately excluded from the trial)'
require '^#define[[:space:]]+ML3_CONFIG_TRIAL_APPROVAL_READY[[:space:]]+1U[[:space:]]*/\*' "$CONFIG" \
  'trial approval readiness flag is missing (must be distinct from ML3_CONFIG_GATE0_APPROVAL_READY)'
require '^#define[[:space:]]+ML3_CONFIG_GATE0_APPROVAL_READY[[:space:]]+0U[[:space:]]*/\*' "$CONFIG" \
  'real Gate 0 approval readiness must remain zero - it must never be satisfied by trial approval'
require '^#define[[:space:]]+ML3_CONFIG_ACQUISITION_READY' "$CONFIG" \
  'acquisition readiness definition is missing'
# task-F1 Step 4: ACQUISITION_READY now points at the trial-scoped macro,
# never at the full Gate 0 standard directly.
if ! awk '
    /^#define[[:space:]]+ML3_CONFIG_ACQUISITION_READY[[:space:]]*\\$/ {
      getline
      if ($0 ~ /^[[:space:]]+ML3_CONFIG_TRIAL_ACQUISITION_READINESS$/) {
        valid = 1
      }
      exit
    }
    END { exit(valid ? 0 : 1) }
  ' "$CONFIG"; then
  fail 'acquisition readiness must expand only ML3_CONFIG_TRIAL_ACQUISITION_READINESS'
fi
if awk '
    /^#define[[:space:]]+ML3_CONFIG_ACQUISITION_READY/ { in_definition = 1 }
    in_definition { print }
    in_definition && $0 !~ /\\\\$/ { exit }
  ' "$CONFIG" | grep -q 'ML3_CONFIG_CAL_'; then
  fail 'acquisition readiness must not include calibration flags'
fi
# task-F1 Step 4: the trial-readiness macro itself must never require the
# full Gate 0 approval flag or the excluded-for-precision V5 divider ratio
# flag - it has its own, narrower approval flag instead.
if awk '
    /^#define[[:space:]]+ML3_CONFIG_TRIAL_ACQUISITION_READINESS/ { in_definition = 1 }
    in_definition { print }
    in_definition && $0 !~ /\\$/ { exit }
  ' "$CONFIG" | grep -Eq 'ML3_CONFIG_GATE0_APPROVAL_READY|ML3_CONFIG_V5_DIVIDER_RATIO_READY'; then
  fail 'trial-readiness macro must not require ML3_CONFIG_GATE0_APPROVAL_READY or ML3_CONFIG_V5_DIVIDER_RATIO_READY'
fi
if ! awk '
    /^#define[[:space:]]+ML3_CONFIG_TRIAL_ACQUISITION_READINESS/ { in_definition = 1 }
    in_definition { print }
    in_definition && $0 !~ /\\$/ { exit }
  ' "$CONFIG" | grep -Fq 'ML3_CONFIG_TRIAL_APPROVAL_READY'; then
  fail 'trial-readiness macro does not require its own trial approval flag'
fi
if grep -Fq 'AT+5V''T' "$RAIL_PROCEDURE"; then
  fail 'rail procedure contains the prohibited rail-time command'
fi
require 'owner-observation values already configure Phase 2' "$RAIL_PROCEDURE" \
  'rail procedure does not state that owner observations already configure Phase 2'
require 'optional field-installation validation' "$RAIL_PROCEDURE" \
  'rail procedure does not make remaining rail work optional field-installation validation'
if ! contract_tmp=$(mktemp -d "${TMPDIR:-/tmp}/ml3-target-integration.XXXXXX"); then
  fail 'cannot create temporary directory for build-only gate contract'
  exit 1
fi
trap cleanup_contract_temporary_files EXIT
if ! printf '%s\n' \
  '#include "ml3_config.h"' \
  'typedef char thermistor_adc_is_pa2[(ML3_CONFIG_THERMISTOR_ADC_CHANNEL == 2U) ? 1 : -1];' \
  'typedef char thermistor_excitation_is_pb4[(ML3_CONFIG_THERMISTOR_EXCITATION_GPIO == 4U) ? 1 : -1];' \
  'typedef char thermistor_adc_ready_stays_off[(ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY == 0U) ? 1 : -1];' \
  'typedef char thermistor_excitation_ready_stays_off[(ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY == 0U) ? 1 : -1];' \
  'typedef char pb5_active_low_is_set[(ML3_CONFIG_PB5_ACTIVE_LOW == 1U) ? 1 : -1];' \
  'typedef char zero_guard_is_2mv[(ML3_CONFIG_ZERO_AMBIGUITY_GUARD_MV == 2U) ? 1 : -1];' \
  'typedef char common_mode_minimum_is_0mv[(ML3_CONFIG_CM_RANGE_MIN_MV == 0U) ? 1 : -1];' \
  'typedef char common_mode_maximum_is_100mv[(ML3_CONFIG_CM_RANGE_MAX_MV == 100U) ? 1 : -1];' \
  'typedef char v5_divider_is_500000ppm[(ML3_CONFIG_V5_DIVIDER_RATIO_PPM == 500000U) ? 1 : -1];' \
  'typedef char v5_minimum_is_4500mv[(ML3_CONFIG_V5_MINIMUM_MV == 4500U) ? 1 : -1];' \
  'typedef char v5_maximum_is_5500mv[(ML3_CONFIG_V5_MAXIMUM_MV == 5500U) ? 1 : -1];' \
  'typedef char discharge_threshold_is_500mv[(ML3_CONFIG_DISCHARGE_THRESHOLD_MV == 500U) ? 1 : -1];' \
  'typedef char discharge_timeout_is_2000ms[(ML3_CONFIG_DISCHARGE_TIMEOUT_MS == 2000U) ? 1 : -1];' \
  'typedef char warmup_time_is_1500ms[(ML3_CONFIG_WARMUP_TIME_MS == 1500U) ? 1 : -1];' \
  'typedef char pb5_active_low_ready_is_set[(ML3_CONFIG_PB5_ACTIVE_LOW_READY == 1U) ? 1 : -1];' \
  'typedef char zero_guard_ready_is_set[(ML3_CONFIG_ZERO_AMBIGUITY_GUARD_READY == 1U) ? 1 : -1];' \
  'typedef char common_mode_ready_is_set[(ML3_CONFIG_CM_RANGE_READY == 1U) ? 1 : -1];' \
  'typedef char v5_divider_ready_stays_off[(ML3_CONFIG_V5_DIVIDER_RATIO_READY == 0U) ? 1 : -1];' \
  'typedef char v5_limits_ready_is_set[(ML3_CONFIG_V5_LIMITS_READY == 1U) ? 1 : -1];' \
  'typedef char discharge_ready_is_set[(ML3_CONFIG_DISCHARGE_READY == 1U) ? 1 : -1];' \
  'typedef char warmup_ready_is_set[(ML3_CONFIG_WARMUP_TIME_READY == 1U) ? 1 : -1];' \
  'typedef char trial_approval_is_set[(ML3_CONFIG_TRIAL_APPROVAL_READY == 1U) ? 1 : -1];' \
  'typedef char gate0_approval_stays_off[(ML3_CONFIG_GATE0_APPROVAL_READY == 0U) ? 1 : -1];' \
  'typedef char pb5_brownout_ready_stays_off[(ML3_CONFIG_PB5_RESET_ISP_BROWNOUT_SAFE_READY == 0U) ? 1 : -1];' \
  'typedef char lora_region_ready_stays_off[(ML3_CONFIG_LORA_REGION_READY == 0U) ? 1 : -1];' \
  'typedef char lora_datarate_ready_stays_off[(ML3_CONFIG_LORA_DATARATE_READY == 0U) ? 1 : -1];' \
  'typedef char max_frmpayload_ready_stays_off[(ML3_CONFIG_MAX_FRMPAYLOAD_READY == 0U) ? 1 : -1];' \
  'typedef char routine_airtime_ready_stays_off[(ML3_CONFIG_ROUTINE_MAX_AIRTIME_READY == 0U) ? 1 : -1];' \
  'typedef char diagnostic_airtime_ready_stays_off[(ML3_CONFIG_DIAGNOSTIC_MAX_AIRTIME_READY == 0U) ? 1 : -1];' \
  'typedef char gate0_readiness_stays_false[(ML3_CONFIG_GATE0_READINESS == 0U) ? 1 : -1];' \
  'typedef char trial_acquisition_readiness_is_true[(ML3_CONFIG_TRIAL_ACQUISITION_READINESS == 1U) ? 1 : -1];' \
  'typedef char acquisition_is_trial_ready[(ML3_CONFIG_ACQUISITION_READY == 1U) ? 1 : -1];' \
  'typedef char deployment_stays_off[(ML3_CONFIG_DEPLOYABLE == 0U) ? 1 : -1];' \
  'int main(void) { return 0; }' | \
  "${CC:-cc}" -std=c99 -Werror -I"$APP_DIR/inc" -x c - -c \
    -o "$contract_tmp/ml3_build_only_gate.o"; then
  fail 'build-only configuration or readiness assertion failed to compile'
fi
require 'if \(ml3_mode_selected\(\)\)' "$BSP" \
  'BSP sensor read does not enter the ML3 ADC lockout'
require 'BSP_ML3_Init\(\);' "$BSP" \
  'BSP sensor init does not return before stock sensor setup'
require 'LoRaMainCallbacks[[:space:]]*=[[:space:]]*\{[[:space:]]*BSP_ML3_GetBatteryLevel' "$MAIN" \
  'LoRa callback still bypasses the ML3 battery wrapper'
require 'BSP_ML3_GetTemperatureLevel' "$MAIN" \
  'LoRa callback still bypasses the ML3 temperature wrapper'
require 'strncmp\(cmd, "AT[+]ML3"' "$COMMAND" \
  'ML3 AT namespace is not intercepted before the legacy table'
require 'at_ml3_execute\(\(const uint8_t \*\)cmd' "$COMMAND" \
  'ML3 AT dispatcher is not length-bounded at the command seam'
require 'mode == ML3_CONFIG_MODE_ML3' "$AT" \
  'AT GETSENSORVALUE does not route mode 10'
require 'AppData.Port = ML3_CONFIG_FPORT' "$MAIN" \
  'mode 10 send path does not select FPort 13'
require 'ML3_CONFIG_ACQUISITION_READY[[:space:]]*!=[[:space:]]*0U' "$MAIN" \
  'remote AT+MOD downlink accepts mode 10 without gating on acquisition readiness'
if grep -q 'at_ml3_execute' "$MAIN"; then
  fail 'ML3 AT commands are reachable from the LoRaWAN downlink path'
fi
require 'ADC_CR_ADDIS' "$BSP" \
  'mode 10 initialization does not fence the stock ADC peripheral'
for source in adc_precision ml3_measurement ml3_calibration ml3_quality ml3_payload ml3_thermistor ml3_at_commands; do
  count=$(grep -Fc "<FilePath>..\\..\\src\\${source}.c</FilePath>" "$PROJECT" || true)
  if [ "$count" -ne 1 ]; then
    fail "Keil project source ${source}.c count=${count}, expected 1"
  fi
done

# --- Unregistered STM32L072 adc_precision target adapter (Task 10B / Phase 1) ---
# This port is compiled into both target artifacts but remains unreachable while
# ML3_CONFIG_ACQUISITION_READY is zero. Pin its direct-register protocol and
# build registration here so future activation work cannot quietly weaken them.
require_file "$ADC_PORT_C" 'src/ml3_stm32_adc_port.c is missing'
require_file "$ADC_PORT_H" 'inc/ml3_stm32_adc_port.h is missing'
require '^bool ml3_stm32_adc_port_init\(ml3_stm32_adc_port_context_t \*context\);' \
  "$ADC_PORT_H" \
  'ADC port init must report whether it accepted a non-NULL context'
require '^uint32_t ml3_stm32_adc_port_now_ms\(void \*port_ctx\);' \
  "$ADC_PORT_H" \
  'ADC port elapsed-time function is not public'
require '^uint32_t ml3_stm32_adc_port_now_ms\(void \*port_ctx\) \{' \
  "$ADC_PORT_C" \
  'ADC port elapsed-time definition is not public'
require '^TARGET_ADAPTER_SRCS[[:space:]]*:=' "$GCC_MAKEFILE" \
  'GCC target-adapter source list is missing'
adapter_gcc_count=$(grep -Fc '$(APP_ROOT)/src/ml3_stm32_adc_port.c' "$GCC_MAKEFILE" || true)
if [ "$adapter_gcc_count" -ne 1 ]; then
  fail "GCC target-adapter source ml3_stm32_adc_port.c count=$adapter_gcc_count, expected 1"
fi
require '^TARGET_ADAPTER_OBJS[[:space:]]*:=' "$GCC_MAKEFILE" \
  'GCC target-adapter object list is missing'
require '^ALL_OBJS[[:space:]]*:=[^#]*\$\(TARGET_ADAPTER_OBJS\)' "$GCC_MAKEFILE" \
  'GCC all-object list omits the target-adapter object'
if ! awk '
    /^\$\(TARGET_ADAPTER_OBJS\):/ { in_rule = 1 }
    in_rule && /\$\(CC\) \$\(ML3_STRICT_CFLAGS\) -c/ { found = 1 }
    in_rule && /^$/ { exit(found ? 0 : 1) }
    END { exit(found ? 0 : 1) }
  ' "$GCC_MAKEFILE"; then
  fail 'GCC target-adapter object is not compiled with ML3_STRICT_CFLAGS'
fi
adapter_mdk_count=$(grep -Fc '<FilePath>..\..\src\ml3_stm32_adc_port.c</FilePath>' "$PROJECT" || true)
if [ "$adapter_mdk_count" -ne 1 ]; then
  fail "Keil target-adapter source ml3_stm32_adc_port.c count=$adapter_mdk_count, expected 1"
fi

if [ -f "$ADC_PORT_C" ]; then
  require '^static ml3_stm32_adc_port_context_t \*ml3_stm32_adc_port_active_context;$' \
    "$ADC_PORT_C" \
    'ADC port has no active-context ownership guard'
  require '^static bool ml3_stm32_adc_port_context_is_active\(const void \*port_ctx\) \{' \
    "$ADC_PORT_C" \
    'ADC port active-context helper is missing'
  require 'HW_RTC_Tick2ms\(now_tick\)' "$ADC_PORT_C" \
    'ADC port now_ms does not convert the RTC tick clock'
  require 'if \(now_tick < context->last_rtc_tick\)' "$ADC_PORT_C" \
    'ADC port now_ms does not detect the raw RTC tick wrap'
  require 'context->rtc_epoch_ms \+= ML3_STM32_ADC_RTC_WRAP_MS;' "$ADC_PORT_C" \
    'ADC port now_ms does not carry milliseconds across the RTC tick wrap'
  require 'return context->rtc_epoch_ms \+ \(uint32_t\)HW_RTC_Tick2ms\(now_tick\);' "$ADC_PORT_C" \
    'ADC port now_ms does not return the wrap-preserving millisecond clock'
  require 'vrefint_enable_tick[[:space:]]*=[[:space:]]*HW_RTC_GetTimerValue\(\)' "$ADC_PORT_C" \
    'ADC port does not timestamp ADC_CCR_VREFEN'
  require '^enum \{ ML3_STM32_ADC_VREFINT_SETTLE_TICKS = 2U \};' "$ADC_PORT_C" \
    'ADC port does not define the two-tick VREFINT settlement duration'
  require '>=[[:space:]]*ML3_STM32_ADC_VREFINT_SETTLE_TICKS' "$ADC_PORT_C" \
    'ADC port lacks the two-tick VREFINT settlement guard'
  require 'return \(ADC1->CR & ADC_CR_ADSTART\) == 0U;' "$ADC_PORT_C" \
    'ADC port does not report conversion stop from ADSTART'
  for marker in ADC_CFGR2_OVSE ADC_OVERSAMPLING_RATIO_256 \
      ADC_RIGHTBITSHIFT_4 ADC_SAMPLETIME_160CYCLES_5 ADC_CR_ADSTP \
      ADC_CR_ADDIS ADC_CR_ADCAL ADC_CR_ADEN ADC_ISR_ADRDY ADC_ISR_EOC \
      ADC_ISR_OVR; do
    require "$marker" "$ADC_PORT_C" "ADC port is missing $marker"
  done

  init_body=$(extract_port_function 'ml3_stm32_adc_port_init')
  configure_body=$(extract_port_function 'ml3_stm32_adc_port_configure')

  require_port_function_line \
    'ml3_stm32_adc_port_init' \
    'RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;' \
    'ADC port init does not enable the SYSCFG peripheral clock'
  require_port_function_line \
    'ml3_stm32_adc_port_init' \
    'RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;' \
    'ADC port init does not enable the ADC1 peripheral clock'
  require_port_function_line \
    'ml3_stm32_adc_port_init' \
    'HW_GPIO_Init(GPIOA, GPIO_PIN_0, &init);' \
    'ADC port init does not configure PA0 as an analog input'
  require_port_function_line \
    'ml3_stm32_adc_port_init' \
    'HW_GPIO_Init(GPIOA, GPIO_PIN_1, &init);' \
    'ADC port init does not configure PA1 as an analog input'
  require_port_function_line \
    'ml3_stm32_adc_port_init' \
    'HW_GPIO_Init(GPIOA, GPIO_PIN_4, &init);' \
    'ADC port init does not configure PA4 as an analog input'
  require_port_function_line \
    'ml3_stm32_adc_port_init' \
    'init.Mode = GPIO_MODE_ANALOG;' \
    'ADC port init does not select analog GPIO mode'
  require_port_function_line \
    'ml3_stm32_adc_port_init' \
    'init.Pull = GPIO_NOPULL;' \
    'ADC port init does not select no GPIO pull'
  require_port_function_line \
    'ml3_stm32_adc_port_init' \
    'init.Speed = GPIO_SPEED_FREQ_HIGH;' \
    'ADC port init does not match the bench GPIO speed'
  for gpio_body in "$init_body" "$configure_body"; do
    if printf '%s\n' "$gpio_body" | grep -Fq 'GPIO_PIN_2'; then
      fail 'ADC port init and configure must preserve PA2 for LPUART1 TX'
      break
    fi
  done
  require 'PA2 remains LPUART1 TX' "$ADC_PORT_C" \
    'ADC port does not document the intentional PA2 UART exception'
  require 'all four ready callbacks observe this one bit' "$ADC_PORT_C" \
    'ADC port does not document the shared STM32L072 ready flag'
  require 'is_reference_settled supplies the independent elapsed-time guard' "$ADC_PORT_C" \
    'ADC port does not document the independent VREFINT settle guard'
  require 'N_PREDIV_S = 10' "$ADC_PORT_C" \
    'ADC port does not document the RTC predivider for VREFINT settling'
  require '^#define[[:space:]]+N_PREDIV_S[[:space:]]+10$' "$RTC_C" \
    'vendor RTC predivider is not N_PREDIV_S = 10'
  require 'approximately 1.95 ms' "$ADC_PORT_C" \
    'ADC port does not document the two-tick VREFINT settle duration'
  require 'documented 10 us VREFINT requirement' "$ADC_PORT_C" \
    'ADC port does not document the VREFINT settle requirement'
  if ! grep -Fq 'HW_RTC_Tick2ms uses floor(raw_tick * 125 / 128)' "$ADC_PORT_C"; then
    fail 'ADC port does not document the RTC tick-to-millisecond arithmetic'
  fi
  if ! grep -Fq 'raw counter wrap has a ~4,194,304,000ms period' "$ADC_PORT_C"; then
    fail 'ADC port does not document the raw RTC wrap period'
  fi
  if ! grep -Fq 'epoch makes the uint32_t millisecond time continuous modulo 2^32 for sampled acquisitions' "$ADC_PORT_C"; then
    fail 'ADC port does not document the RTC epoch continuity boundary'
  fi

  if ! printf '%s\n' "$init_body" | awk '
      /if \(context == NULL\)/ { null_check_line = NR }
      null_check_line > 0 && /return false;/ { reject_line = NR }
      /RCC->APB2ENR/ && first_clock_line == 0 { first_clock_line = NR }
      END {
        exit((null_check_line > 0 && reject_line > null_check_line &&
          first_clock_line > reject_line) ? 0 : 1)
      }
    '; then
    fail 'ADC port init must reject NULL before touching the peripheral clocks'
  fi

  while IFS= read -r callback; do
    [ -n "$callback" ] || continue
    require_port_context_guard "$callback" \
      "ADC port callback $callback must fail closed without its active context"
  done <<'EOF'
ml3_stm32_adc_port_request_stop_conversion
ml3_stm32_adc_port_is_conversion_stopped
ml3_stm32_adc_port_request_disable_adc
ml3_stm32_adc_port_is_adc_disabled
ml3_stm32_adc_port_configure
ml3_stm32_adc_port_request_self_calibration
ml3_stm32_adc_port_is_calibration_complete
ml3_stm32_adc_port_request_enable_adc
ml3_stm32_adc_port_is_adc_ready
ml3_stm32_adc_port_enable_vrefint_gate
ml3_stm32_adc_port_enable_temperature_gate
ml3_stm32_adc_port_enable_vrefint_buffer_gate
ml3_stm32_adc_port_enable_temperature_buffer_gate
ml3_stm32_adc_port_is_vrefint_ready
ml3_stm32_adc_port_is_temperature_ready
ml3_stm32_adc_port_is_vrefint_buffer_ready
ml3_stm32_adc_port_is_temperature_buffer_ready
ml3_stm32_adc_port_is_reference_settled
ml3_stm32_adc_port_select_channel
ml3_stm32_adc_port_start_conversion
ml3_stm32_adc_port_is_conversion_complete
ml3_stm32_adc_port_read_raw
EOF

  runtime_reference_files=$(find "$APP_DIR/src" -maxdepth 1 -type f \
    -name '*.c' ! -name 'ml3_stm32_adc_port.c' ! -name 'bsp.c' \
    -exec grep -l 'ml3_stm32_adc_port' {} + || true)
  if [ -n "$runtime_reference_files" ]; then
    fail "ADC port is referenced outside its guarded BSP integration: $runtime_reference_files"
  fi

  callback_total=$(awk '
      /^static const adc_precision_port_t k_ml3_stm32_adc_port = \{/ {
        in_port = 1
        next
      }
      in_port && /^};/ {
        exit
      }
      in_port && /^[[:space:]]*\.[[:alnum:]_]+[[:space:]]*=/ {
        count++
      }
      END {
        print count + 0
      }
    ' "$ADC_PORT_C")
  if [ "$callback_total" -ne 23 ]; then
    fail "ADC port callback initializer count=$callback_total, expected 23"
  fi

  while IFS=: read -r field callback; do
    [ -n "$field" ] || continue
    require_port_initializer_binding "$field" "$callback"
  done <<'EOF'
now_ms:ml3_stm32_adc_port_now_ms
request_stop_conversion:ml3_stm32_adc_port_request_stop_conversion
is_conversion_stopped:ml3_stm32_adc_port_is_conversion_stopped
request_disable_adc:ml3_stm32_adc_port_request_disable_adc
is_adc_disabled:ml3_stm32_adc_port_is_adc_disabled
configure:ml3_stm32_adc_port_configure
request_self_calibration:ml3_stm32_adc_port_request_self_calibration
is_calibration_complete:ml3_stm32_adc_port_is_calibration_complete
request_enable_adc:ml3_stm32_adc_port_request_enable_adc
is_adc_ready:ml3_stm32_adc_port_is_adc_ready
enable_vrefint_gate:ml3_stm32_adc_port_enable_vrefint_gate
enable_temperature_gate:ml3_stm32_adc_port_enable_temperature_gate
enable_vrefint_buffer_gate:ml3_stm32_adc_port_enable_vrefint_buffer_gate
enable_temperature_buffer_gate:ml3_stm32_adc_port_enable_temperature_buffer_gate
is_vrefint_ready:ml3_stm32_adc_port_is_vrefint_ready
is_temperature_ready:ml3_stm32_adc_port_is_temperature_ready
is_vrefint_buffer_ready:ml3_stm32_adc_port_is_vrefint_buffer_ready
is_temperature_buffer_ready:ml3_stm32_adc_port_is_temperature_buffer_ready
is_reference_settled:ml3_stm32_adc_port_is_reference_settled
select_channel:ml3_stm32_adc_port_select_channel
start_conversion:ml3_stm32_adc_port_start_conversion
is_conversion_complete:ml3_stm32_adc_port_is_conversion_complete
read_raw:ml3_stm32_adc_port_read_raw
EOF

  enable_body=$(extract_port_function \
    'ml3_stm32_adc_port_request_enable_adc')
  if ! printf '%s\n' "$enable_body" | awk '
      /ADC1->ISR[[:space:]]*=[[:space:]]*ADC_ISR_ADRDY;/ {
        ready_line = NR
        ready_count++
      }
      /ADC1->CR[[:space:]]*\|=[[:space:]]*ADC_CR_ADEN;/ {
        enable_line = NR
        enable_count++
      }
      END {
        exit((ready_count == 1 && enable_count == 1 &&
          ready_line < enable_line) ? 0 : 1)
      }
    '; then
    fail 'ADC port does not acknowledge ADRDY before setting ADEN'
  fi

  read_raw_body=$(extract_port_function 'ml3_stm32_adc_port_read_raw')
  if ! printf '%s\n' "$read_raw_body" | awk '
      /ADC1->ISR[[:space:]]*&[[:space:]]*ADC_ISR_OVR/ {
        observed_line = NR
        observed_count++
      }
      /ADC1->DR/ {
        read_line = NR
        read_count++
      }
      /ADC1->ISR[[:space:]]*=[[:space:]]*ADC_ISR_OVR;/ {
        clear_line = NR
        clear_count++
      }
      END {
        exit((observed_count == 1 && read_count == 1 && clear_count == 1 &&
          observed_line < read_line && read_line < clear_line) ? 0 : 1)
      }
    '; then
    fail 'ADC port does not observe OVR, read DR, then acknowledge OVR in order'
  fi

  if grep -Eq '(^|[^[:alnum:]_])(for|while)[[:space:]]*\(|(^|[^[:alnum:]_])(HAL_Delay|HW_RTC_DelayMs|[[:alnum:]_]*Delay(Ms)?)[[:space:]]*\(' "$ADC_PORT_C"; then
    fail 'ADC port must not use polling loops or blocking delays'
  fi

  chsel_count=$(grep -Ec '^[[:space:]]*ADC1->CHSELR = ml3_stm32_adc_port_channel_bit\(channel\);$' "$ADC_PORT_C" || true)
  if [ "$chsel_count" -ne 1 ]; then
    fail 'ADC port channel selection must be exactly one pinned single-bit assignment'
  fi
  channel_body=$(extract_port_function 'ml3_stm32_adc_port_channel_bit')
  if ! printf '%s\n' "$channel_body" | awk '
      /^[[:space:]]*case[[:space:]]+/ {
        case_count++
        if ($0 !~ /^[[:space:]]*case[[:space:]]+[0-9]+U:[[:space:]]*$/) {
          invalid_case = 1
          next
        }
        line = $0
        sub(/^[[:space:]]*case[[:space:]]+/, "", line)
        sub(/U:.*/, "", line)
        current_case = line
        case_seen[current_case]++
        next
      }
      /^[[:space:]]*default[[:space:]]*:/ {
        default_count++
        current_case = "default"
        next
      }
      current_case ~ /^[0-9]+$/ &&
          /^[[:space:]]*return[[:space:]]+/ {
        line = $0
        sub(/^[[:space:]]*return[[:space:]]+/, "", line)
        sub(/;.*/, "", line)
        case_return_count[current_case]++
        if ((current_case == "0" && line == "ADC_CHSELR_CHSEL0") ||
            (current_case == "1" && line == "ADC_CHSELR_CHSEL1") ||
            (current_case == "2" && line == "ADC_CHSELR_CHSEL2") ||
            (current_case == "4" && line == "ADC_CHSELR_CHSEL4") ||
            (current_case == "17" && line == "ADC_CHSELR_CHSEL17") ||
            (current_case == "18" && line == "ADC_CHSELR_CHSEL18")) {
          mapped[current_case]++
        }
        current_case = ""
        next
      }
      current_case == "default" && /^[[:space:]]*return[[:space:]]+/ {
        default_return_count++
        if ($0 ~ /^[[:space:]]*return[[:space:]]+0U;/) {
          default_zero_count++
        }
        current_case = ""
      }
      END {
        exit((case_count == 6 && default_count == 1 &&
          default_return_count == 1 && default_zero_count == 1 && !invalid_case &&
          case_seen["0"] == 1 && case_seen["1"] == 1 &&
          case_seen["2"] == 1 && case_seen["4"] == 1 &&
          case_seen["17"] == 1 && case_seen["18"] == 1 &&
          case_return_count["0"] == 1 && case_return_count["1"] == 1 &&
          case_return_count["2"] == 1 && case_return_count["4"] == 1 &&
          case_return_count["17"] == 1 && case_return_count["18"] == 1 &&
          mapped["0"] == 1 && mapped["1"] == 1 &&
          mapped["2"] == 1 && mapped["4"] == 1 &&
          mapped["17"] == 1 && mapped["18"] == 1) ? 0 : 1)
      }
    '; then
    fail 'ADC port channel map must contain only the six approved cases and default to 0U'
  fi

  config_body=$(extract_port_function 'ml3_stm32_adc_port_configure')
  if ! printf '%s\n' "$config_body" | awk '
      /ADC1->CFGR1[[:space:]]*=/ {
        cfgr1_writes++
      }
      /ADC1->CFGR2[[:space:]]*=/ {
        cfgr2_writes++
      }
      /ADC1->SMPR[[:space:]]*=/ {
        smpr_writes++
      }
      {
        body = body " " $0
      }
      END {
        gsub(/[[:space:]]+/, " ", body)
        exit((cfgr1_writes == 1 && cfgr2_writes == 1 && smpr_writes == 1 &&
          body ~ /ADC1->CFGR1 = ADC_RESOLUTION_12B \| ADC_DATAALIGN_RIGHT \| ADC_EXTERNALTRIGCONVEDGE_NONE;/ &&
          body ~ /ADC1->CFGR2 = ADC_CLOCK_SYNC_PCLK_DIV4 \| ADC_CFGR2_OVSE \| ADC_OVERSAMPLING_RATIO_256 \| ADC_RIGHTBITSHIFT_4;/ &&
          body ~ /ADC1->SMPR = ADC_SAMPLETIME_160CYCLES_5;/) ? 0 : 1)
      }
    '; then
    fail 'ADC port fixed CFGR1/CFGR2/SMPR configuration is not pinned'
  fi
  for register in CFGR1 CFGR2 SMPR; do
    register_references=$(grep -Ec "ADC1->$register" "$ADC_PORT_C" || true)
    if [ "$register_references" -ne 1 ]; then
      fail "ADC port $register must only be configured once in its configure callback"
    fi
  done
  if grep -Eq 'ADC_CFGR1_(CONT|DMAEN|DMACFG)' "$ADC_PORT_C"; then
    fail 'ADC port must not enable continuous conversion or DMA in CFGR1'
  fi
fi

# --- Guarded STM32L072 measurement service (Task 10B / Phase 3) ---
# All target-specific acquisition paths live under this preprocessor fence so
# the current false Gate 0 build retains the inert public ML3 API.
require '^#if ML3_CONFIG_ACQUISITION_READY$' "$BSP" \
  'BSP lacks the acquisition-readiness implementation guard'
if ! grep -Fxq '#endif /* ML3_CONFIG_ACQUISITION_READY */' "$BSP"; then
  fail 'BSP lacks the acquisition-readiness implementation guard terminator'
fi
phase3_block=$(awk '
    /^#if ML3_CONFIG_ACQUISITION_READY$/ { in_block = 1; next }
    in_block && $0 == "#endif /* ML3_CONFIG_ACQUISITION_READY */" { exit }
    in_block { print }
  ' "$BSP")
if [ -z "$phase3_block" ]; then
  fail 'BSP guarded acquisition implementation is empty'
else
  for declaration in \
      'ml3_stm32_adc_port_context_t' \
      'adc_precision_context_t' \
      'ml3_measurement_ctx_t' \
      'ml3_quality_result_t' \
      'ML3_PAYLOAD_ROUTINE_LENGTH'; do
    if ! printf '%s\n' "$phase3_block" | grep -Fq "$declaration"; then
      fail "BSP guarded service lacks required local state: $declaration"
    fi
  done
  for physical_symbol in \
      'ml3_stm32_adc_port_init' \
      'Radio.Sleep' \
      'LORA_send'; do
    total=$(grep -Fc "$physical_symbol" "$BSP" || true)
    guarded=$(printf '%s\n' "$phase3_block" | grep -Fc "$physical_symbol" || true)
    if [ "$total" -ne 1 ] || [ "$guarded" -ne 1 ]; then
      fail "$physical_symbol must have one guarded BSP acquisition use"
    fi
  done
  if ! printf '%s\n' "$phase3_block" | grep -Fq \
      'HAL_GPIO_WritePin(PWR_OUT_PORT, PWR_OUT_PIN,'; then
    fail 'guarded BSP service lacks PB5 power control'
  fi
  for forbidden in 'PB4' 'GPIO_PIN_2' 'vcom_IoDeInit' \
      'ml3_thermistor_convert'; do
    if printf '%s\n' "$phase3_block" | grep -Fq "$forbidden"; then
      fail "guarded BSP service contains forbidden thermistor/UART operation: $forbidden"
    fi
  done
fi

bsp_init_body=$(extract_bsp_function 'BSP_ML3_Init')
bsp_service_body=$(extract_bsp_function 'BSP_ML3_Service')
bsp_abort_body=$(extract_bsp_function 'BSP_ML3_Abort')
bsp_routine_request_body=$(extract_bsp_function 'BSP_ML3_RequestRoutine')
bsp_diagnostic_request_body=$(extract_bsp_function 'BSP_ML3_RequestDiagnostic')
bsp_set_warmup_body=$(extract_bsp_function 'BSP_ML3_SetWarmup')
bsp_set_cycles_body=$(extract_bsp_function 'BSP_ML3_SetCycles')
bsp_set_raw_body=$(extract_bsp_function 'BSP_ML3_SetRaw')
ml3_service_code=$(printf '%s\n%s\n%s\n%s\n%s\n%s\n' \
  "$phase3_block" "$bsp_init_body" "$bsp_service_body" "$bsp_abort_body" \
  "$bsp_routine_request_body" "$bsp_diagnostic_request_body")
for additional_ml3_function in \
    BSP_ML3_IsActive BSP_ML3_GetBatteryLevel BSP_ML3_GetTemperatureLevel \
    BSP_ML3_GetSettings BSP_ML3_SetWarmup BSP_ML3_SetCycles BSP_ML3_SetRaw \
    BSP_ML3_CalibrationChunk BSP_ML3_CalibrationClear; do
  ml3_service_code=$(printf '%s\n%s\n' "$ml3_service_code" \
    "$(extract_bsp_function "$additional_ml3_function")")
done
for forbidden_service_operation in \
    'PB4' 'GPIO_PIN_4' 'GPIOB->' 'PA2' 'GPIO_PIN_2' 'MODER2' \
    'GPIO_MODER_MODER2' 'vcom_IoDeInit' 'ml3_thermistor_convert'; do
  if printf '%s\n' "$ml3_service_code" | grep -Fq "$forbidden_service_operation"; then
    fail "ML3 BSP service contains forbidden pin/thermistor operation: $forbidden_service_operation"
  fi
done
if ! printf '%s\n' "$bsp_service_body" | grep -Fq \
    'if (!ML3_CONFIG_ACQUISITION_READY)'; then
  fail 'BSP_ML3_Service no longer preserves its false-gate return'
fi
default_gate_body=$(printf '%s\n' "$bsp_service_body" | awk '
    /^[[:space:]]*if \(!ML3_CONFIG_ACQUISITION_READY\)$/ { capturing = 1 }
    capturing {
      print
      opening = $0
      gsub(/[^{]/, "", opening)
      closing = $0
      gsub(/[^}]/, "", closing)
      if (length(opening) > 0) {
        saw_opening = 1
      }
      depth += length(opening) - length(closing)
      if (saw_opening && depth == 0) {
        exit
      }
    }
  ')
if [ -z "$default_gate_body" ] \
    || ! printf '%s\n' "$default_gate_body" | grep -Fxq '    return;'; then
  fail 'BSP_ML3_Service lacks a complete false-readiness runtime return'
fi
default_bsp_object=$(mktemp "${TMPDIR:-/tmp}/ml3-target-contract-bsp.XXXXXX")
if ! compile_default_bsp "$default_bsp_object" >/dev/null 2>&1; then
  fail 'default BSP service object did not compile for reachability inspection'
else
  for public_ml3_function in \
      BSP_ML3_Init BSP_ML3_Service BSP_ML3_Abort BSP_ML3_RequestRoutine \
      BSP_ML3_RequestDiagnostic BSP_ML3_IsActive BSP_ML3_GetBatteryLevel \
      BSP_ML3_GetTemperatureLevel BSP_ML3_GetSettings BSP_ML3_SetWarmup \
      BSP_ML3_SetCycles BSP_ML3_SetRaw BSP_ML3_CalibrationChunk \
      BSP_ML3_CalibrationClear; do
    default_function_disassembly=$(extract_object_function \
      "$default_bsp_object" "$public_ml3_function")
    if [ -z "$default_function_disassembly" ]; then
      fail "default BSP object does not expose $public_ml3_function for inspection"
      continue
    fi
    for unreachable_symbol in \
        'ml3_stm32_adc_port_init' 'LORA_send' 'HAL_GPIO_WritePin' 'Radio'; do
      if printf '%s\n' "$default_function_disassembly" | grep -Fq \
          "$unreachable_symbol"; then
        fail "default false-readiness $public_ml3_function reaches $unreachable_symbol"
      fi
    done
  done
fi
enabled_config_overlay="$contract_tmp/ml3_config.h"
enabled_phase3_harness="$contract_tmp/ml3-enabled-phase3.c"
enabled_phase3_object="$contract_tmp/ml3-enabled-phase3.o"
enabled_phase3_log="$contract_tmp/ml3-enabled-phase3.log"
if ! printf '%s\n' \
    "#include \"$CONFIG\"" \
    '#undef ML3_CONFIG_ACQUISITION_READY' \
    '#define ML3_CONFIG_ACQUISITION_READY 1U' \
    > "$enabled_config_overlay"; then
  fail 'cannot create temporary enabled-acquisition configuration overlay'
elif ! write_enabled_phase3_harness "$enabled_phase3_harness"; then
  fail 'cannot extract the guarded Phase 3 target translation unit'
elif ! validate_enabled_phase3_harness "$enabled_phase3_harness"; then
  fail 'guarded Phase 3 translation-unit extraction is incomplete'
elif ! compile_enabled_phase3_harness "$enabled_config_overlay" \
    "$enabled_phase3_harness" "$enabled_phase3_object" \
    > "$enabled_phase3_log" 2>&1; then
  cat "$enabled_phase3_log"
  fail 'enabled guarded Phase 3 target code does not compile with strict warnings'
else
  for strict_flag in -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion \
      -Wvla -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
      -Wundef; do
    if ! grep -Fq -- "$strict_flag" "$enabled_phase3_log"; then
      fail "enabled guarded Phase 3 compile omits strict flag $strict_flag"
    fi
  done
fi
if ! printf '%s\n' "$bsp_service_body" | grep -Fq \
    'ml3_measurement_start(&ml3_measurement_context)'; then
  fail 'BSP_ML3_Service does not start one pending measurement'
fi
# task-F1: the quality-threshold precondition (ml3_quality_thresholds_from_config)
# must be evaluated and must fail closed BEFORE ml3_measurement_start, which is
# what powers PB5 / runs warm-up / captures the ABBA burst by driving the
# measurement core through its states. Checking it later (inside on_process, at
# the very end of an acquisition) let a pending-Gate-0 configuration run a full
# acquisition and then discard it every cycle - battery cost with zero data,
# invisible remotely. Pin that the precondition line precedes the start line,
# and that failing it returns without ever reaching ml3_measurement_start.
if ! printf '%s\n' "$bsp_service_body" | awk '
    /ml3_quality_thresholds_from_config\(&precondition_thresholds\)/ {
      precondition_line = NR
    }
    /ml3_measurement_start\(&ml3_measurement_context\)/ && start_line == 0 {
      start_line = NR
    }
    END {
      exit((precondition_line > 0) &&
        (start_line > precondition_line) ? 0 : 1)
    }
  '; then
  fail 'BSP_ML3_Service does not evaluate quality thresholds before starting acquisition'
fi
if ! printf '%s\n' "$bsp_service_body" | awk '
    /ml3_quality_thresholds_from_config\(&precondition_thresholds\)/ {
      precondition_line = NR
    }
    precondition_line > 0 && /!= ML3_QUALITY_STATUS_OK\)$/ { guard_line = NR }
    guard_line > 0 && /ml3_active = false;/ && active_clear == 0 {
      active_clear = NR
    }
    guard_line > 0 && /ml3_request_pending = false;/ && pending_clear == 0 {
      pending_clear = NR
    }
    guard_line > 0 && /^[[:space:]]*return;[[:space:]]*$/ && return_line == 0 {
      return_line = NR
    }
    END {
      exit((guard_line > precondition_line) &&
        (active_clear > guard_line) &&
        (pending_clear > guard_line) &&
        (return_line > pending_clear) ? 0 : 1)
    }
  '; then
  fail 'BSP_ML3_Service does not fail closed (clear pending, return, never start) when thresholds are not ready'
fi
if ! printf '%s\n' "$bsp_service_body" | grep -Fq \
    'ml3_measurement_step(&ml3_measurement_context)'; then
  fail 'BSP_ML3_Service does not advance the core once per invocation'
fi
if ! printf '%s\n' "$bsp_service_body" | grep -Fq \
    'ml3_active = ml3_measurement_context.active;'; then
  fail 'BSP_ML3_Service does not mirror core activity'
fi
if ! printf '%s\n' "$bsp_service_body" | grep -Eq \
    'step[[:space:]]*==[[:space:]]*ML3_MEASUREMENT_STEP_DONE'; then
  fail 'BSP_ML3_Service does not clear pending work at DONE'
fi
if ! printf '%s\n' "$bsp_service_body" | grep -Eq \
    'step[[:space:]]*==[[:space:]]*ML3_MEASUREMENT_STEP_ERROR'; then
  fail 'BSP_ML3_Service does not clear pending work at ERROR'
fi
if ! printf '%s\n' "$bsp_abort_body" | awk '
    /ml3_measurement_abort\(&ml3_measurement_context\);/ { abort_line = NR }
    /ml3_active = false;/ && clear_line == 0 { clear_line = NR }
    END { exit((abort_line > 0 && clear_line > abort_line) ? 0 : 1) }
  '; then
  fail 'BSP_ML3_Abort must abort the core before clearing public state'
fi
mode_loss_body=$(printf '%s\n' "$bsp_service_body" | awk '
    /^[[:space:]]*if \(!ml3_mode_selected\(\)\)$/ { capturing = 1 }
    capturing {
      print
      opening = $0
      gsub(/[^{]/, "", opening)
      closing = $0
      gsub(/[^}]/, "", closing)
      if (length(opening) > 0) {
        saw_opening = 1
      }
      depth += length(opening) - length(closing)
      if (saw_opening && depth == 0) {
        exit
      }
    }
  ')
if [ -z "$mode_loss_body" ]; then
  fail 'BSP_ML3_Service has no dedicated mode-loss cleanup branch'
else
  for active_evidence in \
      'ml3_measurement_context.active' \
      'ml3_active' \
      'ml3_request_pending'; do
    if ! printf '%s\n' "$mode_loss_body" | grep -Fq "$active_evidence"; then
      fail "BSP mode-loss cleanup does not consider $active_evidence"
    fi
  done
  if ! printf '%s\n' "$mode_loss_body" | awk '
      /ml3_measurement_abort\(&ml3_measurement_context\);/ { abort_line = NR }
      /ml3_active = false;/ && active_clear == 0 { active_clear = NR }
      /ml3_request_pending = false;/ && pending_clear == 0 { pending_clear = NR }
      END {
        exit((abort_line > 0) && (active_clear > abort_line) &&
          (pending_clear > abort_line) ? 0 : 1)
      }
    '; then
    fail 'BSP mode-loss cleanup must abort the core before clearing public state'
  fi
fi
if ! printf '%s\n' "$bsp_service_body" | awk '
    /^#if ML3_CONFIG_ACQUISITION_READY$/ { guarded = 1; next }
    guarded && /^#endif/ { guarded = 0; next }
    guarded && /ml3_measurement_abort\(&ml3_measurement_context\);/ { found = 1 }
    END { exit(found ? 0 : 1) }
  '; then
  fail 'BSP mode-loss cleanup reaches core abort outside the acquisition guard'
fi
if ! awk '
    /^void ml3_measurement_abort\(/ { capturing = 1 }
    capturing && /ml3_measurement_cleanup_controls\(ctx\)/ { cleaned = 1 }
    capturing && /^}/ { exit(cleaned ? 0 : 1) }
    END { exit(cleaned ? 0 : 1) }
  ' "$MEASUREMENT_C" \
  || ! grep -Fq 'ctx->port.set_power_5v(ctx->port.context, false)' "$MEASUREMENT_C"; then
  fail 'mode-loss core abort no longer proves cleanup invokes the PB5-off callback'
fi

if ! printf '%s\n' "$bsp_init_body" | grep -Fq \
    'ml3_measurement_config_t config = {'; then
  fail 'BSP_ML3_Init must use an automatic mutable measurement config'
fi
if printf '%s\n' "$bsp_init_body" | grep -Eq \
    'static[[:space:]].*ml3_measurement_config_t'; then
  fail 'BSP_ML3_Init must not dereference factory VREF in a static initializer'
fi
if ! grep -Fq \
    'static uint16_t ml3_warmup_ms = (uint16_t)ML3_CONFIG_WARMUP_TIME_MS;' \
    "$BSP" \
  || ! grep -Fq 'static uint8_t ml3_cycles = 4U;' "$BSP"; then
  fail 'BSP ML3 settings do not default to 1500 ms and four ABBA cycles'
fi
if printf '%s\n' "$bsp_init_body" | grep -Fq \
    'ml3_warmup_ms = (uint16_t)ML3_CONFIG_WARMUP_TIME_MS;' \
  || printf '%s\n' "$bsp_init_body" | grep -Fq 'ml3_cycles = 4U;'; then
  fail 'BSP_ML3_Init resets selected ML3 settings before core configuration'
fi
for init_requirement in \
    '0U, 1U, 4U, 2U, (uint16_t)ml3_cycles,' \
    '(uint32_t)ml3_warmup_ms' \
    'ML3_CONFIG_DISCHARGE_THRESHOLD_MV / 2U' \
    'ML3_CONFIG_DISCHARGE_TIMEOUT_MS' \
    'config.vrefint_calibration_word = *ML3_TARGET_VREFINT_CAL_ADDR;' \
    'adc_precision_default_timeouts(&ml3_adc_timeouts)' \
    'ml3_adc_timeouts.conversion_ms = 10U;' \
    'ml3_measurement_init(&ml3_measurement_context'; do
  if ! printf '%s\n' "$bsp_init_body" | grep -Fq "$init_requirement"; then
    fail "BSP_ML3_Init lacks required service configuration: $init_requirement"
  fi
done
for setter_contract in \
    'ml3_warmup_ms = warmup_ms;' \
    'ml3_measurement_context.config.warmup_ms = (uint32_t)warmup_ms;'; do
  if ! printf '%s\n' "$bsp_set_warmup_body" | grep -Fq "$setter_contract"; then
    fail "BSP_ML3_SetWarmup does not propagate selected warmup: $setter_contract"
  fi
done
for setter_contract in \
    'ml3_cycles = cycles;' \
    'ml3_measurement_context.config.abba_cycles = (uint16_t)cycles;'; do
  if ! printf '%s\n' "$bsp_set_cycles_body" | grep -Fq "$setter_contract"; then
    fail "BSP_ML3_SetCycles does not propagate selected cycle count: $setter_contract"
  fi
done
if ! printf '%s\n' "$bsp_set_raw_body" | grep -Fq \
    'if ((raw_enabled != 0U) || ml3_active)' \
  || ! printf '%s\n' "$bsp_set_raw_body" | grep -Fq \
    'ml3_raw_enabled = 0U;'; then
  fail 'BSP_ML3_SetRaw must reject unsupported raw diagnostic enablement'
fi
if printf '%s\n' "$bsp_set_raw_body" | grep -Fq \
    'ml3_raw_enabled = raw_enabled'; then
  fail 'BSP_ML3_SetRaw accepts a raw diagnostic state without a payload path'
fi
if printf '%s\n' "$ml3_service_code" | grep -Fq \
    'ml3_payload_build_diagnostic'; then
  fail 'BSP target service must not add a diagnostic payload path in Phase 3'
fi
require '^#define ML3_TARGET_VREFINT_CAL_ADDR[[:space:]]+\\$' "$BSP" \
  'BSP does not define the VREFINT factory-address macro locally'
require '0x1FF80078UL' "$BSP" \
  'BSP VREFINT factory address is not STM32L072 0x1FF80078UL'

for required_callback in \
    'static bool ml3_target_configure_analog_pins(void *context)' \
    'static bool ml3_target_set_power_5v(void *context, bool enabled)' \
    'static bool ml3_target_set_thermistor_excitation(void *context, bool enabled)' \
    'static bool ml3_target_request_radio_sleep(void *context)' \
    'static bool ml3_target_watchdog_refresh(void *context)' \
    'static uint32_t ml3_target_read_reset_cause(void *context)' \
    'static bool ml3_target_on_process(void *context,' \
    'static bool ml3_target_on_build_payload(void *context,' \
    'static bool ml3_target_on_queue(void *context,'; do
  if ! printf '%s\n' "$phase3_block" | grep -Fq "$required_callback"; then
    fail "guarded BSP service lacks callback: $required_callback"
  fi
done
require 'ml3_stm32_adc_port_now_ms' "$BSP" \
  'BSP service does not reuse the ADC port elapsed-time function'
if ! grep -Fq \
    'ml3_stm32_adc_port_init((ml3_stm32_adc_port_context_t *)context)' "$BSP"; then
  fail 'BSP analog setup does not delegate to the STM32 ADC port'
fi
if ! grep -Fq 'enabled ? GPIO_PIN_RESET : GPIO_PIN_SET' "$BSP"; then
  fail 'BSP PB5 power callback is not active low'
fi
radio_sleep_body=$(extract_bsp_function 'ml3_target_request_radio_sleep')
if ! printf '%s\n' "$radio_sleep_body" | awk '
    /LoRaMacState/ { state_line = NR }
    /Radio\.Sleep\(\);/ { sleep_line = NR }
    END { exit((state_line > 0 && sleep_line > state_line) ? 0 : 1) }
  '; then
  fail 'BSP radio sleep must reject an active LoRaMacState before Radio.Sleep'
fi
if ! printf '%s\n' "$radio_sleep_body" | grep -Fq \
    'ML3_TARGET_LORAMAC_BUSY_MASK' \
    || ! grep -Fq 'UINT32_C(0x00000001)' "$BSP" \
    || ! grep -Fq 'UINT32_C(0x00000010)' "$BSP"; then
  fail 'BSP radio sleep does not reject both active LoRaMacState busy bits'
fi
if ! grep -Fq 'IWDG_Refresh();' "$BSP"; then
  fail 'BSP watchdog callback does not refresh the watchdog'
fi
require 'return RCC->CSR;' "$BSP" \
  'BSP reset callback does not snapshot RCC->CSR without clearing it'

process_body=$(extract_bsp_function 'ml3_target_on_process')
payload_body=$(extract_bsp_function 'ml3_target_on_build_payload')
queue_body=$(extract_bsp_function 'ml3_target_on_queue')
quality_input_body=$(extract_bsp_function 'ml3_target_fill_quality_input')
die_temp_body=$(extract_bsp_function 'ml3_target_die_temp_centic')
v5_values_body=$(extract_bsp_function 'ml3_target_v5_values')
if ! printf '%s\n' "$process_body" | grep -Fq \
    'ml3_target_fill_quality_input(result, &quality_input)'; then
  fail 'BSP process callback does not reject a result the engine never populated'
fi
for process_requirement in \
    'ADC_PRECISION_OVERSAMPLING_SCALE' \
    'ML3_TARGET_TEMPSENSOR_CAL1_ADDR' \
    'ML3_TARGET_TEMPSENSOR_CAL2_ADDR' \
    'ML3_CONFIG_V5_DIVIDER_RATIO_PPM' \
    'ml3_quality_thresholds_from_config' \
    'ml3_quality_evaluate' \
    'quality_input->has_calibration_status = true;' \
    'quality_input->calibration_valid = true;' \
    'quality_input->has_thermistor_status = true;' \
    'quality_input->thermistor_valid = true;'; do
  if ! printf '%s\n' "$phase3_block" | grep -Fq "$process_requirement"; then
    fail "BSP process callback lacks required evidence handling: $process_requirement"
  fi
done
if printf '%s\n' "$phase3_block" | grep -Fq 'ml3_thermistor_convert'; then
  fail 'BSP process callback must not convert the unready thermistor table'
fi
if ! printf '%s\n' "$process_body" | grep -Fq \
    'ML3_QUALITY_STATUS_INCOMPLETE'; then
  fail 'BSP process callback does not accept quality-incomplete evidence as transmittable'
fi

# task-F1 (2026-08): a reduced valid-cycle count is normal, expected input -
# per-cycle ABBA fault tolerance, commit 8202ae8 - and the quality module's
# own floor (ml3_quality.c, valid_cycles < 3) is the single place that
# decision belongs. A standalone rejection here on a reduced count, or on
# the raw cycle count differing from the configured one, silently dropped
# the whole reading and made a failing field node indistinguishable from a
# dead radio. Pin that both rejections are gone.
if printf '%s\n' "$quality_input_body" | grep -Fq \
    '(result->valid_cycle_count != result->abba_raw_cycle_count)'; then
  fail 'BSP quality input rejects a reduced valid-cycle count on its own again'
fi
if printf '%s\n' "$quality_input_body" | grep -Fq \
    'result->abba_raw_cycle_count != (uint16_t)ml3_cycles'; then
  fail 'BSP quality input rejects a raw cycle count short of the configured one again'
fi
for forbidden_all_or_nothing_guard in \
    '!result->has_mean_hi_uv' \
    '!result->has_mean_lo_uv' \
    '!result->has_median_diff_uv' \
    '!result->has_sd_uv' \
    '!result->has_drift_uv' \
    '!result->has_vdda_pre_uv' \
    '!result->has_vdda_post_uv'; do
  if printf '%s\n' "$quality_input_body" | grep -Fq -- "$forbidden_all_or_nothing_guard"; then
    fail "BSP quality input still refuses the whole reading on: $forbidden_all_or_nothing_guard"
  fi
done
# Only a NULL argument or a result the engine never populated at all is
# genuinely unusable; every other combination must reach quality evaluation
# with per-field availability instead (checked below).
if ! printf '%s\n' "$quality_input_body" | grep -Fq \
    '!result->has_abba_raw || !result->has_valid_cycle_count'; then
  fail 'BSP quality input does not gate on has_abba_raw / has_valid_cycle_count'
fi
if printf '%s\n' "$quality_input_body" | grep -Fq \
    'quality_input->has_rail_samples = true;'; then
  fail 'BSP quality input hardcodes rail-sample availability instead of deriving it'
fi
for required_pass_through in \
    'quality_input->has_mean_hi_uv = result->has_mean_hi_uv;' \
    'quality_input->has_mean_lo_uv = result->has_mean_lo_uv;' \
    'quality_input->has_median_diff_uv = result->has_median_diff_uv;' \
    'quality_input->has_warmup_drift_uv = result->has_drift_uv;'; do
  if ! printf '%s\n' "$quality_input_body" | grep -Fq "$required_pass_through"; then
    fail "BSP quality input does not pass through optional evidence: $required_pass_through"
  fi
done

# All H1/H2/L1/L2 values are contemporaneous with the pre-VREF VDDA.  PA4 is
# sampled before and after the burst, so its two raw codes must use their own
# respective VDDAs rather than a shared or post-only value.
for retained_raw in abba_h1_raw abba_h2_raw abba_l1_raw abba_l2_raw; do
  if ! printf '%s\n' "$quality_input_body" | grep -Fq \
      "result->$retained_raw[index],"; then
    fail "BSP quality input omits retained raw rail evidence: $retained_raw"
  fi
done
pre_vdda_raw_uses=$(printf '%s\n' "$quality_input_body" | \
  grep -Fc 'result->vdda_pre_uv,' || true)
if [ "$pre_vdda_raw_uses" -ne 4 ]; then
  fail 'BSP quality input does not scale every retained raw rail sample by pre-VREF VDDA'
fi
if ! printf '%s\n' "$v5_values_body" | awk '
    /result->pre_v5_raw,/ { expect_vdda = "pre"; next }
    expect_vdda == "pre" {
      matched = ($0 ~ /result->vdda_pre_uv,/)
      exit
    }
    END { exit(matched ? 0 : 1) }
  '; then
  fail 'BSP pre-PA4 sample is not scaled by pre-VREF VDDA'
fi
if ! printf '%s\n' "$v5_values_body" | awk '
    /result->post_v5_raw,/ { expect_vdda = "post"; next }
    expect_vdda == "post" {
      matched = ($0 ~ /result->vdda_post_uv,/)
      exit
    }
    END { exit(matched ? 0 : 1) }
  '; then
  fail 'BSP post-PA4 sample is not scaled by post-VREF VDDA'
fi

# The STM32L072 temperature factory words are 12-bit values characterized at
# 3.0 V.  The retained ADC code is x16 oversampled, so a 3.3-V raw sample must
# first be normalized by VDDA / 3.0 V, then be reduced to 12 bits.  This
# non-3.0-V fixture distinguishes that direction from the inverse ratio.
die_temp_raw=32000
die_temp_vdda_uv=3300000
die_temp_cal1=2000
die_temp_cal2=3000
die_temp_correct_normalized=$((
  die_temp_raw * die_temp_vdda_uv / 3000000 / 16))
die_temp_inverse_normalized=$((
  die_temp_raw * 3000000 / die_temp_vdda_uv / 16))
die_temp_correct_centic=$((3000 +
  (die_temp_correct_normalized - die_temp_cal1) * 10000 /
    (die_temp_cal2 - die_temp_cal1)))
die_temp_inverse_centic=$((3000 +
  (die_temp_inverse_normalized - die_temp_cal1) * 10000 /
    (die_temp_cal2 - die_temp_cal1)))
if [ "$die_temp_correct_centic" -ne 5000 ]; then
  fail 'die-temperature normalization fixture no longer computes 50.00 C at 3.3 V'
fi
if [ "$die_temp_inverse_centic" -eq "$die_temp_correct_centic" ]; then
  fail 'die-temperature normalization fixture does not distinguish the inverse VDDA ratio'
fi
if ! printf '%s\n' "$die_temp_body" | grep -Fq \
    '(uint64_t)result->die_temp_raw * (uint64_t)result->vdda_post_uv'; then
  fail 'BSP die-temperature normalization does not scale raw code by VDDA'
fi
if ! printf '%s\n' "$die_temp_body" | grep -Fq \
    '/ ML3_TARGET_TEMPSENSOR_CAL_VDDA_UV;'; then
  fail 'BSP die-temperature normalization does not divide by the 3.0-V factory supply'
fi

for payload_requirement in \
    'payload.corrected_diff_available = false;' \
    'payload.soil_temperature_available = false;' \
    'payload.calibration_id = 0U;' \
    'ml3_payload_build_routine(&payload, ml3_routine_frame,' \
    'ML3_PAYLOAD_ROUTINE_LENGTH, ML3_PAYLOAD_ROUTINE_LENGTH,'; do
  if ! printf '%s\n' "$payload_body" | grep -Fq "$payload_requirement"; then
    fail "BSP routine payload callback lacks required contract: $payload_requirement"
  fi
done

# task-F1 (2026-08): ml3_payload_routine_t already models every numeric
# field as available/unavailable and encodes a sentinel for whichever is
# marked unavailable; hardcoding every field to available discarded that
# and made a missing derived value (e.g. a below-floor reading's absent
# statistics) indistinguishable from a present one at the frame level
# instead of producing a sentinel. Pin that availability is now derived,
# not hardcoded, for every field that can legitimately be absent.
for forbidden_hardcoded_available in \
    'payload.mean_hi_available = true;' \
    'payload.mean_lo_available = true;' \
    'payload.vdda_available = true;' \
    'payload.v5_available = true;' \
    'payload.noise_available = true;' \
    'payload.die_temperature_available = true;'; do
  if printf '%s\n' "$payload_body" | grep -Fq -- "$forbidden_hardcoded_available"; then
    fail "BSP routine payload callback hardcodes availability again: $forbidden_hardcoded_available"
  fi
done
for required_sentinel_wiring in \
    'payload.mean_hi_available = result->has_mean_hi_uv;' \
    'payload.mean_lo_available = result->has_mean_lo_uv;' \
    'payload.vdda_available = result->has_vdda_pre_uv;'; do
  if ! printf '%s\n' "$payload_body" | grep -Fq -- "$required_sentinel_wiring"; then
    fail "BSP routine payload callback lacks sentinel-driven wiring: $required_sentinel_wiring"
  fi
done
if printf '%s\n' "$payload_body" | grep -Eq \
    '!result->has_mean_hi_uv \|\| !result->has_mean_lo_uv'; then
  fail 'BSP routine payload callback still refuses the whole frame on missing statistics'
fi
for queue_requirement in \
    'app_data.Port = ML3_CONFIG_FPORT;' \
    'LORA_send(&app_data, LORAWAN_UNCONFIRMED_MSG)' \
    'LORA_SUCCESS'; do
  if ! printf '%s\n' "$queue_body" | grep -Fq "$queue_requirement"; then
    fail "BSP routine queue callback lacks required contract: $queue_requirement"
  fi
done

# --- Bench-only ADC readout tool (Task B2 / Gate 0 Section 4, AT+ML3ADC) ---
# Pins down: the bench module exists with its entry point declared/defined;
# the bench command is dispatched from at.c only inside an ML3_BENCH_TOOLS
# guard; and ML3_BENCH_TOOLS never leaks into the default (BENCH=0) build's
# actual compiler invocation, which is the hard byte-identity gate.
require '^bool bench_adc_run\(uint8_t channel_mask, bench_adc_result_t\* result\);' \
  "$BENCH_ADC_H" \
  'bench_adc_run() is not declared in inc/bench_adc.h'
require '^bool bench_adc_run\(uint8_t channel_mask, bench_adc_result_t\* result\) \{' \
  "$BENCH_ADC_C" \
  'bench_adc_run() is not defined in src/bench_adc.c'
require '^#if ML3_BENCH_TOOLS' "$BENCH_ADC_H" \
  'inc/bench_adc.h body is not ML3_BENCH_TOOLS-guarded'
require '^#if ML3_BENCH_TOOLS' "$BENCH_ADC_C" \
  'src/bench_adc.c body is not ML3_BENCH_TOOLS-guarded'

require '#if ML3_BENCH_TOOLS' "$AT" \
  'at.c has no ML3_BENCH_TOOLS guard at all'
require 'AT\+ML3ADC' "$AT" \
  'AT+ML3ADC command literal is missing from at.c'
require '\+BENCH' "$AT" \
  'AT+ML3VER=? +BENCH self-identification suffix is missing from at.c'

# Stronger than plain co-occurrence: walk at.c and confirm every
# "AT+ML3ADC" occurrence falls inside some #if ML3_BENCH_TOOLS ... #endif
# region, not merely that both strings appear somewhere in the file. This
# repo's #if ML3_BENCH_TOOLS blocks in at.c do not nest, so a single-level
# toggle is sufficient.
if ! awk '
    /^#if ML3_BENCH_TOOLS/ { in_guard = 1; next }
    /^#endif/ { in_guard = 0; next }
    in_guard && /AT\+ML3ADC/ { found = 1 }
    END { exit(found ? 0 : 1) }
  ' "$AT"; then
  fail 'AT+ML3ADC is referenced in at.c outside any ML3_BENCH_TOOLS guard'
fi

# The default (BENCH=0) build's ACTUAL compiler invocation must never
# reference ML3_BENCH_TOOLS. A text grep over the Makefile would trip on
# the `ifeq ($(BENCH),1)` block itself (it legitimately contains the
# string), so this instead force-prints every recipe command line make
# would run for the default target (-B: treat all targets as out of date;
# -n: print without executing, no files touched) and inspects those.
if command -v make >/dev/null 2>&1; then
  if ! default_commands=$(cd "$GCC_MAKEFILE_DIR" && make -B -n 2>&1); then
    fail 'cannot dry-run the default target build'
    default_commands=''
  fi
  if printf '%s\n' "$default_commands" | grep -q 'ML3_BENCH_TOOLS'; then
    fail 'default (BENCH=0) build command line references ML3_BENCH_TOOLS'
  fi
  require_adapter_dry_run_evidence "$default_commands" \
    'build/app/ml3_stm32_adc_port.o' 'build/lora.elf' 'default'
  if ! bench_commands=$(cd "$GCC_MAKEFILE_DIR" && make -B -n BENCH=1 2>&1); then
    fail 'cannot dry-run the BENCH=1 target build'
    bench_commands=''
  fi
  if ! printf '%s\n' "$bench_commands" | grep -q -- '-DML3_BENCH_TOOLS=1'; then
    fail 'BENCH=1 build command line does not define ML3_BENCH_TOOLS=1 (check is not vacuous)'
  fi
  require_adapter_dry_run_evidence "$bench_commands" \
    'build/app-bench/ml3_stm32_adc_port.o' 'build/lora-bench.elf' 'BENCH=1'
else
  fail 'make is not available to verify the default build command line stays ML3_BENCH_TOOLS-free'
fi

if [ "$failures" -ne 0 ]; then
  printf 'ml3_target_integration_contract: %d failure(s)\n' "$failures"
  exit 1
fi
printf 'ml3 target integration contract: OK\n'
