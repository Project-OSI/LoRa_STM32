#!/usr/bin/env bash
set -eu

REAL_CC=${ML3_GATE_REAL_CC:?ML3_GATE_REAL_CC is required}
FAIL_OUTPUT=${ML3_GATE_FAKE_FAIL_OUTPUT:?ML3_GATE_FAKE_FAIL_OUTPUT is required}
previous=

for argument in "$@"; do
  if [ "$previous" = "-o" ]; then
    case "$argument" in
      "$FAIL_OUTPUT"|*/"$FAIL_OUTPUT") exit 99 ;;
    esac
  fi
  previous=$argument
done

exec "$REAL_CC" "$@"
