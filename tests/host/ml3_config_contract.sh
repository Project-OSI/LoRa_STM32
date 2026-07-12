#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CONFIG="${ML3_CONFIG_PATH:-$ROOT_DIR/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h}"

failures=0

fail() {
  printf '%s\n' "$1"
  failures=$((failures + 1))
}

require_define_zero() {
  local macro=$1
  local description=$2
  if ! grep -Eq "^#define[[:space:]]+$macro[[:space:]]+0U" "$CONFIG"; then
    fail "nonzero or missing: $description"
  fi
}

require_marker_contract() {
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    set -- $line
    macro=$2
    case " $exempt_zero_macros " in
      *" $macro "*) continue ;;
    esac

    case "$line" in
      *'GATE0-PENDING ('*'§'*'*/'|*'PHASE2-PENDING ('*'§'*'*/') ;;
      *) fail "missing pending marker or provenance: $line" ;;
    esac
  done <<EOF
$(grep -E '^#define[[:space:]]+ML3_CONFIG_[A-Z0-9_]+[[:space:]]+0U([[:space:]]|$)' "$CONFIG")
EOF
}

require_no_generic_pending() {
  if grep -Eq 'PENDING_MEASUREMENT|PENDING_HARDWARE_DECISION' "$CONFIG"; then
    fail "generic pending markers remain in ml3_config.h"
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

exempt_zero_macros="ML3_CONFIG_PAYLOAD_TYPE_ROUTINE ML3_CONFIG_PAYLOAD_TYPE"

require_define_zero ML3_CONFIG_MODE_ML3 "MODE_ML3"
require_define_zero ML3_CONFIG_FPORT "FPort"
require_define_zero ML3_CONFIG_MODE_ML3_READY "MODE_ML3 readiness"
require_define_zero ML3_CONFIG_FPORT_READY "FPort readiness"
require_define_zero ML3_CONFIG_GATE0_APPROVAL_READY "Gate 0 approval readiness"
require_define_zero ML3_CONFIG_GATE1_APPROVAL_READY "Gate 1 approval readiness"
require_no_generic_pending
require_marker_contract

deployable_block=$(extract_define_block ML3_CONFIG_DEPLOYABLE || true)
for required_symbol in \
  ML3_CONFIG_PROTOCOL_READY \
  ML3_CONFIG_MODE_ML3_READY \
  ML3_CONFIG_FPORT_READY \
  ML3_CONFIG_GATE0_READINESS \
  ML3_CONFIG_PHASE2_READINESS
do
  if ! printf '%s\n' "$deployable_block" | grep -Eq "$required_symbol"; then
    fail "deployability expression missing required readiness gate: $required_symbol"
  fi
done

if [ "$failures" -ne 0 ]; then
  printf 'ml3_config_contract: %d failure(s)\n' "$failures"
  exit 1
fi
