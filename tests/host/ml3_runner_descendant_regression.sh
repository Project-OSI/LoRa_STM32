#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
RUNNER="$ROOT_DIR/tests/host/run_ml3_host_tests.sh"
TEST_TMPDIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-runner-leak.XXXXXX")
RUNNER_TMPDIR=$(mktemp -d "$TEST_TMPDIR/runner.XXXXXX")
LOG_FILE="$TEST_TMPDIR/runner.log"
GREP_SENTINEL="$TEST_TMPDIR/grep-sentinel"
CC_LEAK_SENTINEL="$TEST_TMPDIR/cc-leak-sentinel"
CC_PID_FILE="$TEST_TMPDIR/cc.pid"
CC_IDENTITY_FILE="$TEST_TMPDIR/cc.identity"
RUNNER_IDENTITY_FILE="$TEST_TMPDIR/runner.identity"
runner_pid=
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

cleanup() {
  local status=0

  if [ -f "$RUNNER_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$RUNNER_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  if [ -f "$CC_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$CC_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  if [ -d "$(dirname -- "$CC_PID_FILE")" ]; then
    : >"$CC_PID_FILE"
  fi
  rm -rf "$TEST_TMPDIR"
  return "$status"
}

trap 'if ! cleanup; then exit 1; fi' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

grep() {
  if [ ! -e "$GREP_SENTINEL" ]; then
    : >"$GREP_SENTINEL"
    sleep 0.2
  fi
  command grep "$@"
}
export -f grep
export GREP_SENTINEL CC_LEAK_SENTINEL CC_PID_FILE

cat >"$TEST_TMPDIR/cc-wrapper.sh" <<'EOF_WRAPPER'
#!/usr/bin/env bash
set -eu
printf '%s\n' "$$" >"$CC_PID_FILE"
if [ ! -e "$CC_LEAK_SENTINEL" ]; then
  : >"$CC_LEAK_SENTINEL"
fi
trap '' HUP INT TERM
while :; do
  sleep 1
done
EOF_WRAPPER
chmod +x "$TEST_TMPDIR/cc-wrapper.sh"

set +e
setsid timeout -k 5s 20s env TMPDIR="$RUNNER_TMPDIR" CC="$TEST_TMPDIR/cc-wrapper.sh" "$RUNNER" >"$LOG_FILE" 2>&1 &
runner_pid=$!
set -e

if ! read -r RUNNER_PID RUNNER_PGID RUNNER_SESSION RUNNER_STARTTIME \
  < <(ml3_capture_process_identity_with_deadline "$runner_pid" $(( $(ml3_now_ms) + 1000 ))); then
  printf 'runner leak regression failed to capture runner identity\n'
  cat "$LOG_FILE"
  exit 1
fi
printf '%s %s %s %s\n' "$RUNNER_PID" "$RUNNER_PGID" "$RUNNER_SESSION" "$RUNNER_STARTTIME" >"$RUNNER_IDENTITY_FILE"

sentinel_deadline_ms=$(( $(ml3_now_ms) + 2000 ))
if ! wait_for_path "$GREP_SENTINEL" "$sentinel_deadline_ms"; then
  printf 'runner leak regression did not observe config validation sentinel\n'
  cat "$LOG_FILE"
  exit 1
fi
if ! wait_for_path "$CC_LEAK_SENTINEL" "$sentinel_deadline_ms"; then
  printf 'runner leak regression did not observe leaking compiler invocation\n'
  cat "$LOG_FILE"
  exit 1
fi

cc_pid=$(cat "$CC_PID_FILE" 2>/dev/null || true)
if [ -n "$cc_pid" ]; then
  if ! ml3_store_process_identity_file "$CC_IDENTITY_FILE" "$cc_pid"; then
    printf 'runner leak regression could not capture compiler identity\n'
    cat "$LOG_FILE"
    exit 1
  fi
fi

if ! ml3_wait_for_owned_process_identity_file "$RUNNER_IDENTITY_FILE" $(( $(ml3_now_ms) + 22000 )); then
  printf 'runner leak regression timed out instead of failing cleanly\n'
  cat "$LOG_FILE"
  exit 1
fi

if wait "$runner_pid" 2>/dev/null; then
  status=0
else
  status=$?
fi
runner_pid=

if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
  printf 'runner leak regression timed out instead of failing cleanly (status %s)\n' "$status"
  cat "$LOG_FILE"
  exit 1
fi

if ! ml3_drain_owned_process_identity_file "$RUNNER_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
  printf 'runner leak regression left runner process alive\n'
  exit 1
fi
if ! ml3_drain_owned_process_identity_file "$CC_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
  printf 'runner leak regression left compiler wrapper alive\n'
  exit 1
fi

if [ "$status" -ne 1 ]; then
  printf 'runner leak regression expected 1, got %s\n' "$status"
  cat "$LOG_FILE"
  exit 1
fi

if [ -n "$(find "$RUNNER_TMPDIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
  printf 'runner leak regression left runner artifacts under TMPDIR\n'
  find "$RUNNER_TMPDIR" -mindepth 1 -maxdepth 1 -print
  exit 1
fi

if grep -q 'unbound variable' "$LOG_FILE"; then
  printf 'runner leak regression log still contains an unbound-variable error\n'
  cat "$LOG_FILE"
  exit 1
fi
