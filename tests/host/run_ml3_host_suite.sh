#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
REQUESTED_EXIT_CODE=
ACTIVE_STAGE_PID=
ACTIVE_STAGE_PGID=
ACTIVE_STAGE_SESSION=
ACTIVE_STAGE_STARTTIME=
ACTIVE_STAGE_SHUTDOWN_SIGNAL=
ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS=
ACTIVE_STAGE_WAIT_DEADLINE_MS=
ACTIVE_STAGE_TIMEOUT_MS=${ML3_SUITE_STAGE_TIMEOUT_MS:-60000}
ACTIVE_STAGE_KILL_GRACE_MS=500
PRE_STATUS=
POST_STATUS=
PROCESS_GUARD="$ROOT_DIR/tests/host/ml3_process_guard.sh"

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
  ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS=$(( $(ml3_now_ms) + ACTIVE_STAGE_KILL_GRACE_MS ))
}

ml3_clear_active_stage() {
  ACTIVE_STAGE_PID=
  ACTIVE_STAGE_PGID=
  ACTIVE_STAGE_SESSION=
  ACTIVE_STAGE_STARTTIME=
  ACTIVE_STAGE_SHUTDOWN_SIGNAL=
  ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS=
  ACTIVE_STAGE_WAIT_DEADLINE_MS=
}

ml3_drain_active_stage() {
  if [ -z "${ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS:-}" ]; then
    ml3_arm_shutdown_deadline
  fi
  if [ -z "${ACTIVE_STAGE_SHUTDOWN_SIGNAL:-}" ]; then
    ACTIVE_STAGE_SHUTDOWN_SIGNAL=TERM
  fi

  if ! ml3_drain_owned_process_tree \
    "$ACTIVE_STAGE_PID" \
    "$ACTIVE_STAGE_PGID" \
    "$ACTIVE_STAGE_SESSION" \
    "$ACTIVE_STAGE_STARTTIME" \
    "$ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS" \
    "$ACTIVE_STAGE_SHUTDOWN_SIGNAL"; then
    return 1
  fi

  ml3_clear_active_stage
  return 0
}

ml3_request_shutdown() {
  local signal=$1
  if [ -z "${REQUESTED_EXIT_CODE:-}" ]; then
    REQUESTED_EXIT_CODE=$(ml3_signal_exit_code "$signal")
  fi
  if [ -z "${ACTIVE_STAGE_SHUTDOWN_SIGNAL:-}" ]; then
    ACTIVE_STAGE_SHUTDOWN_SIGNAL=$signal
  fi
  if [ -z "${ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS:-}" ]; then
    ml3_arm_shutdown_deadline
  fi
}

ml3_wait_for_stage_shutdown() {
  local now_ms=0
  local status=0

  while :; do
    if [ -n "${REQUESTED_EXIT_CODE:-}" ]; then
      if ! ml3_drain_active_stage; then
        return 1
      fi
      return "$REQUESTED_EXIT_CODE"
    fi

    if ! ml3_process_tree_alive \
      "$ACTIVE_STAGE_PID" \
      "$ACTIVE_STAGE_PGID" \
      "$ACTIVE_STAGE_SESSION" \
      "$ACTIVE_STAGE_STARTTIME"; then
      wait "$ACTIVE_STAGE_PID" 2>/dev/null || status=$?
      ml3_clear_active_stage
      return "$status"
    fi

    now_ms=$(ml3_now_ms)
    if [ -n "${ACTIVE_STAGE_WAIT_DEADLINE_MS:-}" ] && [ "$now_ms" -ge "$ACTIVE_STAGE_WAIT_DEADLINE_MS" ]; then
      if ! ml3_drain_active_stage; then
        return 1
      fi
      return 1
    fi

    sleep 0.05
  done
}

ml3_run_stage() {
  local child_pid=
  local status=0
  local capture_deadline_ms=

  setsid "$@" &
  child_pid=$!
  ACTIVE_STAGE_PID=$child_pid
  if ! read -r ACTIVE_STAGE_PID ACTIVE_STAGE_PGID ACTIVE_STAGE_SESSION ACTIVE_STAGE_STARTTIME \
    < <(ml3_capture_process_identity_with_deadline "$child_pid" $(( $(ml3_now_ms) + 500 ))); then
    capture_deadline_ms=$(( $(ml3_now_ms) + ACTIVE_STAGE_TIMEOUT_MS ))
    while ! read -r ACTIVE_STAGE_PID ACTIVE_STAGE_PGID ACTIVE_STAGE_SESSION ACTIVE_STAGE_STARTTIME \
      < <(ml3_capture_process_identity "$child_pid"); do
      if [ ! -e "/proc/$child_pid" ]; then
        wait "$child_pid" 2>/dev/null || status=$?
        ml3_clear_active_stage
        return "$status"
      fi
      if [ "$(ml3_now_ms)" -ge "$capture_deadline_ms" ]; then
        if ! ml3_drain_direct_child_pid "$child_pid" "$(ml3_now_ms)" TERM; then
          ml3_clear_active_stage
          return 1
        fi
        ml3_clear_active_stage
        return 1
      fi
      sleep 0.05
    done
  fi
  ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS=
  ACTIVE_STAGE_SHUTDOWN_SIGNAL=
  ACTIVE_STAGE_WAIT_DEADLINE_MS=$(( $(ml3_now_ms) + ACTIVE_STAGE_TIMEOUT_MS ))
  REQUESTED_EXIT_CODE=

  ml3_wait_for_stage_shutdown
}

ml3_capture_status() {
  git -C "$ROOT_DIR" status --porcelain --untracked-files=all
}

ml3_assert_no_repo_build_artifact() {
  if [ -e "$ROOT_DIR/tests/host/.build" ]; then
    printf 'suite rejected repository build artifact: %s\n' "$ROOT_DIR/tests/host/.build"
    return 1
  fi
}

cleanup() {
  local cleanup_status=0

  if [ -n "${ACTIVE_STAGE_PID:-}" ] || [ -n "${ACTIVE_STAGE_PGID:-}" ]; then
    ml3_request_shutdown TERM
    if ! ml3_drain_active_stage; then
      cleanup_status=1
    fi
  fi
  if [ "$cleanup_status" -ne 0 ]; then
    exit 1
  fi
}

trap cleanup EXIT
trap 'ml3_request_shutdown HUP' HUP
trap 'ml3_request_shutdown INT' INT
trap 'ml3_request_shutdown TERM' TERM

STAGES=(
  "$ROOT_DIR/tests/host/ml3_readiness_cohesion_contract.sh"
  "$ROOT_DIR/tests/host/run_ml3_host_tests.sh"
  "$ROOT_DIR/tests/host/ml3_marker_mutation_regression.sh"
  "$ROOT_DIR/tests/host/ml3_signal_term_regression.sh"
  "$ROOT_DIR/tests/host/ml3_runner_signal_status_regression.sh"
  "$ROOT_DIR/tests/host/ml3_suite_signal_status_regression.sh"
  "$ROOT_DIR/tests/host/ml3_runner_descendant_regression.sh"
  "$ROOT_DIR/tests/host/ml3_concurrency_regression.sh"
  "$ROOT_DIR/tests/host/ml3_clean_tree_after_make_test.sh"
  "$ROOT_DIR/tests/host/ml3_suite_signal_term_regression.sh"
  "$ROOT_DIR/tests/host/ml3_suite_descendant_regression.sh"
)

PRE_STATUS=$(ml3_capture_status)

for stage in "${STAGES[@]}"; do
  ml3_assert_no_repo_build_artifact || exit 1
  ml3_run_stage "$stage"
  status=$?
  if [ "$status" -ne 0 ]; then
    exit "$status"
  fi
  ml3_assert_no_repo_build_artifact || exit 1
done

POST_STATUS=$(ml3_capture_status)
if [ "$POST_STATUS" != "$PRE_STATUS" ]; then
  printf 'suite changed worktree status\n'
  printf 'before:\n%s\n' "$PRE_STATUS"
  printf 'after:\n%s\n' "$POST_STATUS"
  exit 1
fi

if ! ml3_assert_no_repo_build_artifact; then
  exit 1
fi
