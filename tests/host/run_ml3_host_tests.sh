#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
APP_DIR="$ROOT_DIR/STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)"
INC_DIR="$APP_DIR/inc"
SRC_DIR="$APP_DIR/src"
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-build.XXXXXX")
CONFIG_CONTRACT="$ROOT_DIR/tests/host/ml3_config_contract.sh"
CC="${CC:-gcc}"
PROCESS_GUARD="$ROOT_DIR/tests/host/ml3_process_guard.sh"
REQUESTED_EXIT_CODE=
ACTIVE_JOB_PID=
ACTIVE_JOB_PGID=
ACTIVE_JOB_SESSION=
ACTIVE_JOB_STARTTIME=
ACTIVE_JOB_SHUTDOWN_SIGNAL=
ACTIVE_JOB_SHUTDOWN_DEADLINE_MS=
ACTIVE_JOB_STAGE_DEADLINE_MS=
ACTIVE_JOB_STAGE_TIMEOUT_MS=5000
ACTIVE_JOB_KILL_GRACE_MS=500

. "$PROCESS_GUARD"

ml3_signal_exit_code() {
  case "$1" in
    HUP) printf '%s' 129 ;;
    INT) printf '%s' 130 ;;
    TERM) printf '%s' 143 ;;
    *) printf '%s' 1 ;;
  esac
}

ml3_arm_shutdown_deadline() {
  ACTIVE_JOB_SHUTDOWN_DEADLINE_MS=$(( $(ml3_now_ms) + ACTIVE_JOB_KILL_GRACE_MS ))
}

ml3_clear_active_job() {
  ACTIVE_JOB_PID=
  ACTIVE_JOB_PGID=
  ACTIVE_JOB_SESSION=
  ACTIVE_JOB_STARTTIME=
  ACTIVE_JOB_SHUTDOWN_SIGNAL=
  ACTIVE_JOB_SHUTDOWN_DEADLINE_MS=
  ACTIVE_JOB_STAGE_DEADLINE_MS=
}

ml3_drain_active_job() {
  if [ -z "${ACTIVE_JOB_SHUTDOWN_DEADLINE_MS:-}" ]; then
    ml3_arm_shutdown_deadline
  fi
  if [ -z "${ACTIVE_JOB_SHUTDOWN_SIGNAL:-}" ]; then
    ACTIVE_JOB_SHUTDOWN_SIGNAL=TERM
  fi

  if ! ml3_drain_owned_process_tree \
    "$ACTIVE_JOB_PID" \
    "$ACTIVE_JOB_PGID" \
    "$ACTIVE_JOB_SESSION" \
    "$ACTIVE_JOB_STARTTIME" \
    "$ACTIVE_JOB_SHUTDOWN_DEADLINE_MS" \
    "$ACTIVE_JOB_SHUTDOWN_SIGNAL"; then
    return 1
  fi

  ml3_clear_active_job
  return 0
}

ml3_request_shutdown() {
  local signal=$1
  if [ -z "${REQUESTED_EXIT_CODE:-}" ]; then
    REQUESTED_EXIT_CODE=$(ml3_signal_exit_code "$signal")
  fi
  if [ -z "${ACTIVE_JOB_SHUTDOWN_SIGNAL:-}" ]; then
    ACTIVE_JOB_SHUTDOWN_SIGNAL=$signal
  fi
  if [ -z "${ACTIVE_JOB_SHUTDOWN_DEADLINE_MS:-}" ]; then
    ml3_arm_shutdown_deadline
  fi
}

ml3_wait_for_shutdown() {
  local status=$1
  local now_ms=0

  while :; do
    if [ -n "${REQUESTED_EXIT_CODE:-}" ]; then
      if ! ml3_drain_active_job; then
        return 1
      fi
      return "$REQUESTED_EXIT_CODE"
    fi

    if ! ml3_process_tree_alive \
      "$ACTIVE_JOB_PID" \
      "$ACTIVE_JOB_PGID" \
      "$ACTIVE_JOB_SESSION" \
      "$ACTIVE_JOB_STARTTIME"; then
      wait "$ACTIVE_JOB_PID" 2>/dev/null || status=$?
      ml3_clear_active_job
      return "$status"
    fi

    now_ms=$(ml3_now_ms)
    if [ -n "${ACTIVE_JOB_STAGE_DEADLINE_MS:-}" ] && [ "$now_ms" -ge "$ACTIVE_JOB_STAGE_DEADLINE_MS" ]; then
      if ! ml3_drain_active_job; then
        return 1
      fi
      return 1
    fi

    sleep 0.05
  done
}

ml3_run_isolated_command() {
  local child_pid=
  local status=0
  local capture_deadline_ms=

  setsid "$@" &
  child_pid=$!
  ACTIVE_JOB_PID=$child_pid

  if ! read -r ACTIVE_JOB_PID ACTIVE_JOB_PGID ACTIVE_JOB_SESSION ACTIVE_JOB_STARTTIME \
    < <(ml3_capture_process_identity_with_deadline "$child_pid" $(( $(ml3_now_ms) + 500 ))); then
    capture_deadline_ms=$(( $(ml3_now_ms) + ACTIVE_JOB_STAGE_TIMEOUT_MS ))
    while ! read -r ACTIVE_JOB_PID ACTIVE_JOB_PGID ACTIVE_JOB_SESSION ACTIVE_JOB_STARTTIME \
      < <(ml3_capture_process_identity "$child_pid"); do
      if [ ! -e "/proc/$child_pid" ]; then
        wait "$child_pid" 2>/dev/null || status=$?
        ml3_clear_active_job
        return "$status"
      fi
      if [ "$(ml3_now_ms)" -ge "$capture_deadline_ms" ]; then
        if ! ml3_drain_direct_child_pid "$child_pid" "$(ml3_now_ms)" TERM; then
          ml3_clear_active_job
          return 1
        fi
        ml3_clear_active_job
        return 1
      fi
      sleep 0.05
    done
  fi

  ACTIVE_JOB_SHUTDOWN_DEADLINE_MS=
  ACTIVE_JOB_SHUTDOWN_SIGNAL=
  ACTIVE_JOB_STAGE_DEADLINE_MS=$(( $(ml3_now_ms) + ACTIVE_JOB_STAGE_TIMEOUT_MS ))
  REQUESTED_EXIT_CODE=

  ml3_wait_for_shutdown 0
}

CFLAGS=(
  -std=c99
  -Wall
  -Wextra
  -Werror
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wvla
  -Wstrict-prototypes
  -Wmissing-prototypes
  -Wmissing-declarations
  -Wundef
  -I"$INC_DIR"
)
MODULE_SOURCES=(
  "$SRC_DIR/adc_precision.c"
  "$SRC_DIR/ml3_measurement.c"
  "$SRC_DIR/ml3_calibration.c"
  "$SRC_DIR/ml3_quality.c"
  "$SRC_DIR/ml3_payload.c"
  "$SRC_DIR/ml3_thermistor.c"
  "$SRC_DIR/ml3_at_commands.c"
)
PRECISION_TEST="$ROOT_DIR/tests/host/ml3_adc_precision_test.c"
TASK3_TEST="$ROOT_DIR/tests/host/ml3_measurement_task3_test.c"
CALIBRATION_TEST="$ROOT_DIR/tests/host/ml3_calibration_test.c"
QUALITY_TEST="$ROOT_DIR/tests/host/ml3_quality_test.c"
THERMISTOR_TEST="$ROOT_DIR/tests/host/ml3_thermistor_test.c"

cleanup() {
  local cleanup_status=0

  if [ -n "${ACTIVE_JOB_PID:-}" ] || [ -n "${ACTIVE_JOB_PGID:-}" ]; then
    ml3_request_shutdown TERM
    if ! ml3_drain_active_job; then
      cleanup_status=1
    fi
  fi
  if [ -n "${BUILD_DIR:-}" ] && [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
  fi
  if [ "$cleanup_status" -ne 0 ]; then
    exit 1
  fi
}

trap cleanup EXIT
trap 'ml3_request_shutdown HUP' HUP
trap 'ml3_request_shutdown INT' INT
trap 'ml3_request_shutdown TERM' TERM

status=0

if grep -Eq '\(int(16|32)_t\)ml3_read_u(16|32)_le' "$SRC_DIR/ml3_calibration.c"; then
  echo "ml3_calibration uses implementation-defined unsigned-to-signed decode cast" >&2
  exit 1
fi

if grep -Eiq '\b(float|double|malloc|calloc|realloc|free|__int128|HAL_|stm32|i2c)\b' \
  "$INC_DIR/ml3_quality.h" "$SRC_DIR/ml3_quality.c" "$QUALITY_TEST"; then
  echo "ml3_quality contains a forbidden dependency or numeric type" >&2
  exit 1
fi

ml3_run_isolated_command "$CONFIG_CONTRACT"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

for source in "${MODULE_SOURCES[@]}"; do
  ml3_run_isolated_command "$CC" "${CFLAGS[@]}" -c "$source" -o "$BUILD_DIR/$(basename "${source%.c}").o"
  status=$?
  if [ "$status" -ne 0 ]; then
    exit "$status"
  fi
done

ml3_run_isolated_command "$CC" "${CFLAGS[@]}" \
  "$ROOT_DIR/tests/host/ml3_contract_test.c" \
  "$BUILD_DIR"/adc_precision.o \
  "$BUILD_DIR"/ml3_measurement.o \
  "$BUILD_DIR"/ml3_calibration.o \
  "$BUILD_DIR"/ml3_quality.o \
  "$BUILD_DIR"/ml3_payload.o \
  "$BUILD_DIR"/ml3_thermistor.o \
  "$BUILD_DIR"/ml3_at_commands.o \
  -o "$BUILD_DIR/ml3_contract_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$CC" "${CFLAGS[@]}" \
  "$PRECISION_TEST" \
  "$BUILD_DIR"/adc_precision.o \
  "$BUILD_DIR"/ml3_measurement.o \
  "$BUILD_DIR"/ml3_calibration.o \
  "$BUILD_DIR"/ml3_quality.o \
  "$BUILD_DIR"/ml3_payload.o \
  "$BUILD_DIR"/ml3_thermistor.o \
  "$BUILD_DIR"/ml3_at_commands.o \
  -o "$BUILD_DIR/ml3_adc_precision_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$CC" "${CFLAGS[@]}" \
  "$TASK3_TEST" \
  "$BUILD_DIR"/adc_precision.o \
  "$BUILD_DIR"/ml3_measurement.o \
  "$BUILD_DIR"/ml3_calibration.o \
  "$BUILD_DIR"/ml3_quality.o \
  "$BUILD_DIR"/ml3_payload.o \
  "$BUILD_DIR"/ml3_thermistor.o \
  "$BUILD_DIR"/ml3_at_commands.o \
  -o "$BUILD_DIR/ml3_measurement_task3_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$CC" "${CFLAGS[@]}" \
  "$CALIBRATION_TEST" \
  "$BUILD_DIR"/ml3_calibration.o \
  -o "$BUILD_DIR/ml3_calibration_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$CC" "${CFLAGS[@]}" \
  "$QUALITY_TEST" \
  "$BUILD_DIR"/ml3_quality.o \
  -o "$BUILD_DIR/ml3_quality_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$CC" "${CFLAGS[@]}" \
  "$THERMISTOR_TEST" \
  "$BUILD_DIR"/ml3_thermistor.o \
  -o "$BUILD_DIR/ml3_thermistor_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$BUILD_DIR/ml3_contract_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$BUILD_DIR/ml3_measurement_task3_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$BUILD_DIR/ml3_calibration_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$BUILD_DIR/ml3_quality_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$BUILD_DIR/ml3_thermistor_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi

ml3_run_isolated_command "$BUILD_DIR/ml3_adc_precision_test"
status=$?
if [ "$status" -ne 0 ]; then
  exit "$status"
fi
