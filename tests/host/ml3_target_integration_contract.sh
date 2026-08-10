#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
APP_DIR="$ROOT_DIR/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)"
CONFIG="$APP_DIR/inc/ml3_config.h"
BSP="$APP_DIR/src/bsp.c"
AT="$APP_DIR/src/at.c"
COMMAND="$APP_DIR/src/command.c"
MAIN="$APP_DIR/src/main.c"
PROJECT="$APP_DIR/MDK-ARM/STM32L072CZ-Nucleo/Lora.uvprojx"
BENCH_ADC_C="$APP_DIR/src/bench_adc.c"
BENCH_ADC_H="$APP_DIR/inc/bench_adc.h"
LORA="$APP_DIR/src/lora.c"
ADC_PORT_C="$APP_DIR/src/ml3_stm32_adc_port.c"
ADC_PORT_H="$APP_DIR/inc/ml3_stm32_adc_port.h"
GCC_MAKEFILE_DIR="$APP_DIR/gcc"
GCC_MAKEFILE="$GCC_MAKEFILE_DIR/Makefile"
failures=0

fail() {
  printf 'ml3 target integration: %s\n' "$1"
  failures=$((failures + 1))
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

require_port_function_line() {
  local function_name=$1
  local required_line=$2
  local description=$3
  if ! awk -v function_name="$function_name" -v required_line="$required_line" '
      function_name == substr($0, 1, length(function_name)) {
        in_function = 1
      }
      in_function && index($0, required_line) != 0 {
        found = 1
      }
      in_function && /^}/ {
        complete = 1
        exit
      }
      END {
        exit((complete && found) ? 0 : 1)
      }
    ' "$ADC_PORT_C"; then
    fail "$description"
  fi
}

require_port_channel_mapping() {
  local channel=$1
  local channel_bit=$2
  if ! awk -v case_line="case ${channel}U:" -v return_line="return ${channel_bit};" '
      /^static uint32_t ml3_stm32_adc_port_channel_bit/ {
        in_function = 1
        next
      }
      !in_function {
        next
      }
      index($0, case_line) != 0 {
        in_case = 1
        next
      }
      in_case && index($0, return_line) != 0 {
        found = 1
        exit
      }
      in_case && ($0 ~ /^[[:space:]]*case / ||
          $0 ~ /^[[:space:]]*default:/ || $0 ~ /^}/) {
        exit
      }
      END {
        exit(found ? 0 : 1)
      }
    ' "$ADC_PORT_C"; then
    fail "ADC port channel ${channel} is not mapped to ${channel_bit}"
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
require '^#define[[:space:]]+ML3_CONFIG_ACQUISITION_READY' "$CONFIG" \
  'acquisition readiness definition is missing'
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
  require 'HW_RTC_Tick2ms\(HW_RTC_GetTimerValue\(\)\)' "$ADC_PORT_C" \
    'ADC port now_ms does not convert the RTC tick clock'
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

  require_port_function_line \
    'void ml3_stm32_adc_port_init(ml3_stm32_adc_port_context_t *context)' \
    'RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;' \
    'ADC port init does not enable the SYSCFG peripheral clock'

  for runtime_source in "$BSP" "$MAIN" "$AT" "$COMMAND" "$LORA"; do
    if grep -q 'ml3_stm32_adc_port' "$runtime_source"; then
      fail "$(basename "$runtime_source") must not reference the unregistered ADC port"
    fi
  done

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
  for callback in now_ms request_stop_conversion is_conversion_stopped \
      request_disable_adc is_adc_disabled configure request_self_calibration \
      is_calibration_complete request_enable_adc is_adc_ready \
      enable_vrefint_gate enable_temperature_gate enable_vrefint_buffer_gate \
      enable_temperature_buffer_gate is_vrefint_ready is_temperature_ready \
      is_vrefint_buffer_ready is_temperature_buffer_ready is_reference_settled \
      select_channel start_conversion is_conversion_complete read_raw; do
    callback_count=$(awk -v callback=".$callback" '
        /^static const adc_precision_port_t k_ml3_stm32_adc_port = \{/ {
          in_port = 1
          next
        }
        in_port && /^};/ {
          exit
        }
        in_port {
          line = $0
          sub(/^[[:space:]]*/, "", line)
          sub(/[[:space:]]*=.*/, "", line)
          if (line == callback) {
            count++
          }
        }
        END {
          print count + 0
        }
      ' "$ADC_PORT_C")
    if [ "$callback_count" -ne 1 ]; then
      fail "ADC port callback .$callback count=$callback_count, expected 1"
    fi
  done

  if ! awk '
      /^static void ml3_stm32_adc_port_request_enable_adc/ {
        in_function = 1
      }
      in_function && /ADC1->ISR = ADC_ISR_ADRDY;/ {
        ready_line = NR
      }
      in_function && /ADC1->CR \|= ADC_CR_ADEN;/ {
        enable_line = NR
      }
      in_function && /^}/ {
        complete = 1
        exit
      }
      END {
        exit((complete && ready_line > 0 && enable_line > 0 &&
          ready_line < enable_line) ? 0 : 1)
      }
    ' "$ADC_PORT_C"; then
    fail 'ADC port does not acknowledge ADRDY before setting ADEN'
  fi

  if ! awk '
      /^static bool ml3_stm32_adc_port_read_raw/ {
        in_function = 1
      }
      in_function && /observed_overrun = \(ADC1->ISR & ADC_ISR_OVR\) != 0U;/ {
        observed_line = NR
      }
      in_function && /\*raw_code = \(uint16_t\)ADC1->DR;/ {
        read_line = NR
      }
      in_function && /ADC1->ISR = ADC_ISR_OVR;/ {
        clear_line = NR
      }
      in_function && /^}/ {
        complete = 1
        exit
      }
      END {
        exit((complete && observed_line > 0 && read_line > 0 && clear_line > 0 &&
          observed_line < read_line && read_line < clear_line) ? 0 : 1)
      }
    ' "$ADC_PORT_C"; then
    fail 'ADC port does not observe OVR, read DR, then acknowledge OVR in order'
  fi

  if grep -Eq '(^|[^[:alnum:]_])(while|for)[[:space:]]*\(|HAL_Delay[[:space:]]*\(|HW_RTC_DelayMs[[:space:]]*\(' "$ADC_PORT_C"; then
    fail 'ADC port must not use polling loops or blocking delays'
  fi

  require 'ADC1->CHSELR = ml3_stm32_adc_port_channel_bit\(channel\);' "$ADC_PORT_C" \
    'ADC port channel selection is not the pinned single-bit assignment'
  require_port_channel_mapping 0 ADC_CHSELR_CHSEL0
  require_port_channel_mapping 1 ADC_CHSELR_CHSEL1
  require_port_channel_mapping 2 ADC_CHSELR_CHSEL2
  require_port_channel_mapping 4 ADC_CHSELR_CHSEL4
  require_port_channel_mapping 17 ADC_CHSELR_CHSEL17
  require_port_channel_mapping 18 ADC_CHSELR_CHSEL18

  if ! awk '
      /^static void ml3_stm32_adc_port_configure/ {
        in_function = 1
      }
      in_function {
        body = body " " $0
      }
      in_function && /^}/ {
        complete = 1
        gsub(/[[:space:]]+/, " ", body)
        exit
      }
      END {
        exit((complete &&
          body ~ /ADC1->CFGR1 = ADC_RESOLUTION_12B \| ADC_DATAALIGN_RIGHT \| ADC_EXTERNALTRIGCONVEDGE_NONE;/ &&
          body ~ /ADC1->CFGR2 = ADC_CLOCK_SYNC_PCLK_DIV4 \| ADC_CFGR2_OVSE \| ADC_OVERSAMPLING_RATIO_256 \| ADC_RIGHTBITSHIFT_4;/ &&
          body ~ /ADC1->SMPR = ADC_SAMPLETIME_160CYCLES_5;/) ? 0 : 1)
      }
    ' "$ADC_PORT_C"; then
    fail 'ADC port fixed CFGR1/CFGR2/SMPR configuration is not pinned'
  fi
fi

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
  default_commands=$(cd "$GCC_MAKEFILE_DIR" && make -B -n 2>&1)
  if printf '%s\n' "$default_commands" | grep -q 'ML3_BENCH_TOOLS'; then
    fail 'default (BENCH=0) build command line references ML3_BENCH_TOOLS'
  fi
  bench_commands=$(cd "$GCC_MAKEFILE_DIR" && make -B -n BENCH=1 2>&1)
  if ! printf '%s\n' "$bench_commands" | grep -q -- '-DML3_BENCH_TOOLS=1'; then
    fail 'BENCH=1 build command line does not define ML3_BENCH_TOOLS=1 (check is not vacuous)'
  fi
else
  fail 'make is not available to verify the default build command line stays ML3_BENCH_TOOLS-free'
fi

if [ "$failures" -ne 0 ]; then
  printf 'ml3_target_integration_contract: %d failure(s)\n' "$failures"
  exit 1
fi
printf 'ml3 target integration contract: OK\n'
