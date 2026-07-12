#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SUITE="${SUITE_PATH:-$ROOT_DIR/tests/host/run_ml3_host_suite.sh}"
FIXTURE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-suite-leak.XXXXXX")
FIXTURE_ROOT="$FIXTURE_DIR/fixture"
SUITE_LOG="$FIXTURE_DIR/suite.log"
SUITE_IDENTITY_FILE="$FIXTURE_DIR/suite.identity"
LEAK_PID_FILE="$FIXTURE_ROOT/tests/host/ml3_suite_descendant_regression.leak.pid"
LEAK_SENTINEL="$FIXTURE_ROOT/tests/host/ml3_suite_descendant_regression.leak.sentinel"
LEAK_IDENTITY_FILE="$FIXTURE_ROOT/tests/host/ml3_suite_descendant_regression.leak.identity"
suite_pid=
PROCESS_GUARD="$ROOT_DIR/tests/host/ml3_process_guard.sh"

. "$PROCESS_GUARD"

ml3_now_ms() {
  date +%s%3N
}

wait_for_path() {
  local path=$1
  local deadline_ms=$2
  while [ ! -e "$path" ]; do
    if [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
      return 1
    fi
    sleep 0.05
  done
}

write_stage() {
  local path=$1
  local body=$2
  mkdir -p "$(dirname -- "$path")"
  cat >"$path" <<EOF
#!/usr/bin/env bash
set -eu
$body
EOF
  chmod +x "$path"
}

write_suite_fixture() {
  write_stage "$FIXTURE_ROOT/tests/host/ml3_readiness_cohesion_contract.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/run_ml3_host_tests.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_marker_mutation_regression.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_signal_term_regression.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_runner_signal_status_regression.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_runner_descendant_regression.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_concurrency_regression.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_clean_tree_after_make_test.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_suite_signal_status_regression.sh" ':'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_suite_descendant_regression.sh" 'fixture_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
printf "%s\n" "$$" >"$fixture_root/tests/host/ml3_suite_descendant_regression.leak.pid"
: >"$fixture_root/tests/host/ml3_suite_descendant_regression.leak.sentinel"
trap "" HUP INT TERM
while :; do
  sleep 1
done'
  write_stage "$FIXTURE_ROOT/tests/host/ml3_suite_signal_term_regression.sh" ':'
}

cleanup() {
  local status=0

  if [ -f "$SUITE_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$SUITE_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  if [ -f "$LEAK_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$LEAK_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  if [ -n "${suite_pid:-}" ] && [ -f "$SUITE_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$SUITE_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  rm -rf "$FIXTURE_DIR"
  return "$status"
}

trap 'if ! cleanup; then exit 1; fi' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p "$FIXTURE_ROOT/tests/host"
git -C "$FIXTURE_DIR" init -q
cp "$SUITE" "$FIXTURE_ROOT/tests/host/run_ml3_host_suite.sh"
cp "$ROOT_DIR/tests/host/ml3_process_guard.sh" "$FIXTURE_ROOT/tests/host/ml3_process_guard.sh"
chmod +x "$FIXTURE_ROOT/tests/host/run_ml3_host_suite.sh"

write_suite_fixture

run_suite() {
  suite_start_deadline_ms=$(( $(ml3_now_ms) + 1000 ))
  set +e
  setsid env TMPDIR="$FIXTURE_DIR/tmp" ML3_SUITE_STAGE_TIMEOUT_MS=5000 "$FIXTURE_ROOT/tests/host/run_ml3_host_suite.sh" >"$SUITE_LOG" 2>&1 &
  suite_pid=$!
  set -e

  if ! read -r SUITE_PID SUITE_PGID SUITE_SESSION SUITE_STARTTIME \
    < <(ml3_capture_process_identity_with_deadline "$suite_pid" $suite_start_deadline_ms); then
    printf 'suite descendant regression failed to capture suite identity\n'
    cat "$SUITE_LOG"
    return 1
  fi
  printf '%s %s %s %s\n' "$SUITE_PID" "$SUITE_PGID" "$SUITE_SESSION" "$SUITE_STARTTIME" >"$SUITE_IDENTITY_FILE"

  if ! wait_for_path "$LEAK_SENTINEL" $(( $(ml3_now_ms) + 2000 )); then
    printf 'suite descendant regression did not observe leaking stage sentinel\n'
    cat "$SUITE_LOG"
    return 1
  fi

  leak_pid=$(cat "$LEAK_PID_FILE" 2>/dev/null || true)
  if [ -z "$leak_pid" ]; then
    printf 'suite descendant regression did not capture leaking stage pid\n'
    cat "$SUITE_LOG"
    return 1
  fi
  if ! ml3_store_process_identity_file "$LEAK_IDENTITY_FILE" "$leak_pid"; then
    printf 'suite descendant regression failed to capture leaking stage identity\n'
    cat "$SUITE_LOG"
    return 1
  fi

  if ! ml3_wait_for_owned_process_identity_file "$SUITE_IDENTITY_FILE" $(( $(ml3_now_ms) + 20000 )); then
    printf 'suite descendant regression timed out waiting for suite completion\n'
    cat "$SUITE_LOG"
    return 1
  fi

  if wait "$suite_pid"; then
    status=0
  else
    status=$?
  fi

  if [ "$status" -ne 1 ]; then
    printf 'suite descendant regression expected 1, got %s\n' "$status"
    cat "$SUITE_LOG"
    return 1
  fi

  if ! ml3_drain_owned_process_identity_file "$SUITE_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
    printf 'suite descendant regression left suite process alive\n'
    return 1
  fi
  if ! ml3_drain_owned_process_identity_file "$LEAK_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
    printf 'suite descendant regression left leaking stage alive\n'
    return 1
  fi

  return 0
}

run_suite
exit_status=$?
if ! cleanup; then
  exit 1
fi
exit "$exit_status"
