#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
RUNNER="$ROOT_DIR/tests/host/run_ml3_host_tests.sh"
TEST_TMPDIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-runner-status.XXXXXX")
RUNNER_TMPDIR=$(mktemp -d "$TEST_TMPDIR/runner.XXXXXX")
MUTATED_RUNNER="$TEST_TMPDIR/run_ml3_host_tests.sh"
MUTATED_RUNNER_FLATTEN="$TEST_TMPDIR/run_ml3_host_tests.flatten.sh"
LOG_FILE="$TEST_TMPDIR/runner.log"
GREP_SENTINEL="$TEST_TMPDIR/grep-sentinel"
CC_SENTINEL="$TEST_TMPDIR/cc-sentinel"
CC_PID_FILE="$TEST_TMPDIR/cc.pid"
CC_IDENTITY_FILE="$TEST_TMPDIR/cc.identity"
RUNNER_IDENTITY_FILE="$TEST_TMPDIR/runner.identity"
CC_WRAPPER="$TEST_TMPDIR/cc-wrapper.sh"
ML3_STATUS_RUNNER_PID=
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

make_mutated_runner() {
  local output=$1
  local flatten=$2
  local tmp_output="$output.tmp"

  awk -v override_root="$ROOT_DIR" -v override_guard="$PROCESS_GUARD" -v include_flatten="$flatten" '
    BEGIN {
      inserted_overrides = 0
    }

    function emit_request_shutdown() {
      print "ml3_request_shutdown() {"
      print "  local signal=$1"
      print "  if [ -z \"${REQUESTED_EXIT_CODE:-}\" ]; then"
      print "    REQUESTED_EXIT_CODE=$(ml3_signal_exit_code \"$signal\")"
      print "  fi"
      print "  if [ -z \"${ACTIVE_JOB_SHUTDOWN_SIGNAL:-}\" ]; then"
      print "    ACTIVE_JOB_SHUTDOWN_SIGNAL=$signal"
      print "  fi"
      print "  if [ -z \"${ACTIVE_JOB_SHUTDOWN_DEADLINE_MS:-}\" ]; then"
      print "    ACTIVE_JOB_SHUTDOWN_DEADLINE_MS=$(( $(ml3_now_ms) + ACTIVE_JOB_KILL_GRACE_MS ))"
      print "  fi"
      print ""
      print "  if [ \"${ML3_FORCE_DRAIN_FAILURE:-0}\" = \"1\" ]; then"
      print "    ACTIVE_JOB_PGID="
      print "    ACTIVE_JOB_SESSION="
      print "    ACTIVE_JOB_STARTTIME="
      print "  fi"
      print "}"
      print ""
    }

    function emit_wait_for_shutdown() {
      print "ml3_wait_for_shutdown() {"
      print "  local status=$1"
      print "  local now_ms=0"
      print ""
      print "  while :; do"
      print "    if [ -n \"${REQUESTED_EXIT_CODE:-}\" ]; then"
      print "      if ! ml3_drain_active_job; then"
      print "        if [ \"${ML3_FLATTEN_DRAIN_FAILURE:-0}\" = \"1\" ]; then"
      print "          ml3_clear_active_job"
      print "          return \"$REQUESTED_EXIT_CODE\""
      print "        fi"
      print "        return 1"
      print "      fi"
      print "      return \"$REQUESTED_EXIT_CODE\""
      print "    fi"
      print ""
      print "    if ! ml3_process_tree_alive \\"
      print "      \"$ACTIVE_JOB_PID\" \\"
      print "      \"$ACTIVE_JOB_PGID\" \\"
      print "      \"$ACTIVE_JOB_SESSION\" \\"
      print "      \"$ACTIVE_JOB_STARTTIME\"; then"
      print "      wait \"$ACTIVE_JOB_PID\" 2>/dev/null || status=$?"
      print "      ml3_clear_active_job"
      print "      return \"$status\""
      print "    fi"
      print ""
      print "    now_ms=$(ml3_now_ms)"
      print "    if [ -n \"${ACTIVE_JOB_STAGE_DEADLINE_MS:-}\" ] && [ \"$now_ms\" -ge \"${ACTIVE_JOB_STAGE_DEADLINE_MS}\" ]; then"
      print "      if ! ml3_drain_active_job; then"
      print "        if [ \"${ML3_FLATTEN_DRAIN_FAILURE:-0}\" = \"1\" ]; then"
      print "          ml3_clear_active_job"
      print "          return \"$REQUESTED_EXIT_CODE\""
      print "        fi"
      print "        return 1"
      print "      fi"
      print "      return \"$REQUESTED_EXIT_CODE\""
      print "    fi"
      print ""
      print "    sleep 0.05"
      print "  done"
      print "}"
      print ""
    }

    {
      if ($0 ~ /^ROOT_DIR=/) {
        print "ROOT_DIR=\"" override_root "\""
        next
      }

      if ($0 ~ /^PROCESS_GUARD=/) {
        print "PROCESS_GUARD=\"" override_guard "\""
        next
      }

      if (!inserted_overrides && $0 ~ /^cleanup\(\)[[:space:]]*{$/) {
        emit_request_shutdown()
        if (include_flatten == "1") {
          emit_wait_for_shutdown()
        }
        inserted_overrides = 1
      }

      print
    }
  ' "$RUNNER" > "$tmp_output" && mv "$tmp_output" "$output"

  if [ ! -f "$output" ]; then
    return 1
  fi

  chmod +x "$output"
}

cleanup() {
  local status=0

  if [ -z "${TEST_TMPDIR:-}" ] || [ ! -d "$TEST_TMPDIR" ]; then
    return 0
  fi

  if [ -n "${ML3_STATUS_RUNNER_PID:-}" ]; then
    if ! ml3_drain_owned_process_identity_file "$RUNNER_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
    ML3_STATUS_RUNNER_PID=
  elif ! ml3_drain_owned_process_identity_file "$RUNNER_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
    status=1
  fi
  if [ -f "$CC_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$CC_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi

  if [ -d "$(dirname -- "$CC_PID_FILE")" ]; then
    : >"$CC_PID_FILE"
  fi
  if [ "$status" -ne 0 ]; then
    return 1
  fi

  return 0
}

run_case() {
  local name=$1
  local flatten=$2
  local expected_status=$3
  local runner_script=$4
  local runner_log=$5
  local status=0
  local runner_pid=
  local cc_pid=

  rm -f "$GREP_SENTINEL" "$CC_SENTINEL" "$CC_PID_FILE" "$CC_IDENTITY_FILE" "$RUNNER_IDENTITY_FILE"

  make_mutated_runner "$runner_script" "$flatten"

cat >"$CC_WRAPPER" <<'EOF'
#!/usr/bin/env bash
set -eu
printf '%s\n' "$$" >"$CC_PID_FILE"
: >"$CC_SENTINEL"
trap '' HUP INT TERM
while :; do
  sleep 1
done
EOF
  chmod +x "$CC_WRAPPER"

  set +e
  export CC_PID_FILE CC_SENTINEL
  ML3_FORCE_DRAIN_FAILURE=1 \
  ML3_FLATTEN_DRAIN_FAILURE="$flatten" \
  setsid env TMPDIR="$RUNNER_TMPDIR" CC="$CC_WRAPPER" "$runner_script" >"$runner_log" 2>&1 &
  ML3_STATUS_RUNNER_PID=$!
  runner_pid=$ML3_STATUS_RUNNER_PID
  set -e

  if ! read -r RUNNER_PID RUNNER_PGID RUNNER_SESSION RUNNER_STARTTIME \
    < <(ml3_capture_process_identity_with_deadline "$runner_pid" $(( $(ml3_now_ms) + 1000 ))); then
    printf '%s runner status regression failed to capture runner identity\n' "$name"
    cat "$runner_log"
    return 1
  fi
  printf '%s %s %s %s\n' "$RUNNER_PID" "$RUNNER_PGID" "$RUNNER_SESSION" "$RUNNER_STARTTIME" >"$RUNNER_IDENTITY_FILE"

  if ! wait_for_path "$GREP_SENTINEL" $(( $(ml3_now_ms) + 2000 )); then
    printf '%s runner status regression did not observe config validation sentinel\n' "$name"
    cat "$runner_log"
    return 1
  fi
  if ! wait_for_path "$CC_SENTINEL" $(( $(ml3_now_ms) + 2000 )); then
    printf '%s runner status regression did not observe compiler entry sentinel\n' "$name"
    cat "$runner_log"
    return 1
  fi

  cc_pid=$(cat "$CC_PID_FILE" 2>/dev/null || true)
  if [ -n "$cc_pid" ]; then
    if ! ml3_store_process_identity_file "$CC_IDENTITY_FILE" "$cc_pid"; then
      printf '%s runner status regression could not capture compiler identity\n' "$name"
      cat "$runner_log"
      return 1
    fi
  fi

  if ! ml3_signal_owned_process_identity_file TERM "$RUNNER_IDENTITY_FILE"; then
    printf '%s runner status regression could not signal runner identity\n' "$name"
    cat "$runner_log"
    return 1
  fi

  if ! ml3_wait_for_owned_process_identity_file "$RUNNER_IDENTITY_FILE" $(( $(ml3_now_ms) + 5000 )); then
    printf '%s runner status regression timed out waiting for runner completion\n' "$name"
    cat "$runner_log"
    return 1
  fi

  if wait "$runner_pid"; then
    status=0
  else
    status=$?
  fi
  ML3_STATUS_RUNNER_PID=

  if [ "$status" -ne "$expected_status" ]; then
    printf '%s runner status regression expected %s, got %s\n' "$name" "$expected_status" "$status"
    cat "$runner_log"
    return 1
  fi

  if ! ml3_drain_owned_process_identity_file "$CC_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
    printf '%s runner status regression left compiler wrapper alive\n' "$name"
    return 1
  fi

  if grep -q 'unbound variable' "$runner_log"; then
    printf '%s runner status regression log still contains an unbound-variable error\n' "$name"
    cat "$runner_log"
    return 1
  fi

  if ! cleanup; then
    printf '%s runner status regression cleanup failed\n' "$name"
    cat "$runner_log"
    return 1
  fi

  return 0
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
export GREP_SENTINEL

if ! run_case "runner_real_drain_failure" "0" 1 "$MUTATED_RUNNER" "$LOG_FILE"; then
  exit 1
fi
if ! run_case "runner_flattened_drain_failure" "1" 143 "$MUTATED_RUNNER_FLATTEN" "$TEST_TMPDIR/runner_flatten.log"; then
  exit 1
fi

if ! cleanup; then
  exit 1
fi
rm -rf "$TEST_TMPDIR"
