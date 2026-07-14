#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CONFIG="$ROOT_DIR/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h"

failures=0

fail() {
  printf '%s\n' "$1"
  failures=$((failures + 1))
}

require_defined_zero() {
  local macro=$1
  local description=$2
  if ! grep -Eq "^#define[[:space:]]+$macro[[:space:]]+0U" "$CONFIG"; then
    fail "missing or nonzero: $description"
  fi
}

extract_define_block() {
  local macro=$1
  awk -v macro="$macro" '
    $1 == "#define" && $2 == macro { printing=1 }
    printing {
      print
      if ($0 !~ /\\$/) {
        exit
      }
    }
  ' "$CONFIG"
}

require_define_block_symbols() {
  local macro=$1
  shift
  local block
  block=$(extract_define_block "$macro" || true)
  for required_symbol in "$@"; do
    if ! printf '%s\n' "$block" | grep -Eq "$required_symbol"; then
      fail "$macro missing required symbol: $required_symbol"
    fi
  done
}

for forbidden_macro in \
  ML3_CONFIG_GATE0_READINESS_READY \
  ML3_CONFIG_QUALIFICATION_READY \
  ML3_CONFIG_PHASE2_QUALIFICATION_READY \
  ML3_CONFIG_PHASE2_READINESS_READY \
  ML3_CONFIG_CALIBRATION_READY \
  ML3_CONFIG_CAL_V5_DIVIDER_PPM \
  ML3_CONFIG_CAL_V5_DIVIDER_READY
do
  if grep -Eq "^#define[[:space:]]+$forbidden_macro[[:space:]]" "$CONFIG"; then
    fail "forbidden aggregate macro still defined: $forbidden_macro"
  fi
done

require_defined_zero ML3_CONFIG_GATE0_APPROVAL_READY "Gate 0 approval"
require_defined_zero ML3_CONFIG_GATE1_APPROVAL_READY "Gate 1 approval"
require_defined_zero ML3_CONFIG_V5_DIVIDER_RATIO_PPM "canonical divider value"
require_defined_zero ML3_CONFIG_V5_DIVIDER_RATIO_READY "canonical divider readiness"

require_define_block_symbols ML3_CONFIG_GATE0_READINESS \
  ML3_CONFIG_PB5_ACTIVE_LOW_READY \
  ML3_CONFIG_PB5_RESET_ISP_BROWNOUT_SAFE_READY \
  ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY \
  ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY \
  ML3_CONFIG_ZERO_AMBIGUITY_GUARD_READY \
  ML3_CONFIG_CM_RANGE_READY \
  ML3_CONFIG_V5_DIVIDER_RATIO_READY \
  ML3_CONFIG_V5_LIMITS_READY \
  ML3_CONFIG_DISCHARGE_READY \
  ML3_CONFIG_WARMUP_TIME_READY \
  ML3_CONFIG_LORA_REGION_READY \
  ML3_CONFIG_LORA_DATARATE_READY \
  ML3_CONFIG_ROUTINE_MAX_AIRTIME_READY \
  ML3_CONFIG_DIAGNOSTIC_MAX_AIRTIME_READY \
  ML3_CONFIG_GATE0_APPROVAL_READY

require_define_block_symbols ML3_CONFIG_PHASE2_READINESS \
  ML3_CONFIG_THERMISTOR_EFFECTIVE_REFERENCE_READY \
  ML3_CONFIG_THERMISTOR_RAIL_GUARD_READY \
  ML3_CONFIG_THERMISTOR_SETTLE_TIME_READY \
  ML3_CONFIG_THERMISTOR_TABLE_READY \
  ML3_CONFIG_NOISE_WARN_READY \
  ML3_CONFIG_NOISE_INVALID_READY \
  ML3_CONFIG_WARMUP_DRIFT_WARN_READY \
  ML3_CONFIG_WARMUP_DRIFT_INVALID_READY \
  ML3_CONFIG_VDDA_DRIFT_WARN_READY \
  ML3_CONFIG_VDDA_DRIFT_INVALID_READY \
  ML3_CONFIG_DIE_TEMP_RANGE_READY \
  ML3_CONFIG_CAL_OFFSET_READY \
  ML3_CONFIG_CAL_GAIN_READY \
  ML3_CONFIG_CAL_CM_READY \
  ML3_CONFIG_GATE1_APPROVAL_READY

require_define_block_symbols ML3_CONFIG_DEPLOYABLE \
  ML3_CONFIG_PROTOCOL_READY \
  ML3_CONFIG_MODE_ML3_READY \
  ML3_CONFIG_FPORT_READY \
  ML3_CONFIG_GATE0_READINESS \
  ML3_CONFIG_PHASE2_READINESS

if [ "$failures" -ne 0 ]; then
  printf 'ml3_readiness_cohesion_contract: %d failure(s)\n' "$failures"
  exit 1
fi
