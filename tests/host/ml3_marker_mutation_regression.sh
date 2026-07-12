#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CONFIG="$ROOT_DIR/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/ml3_config.h"
VALIDATOR="$ROOT_DIR/tests/host/ml3_config_contract.sh"
if ! TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-marker.XXXXXX"); then
  printf 'marker-mutation regression failed to create temporary directory\n'
  exit 1
fi
MUTATED_CONFIG="$TMP_DIR/ml3_config.h"

cleanup() {
  rm -rf "$TMP_DIR"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if ! cp "$CONFIG" "$MUTATED_CONFIG"; then
  printf 'marker-mutation regression failed to copy config fixture\n'
  exit 1
fi

if ! "$VALIDATOR" >/dev/null 2>&1; then
  printf 'real config unexpectedly failed marker validation\n'
  exit 1
fi

if ! perl -0pi -e 's/^#define ML3_CONFIG_MODE_ML3\s+0U\s+\/\* GATE0-PENDING \(§3\.6\) \*\/$/#define ML3_CONFIG_MODE_ML3 0U/m' "$MUTATED_CONFIG"; then
  printf 'marker-mutation regression failed to mutate config fixture\n'
  exit 1
fi
if ! grep -Eq '^#define ML3_CONFIG_MODE_ML3[[:space:]]+0U$' "$MUTATED_CONFIG"; then
  printf 'marker-mutation regression did not produce the expected unmarked config fixture\n'
  exit 1
fi

if ML3_CONFIG_PATH="$MUTATED_CONFIG" "$VALIDATOR" >/dev/null 2>&1; then
  printf 'marker-mutation regression unexpectedly passed after comment removal\n'
  exit 1
fi
