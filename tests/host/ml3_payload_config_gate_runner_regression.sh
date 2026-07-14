#!/usr/bin/env bash
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CONTRACT="$ROOT_DIR/tests/host/ml3_payload_config_gate_contract.sh"
FAKE_CC="$ROOT_DIR/tests/host/ml3_payload_config_gate_fake_cc.sh"
REAL_CC=$(command -v "${CC:-gcc}")
LOG_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-payload-config-runner.XXXXXX")

cleanup() {
  rm -rf "$LOG_DIR"
}
trap cleanup EXIT

expect_compiler_failure() {
  local output_name=$1
  local log_file="$LOG_DIR/$output_name.log"
  local status=0

  if ML3_GATE_REAL_CC="$REAL_CC" \
    ML3_GATE_FAKE_FAIL_OUTPUT="$output_name" \
    CC="$FAKE_CC" \
    "$CONTRACT" >"$log_file" 2>&1; then
    status=0
  else
    status=$?
  fi

  if [ "$status" -ne 99 ]; then
    sed -n '1,120p' "$log_file"
    printf 'config gate did not propagate compiler exit 99 for %s (status %s)\n' \
      "$output_name" "$status" >&2
    exit 1
  fi
  if grep -Fq 'ml3 payload config gate: OK' "$log_file"; then
    sed -n '1,120p' "$log_file"
    printf 'config gate printed OK after compiler failure for %s\n' \
      "$output_name" >&2
    exit 1
  fi
}

expect_compiler_failure pending.o
expect_compiler_failure exact.o

printf 'ml3 payload config gate runner regression: OK\n'
