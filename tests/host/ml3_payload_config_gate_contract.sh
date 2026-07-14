#!/usr/bin/env bash
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
APP_DIR="$ROOT_DIR/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)"
INC_DIR="$APP_DIR/inc"
PROBE="$ROOT_DIR/tests/host/ml3_payload_config_gate_probe.c"
CC="${CC:-gcc}"
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-payload-config-gate.XXXXXX")
CFLAGS=(
  -std=c99
  -Wall
  -Wextra
  -Werror
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wstrict-prototypes
  -Wmissing-prototypes
  -Wmissing-declarations
  -Wundef
)

cleanup() {
  rm -rf "$BUILD_DIR"
}
trap cleanup EXIT

write_variant() {
  local directory=$1
  local maximum=$2

  mkdir -p "$directory"
  sed -E \
    -e "s/^(#define[[:space:]]+ML3_CONFIG_MAX_FRMPAYLOAD_BYTES)[[:space:]]+[^[:space:]]+.*/\\1 ${maximum}U/" \
    -e 's/^(#define[[:space:]]+ML3_CONFIG_MAX_FRMPAYLOAD_READY)[[:space:]]+[^[:space:]]+.*/\1 1U/' \
    "$INC_DIR/ml3_config.h" >"$directory/ml3_config.h"
  cp "$INC_DIR/ml3_payload.h" "$directory/ml3_payload.h"
  cp "$INC_DIR/ml3_quality.h" "$directory/ml3_quality.h"
}

"$CC" "${CFLAGS[@]}" -I"$INC_DIR" \
  -c "$PROBE" -o "$BUILD_DIR/pending.o"

write_variant "$BUILD_DIR/too-small" 34
if "$CC" "${CFLAGS[@]}" -I"$BUILD_DIR/too-small" \
  -c "$PROBE" -o "$BUILD_DIR/too-small.o" >"$BUILD_DIR/too-small.log" 2>&1; then
  printf 'payload config gate accepted ready maximum 34\n'
  exit 1
fi
if ! grep -Fq 'ML3_CONFIG_MAX_FRMPAYLOAD_BYTES is too small' \
  "$BUILD_DIR/too-small.log"; then
  sed -n '1,120p' "$BUILD_DIR/too-small.log"
  exit 1
fi

write_variant "$BUILD_DIR/exact" 35
"$CC" "${CFLAGS[@]}" -I"$BUILD_DIR/exact" \
  -c "$PROBE" -o "$BUILD_DIR/exact.o"

printf 'ml3 payload config gate: OK\n'
