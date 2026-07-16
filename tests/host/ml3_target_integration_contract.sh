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

require '^#define[[:space:]]+ML3_CONFIG_MODE_ML3[[:space:]]+10U' "$CONFIG" \
  'authorized mode 10 missing'
require '^#define[[:space:]]+ML3_CONFIG_FPORT[[:space:]]+13U' "$CONFIG" \
  'authorized FPort 13 missing'
require '^#define[[:space:]]+ML3_CONFIG_MODE_ML3_READY[[:space:]]+1U' "$CONFIG" \
  'mode readiness flag missing'
require '^#define[[:space:]]+ML3_CONFIG_FPORT_READY[[:space:]]+1U' "$CONFIG" \
  'FPort readiness flag missing'
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

if [ "$failures" -ne 0 ]; then
  printf 'ml3_target_integration_contract: %d failure(s)\n' "$failures"
  exit 1
fi
printf 'ml3 target integration contract: OK\n'
