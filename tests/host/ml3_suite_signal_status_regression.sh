#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SUITE="$ROOT_DIR/tests/host/run_ml3_host_suite.sh"
TEST_TMPDIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-suite-status.XXXXXX")
SUITE_TMPDIR=$(mktemp -d "$TEST_TMPDIR/suite.XXXXXX")
MUTATED_SUITE="$TEST_TMPDIR/run_ml3_host_suite.sh"
MUTATED_SUITE_FLATTEN="$TEST_TMPDIR/run_ml3_host_suite.flatten.sh"
LOG_FILE="$TEST_TMPDIR/suite.log"
LOG_FILE_FLATTEN="$TEST_TMPDIR/suite_flatten.log"
SUITE_IDENTITY_FILE="$TEST_TMPDIR/suite.identity"
CC_PID_FILE="$TEST_TMPDIR/cc.pid"
CC_IDENTITY_FILE="$TEST_TMPDIR/cc.identity"
CC_SENTINEL="$TEST_TMPDIR/cc-sentinel"
CC_WRAPPER="$TEST_TMPDIR/cc-wrapper.sh"
GREP_SENTINEL="$TEST_TMPDIR/grep-sentinel"
SUITE_PID=
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

make_mutated_suite() {
  local output=$1
  local flatten=$2

  local tmp_output="$output.tmp"
  if ! {
    grep -v 'ml3_suite_signal_status_regression.sh"' "$SUITE" | \
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
        print "  if [ -z \"${ACTIVE_STAGE_SHUTDOWN_SIGNAL:-}\" ]; then"
        print "    ACTIVE_STAGE_SHUTDOWN_SIGNAL=$signal"
        print "  fi"
        print "  if [ -z \"${ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS:-}\" ]; then"
        print "    ACTIVE_STAGE_SHUTDOWN_DEADLINE_MS=$(( $(ml3_now_ms) + ACTIVE_STAGE_KILL_GRACE_MS ))"
        print "  fi"
        print ""
        print "  if [ \"${ML3_FORCE_DRAIN_FAILURE:-0}\" = \"1\" ]; then"
        print "    ACTIVE_STAGE_PID=0"
        print "    ACTIVE_STAGE_PGID="
        print "    ACTIVE_STAGE_SESSION="
        print "    ACTIVE_STAGE_STARTTIME="
        print "    ACTIVE_STAGE_SHUTDOWN_SIGNAL=$signal"
        print "  fi"
        print "}"
        print ""
      }

      function emit_wait_for_shutdown() {
        print "ml3_wait_for_stage_shutdown() {"
        print "  local status=0"
        print "  local now_ms=0"
        print ""
        print "  while :; do"
        print "    if [ -n \"${REQUESTED_EXIT_CODE:-}\" ]; then"
        print "      if ! ml3_drain_active_stage; then"
        print "        if [ \"${ML3_FLATTEN_DRAIN_FAILURE:-0}\" = \"1\" ]; then"
        print "          ml3_clear_active_stage"
        print "          return \"$REQUESTED_EXIT_CODE\""
        print "        fi"
        print "        return 1"
        print "      fi"
        print "      return \"$REQUESTED_EXIT_CODE\""
        print "    fi"
        print ""
        print "    if ! ml3_process_tree_alive \\"
        print "      \"$ACTIVE_STAGE_PID\" \\"
        print "      \"$ACTIVE_STAGE_PGID\" \\"
        print "      \"$ACTIVE_STAGE_SESSION\" \\"
        print "      \"$ACTIVE_STAGE_STARTTIME\"; then"
        print "      wait \"$ACTIVE_STAGE_PID\" 2>/dev/null || status=$?"
        print "      ml3_clear_active_stage"
        print "      return \"$status\""
        print "    fi"
        print ""
        print "    now_ms=$(ml3_now_ms)"
        print "    if [ -n \"${ACTIVE_STAGE_WAIT_DEADLINE_MS:-}\" ] && [ \"$now_ms\" -ge \"$ACTIVE_STAGE_WAIT_DEADLINE_MS\" ]; then"
        print "      if ! ml3_drain_active_stage; then"
        print "        if [ \"${ML3_FLATTEN_DRAIN_FAILURE:-0}\" = \"1\" ]; then"
        print "          ml3_clear_active_stage"
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
    ' >"$tmp_output"
  }; then
    return 1
  fi

  if ! mv "$tmp_output" "$output"; then
    return 1
  fi

  chmod +x "$output"
}

run_case() {
  local name=$1
  local flatten=$2
  local expected_status=$3
  local suite_file=$4
  local log_file=$5
  local status=0
  local suite_pid=
  local cc_pid=

  rm -f "$CC_SENTINEL" "$GREP_SENTINEL" "$CC_PID_FILE" "$CC_IDENTITY_FILE" "$SUITE_IDENTITY_FILE"

  make_mutated_suite "$suite_file" "$flatten"

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
  setsid env TMPDIR="$SUITE_TMPDIR" CC="$CC_WRAPPER" "$suite_file" >"$log_file" 2>&1 &
  SUITE_PID=$!
  suite_pid=$SUITE_PID
  set -e

  if ! read -r SUITE_PID_VAL SUITE_PGID SUITE_SESSION SUITE_STARTTIME \
    < <(ml3_capture_process_identity_with_deadline "$suite_pid" $(( $(ml3_now_ms) + 1000 ))); then
    printf '%s suite status regression failed to capture suite identity\n' "$name"
    cat "$log_file"
    return 1
  fi
  printf '%s %s %s %s\n' "$SUITE_PID_VAL" "$SUITE_PGID" "$SUITE_SESSION" "$SUITE_STARTTIME" >"$SUITE_IDENTITY_FILE"

  if ! wait_for_path "$CC_SENTINEL" $(( $(ml3_now_ms) + 2000 )); then
    printf '%s suite status regression did not observe compiler wrapper sentinel\n' "$name"
    cat "$log_file"
    return 1
  fi
  if [ ! -s "$CC_PID_FILE" ]; then
    printf '%s suite status regression missing compiler pid\n' "$name"
    cat "$log_file"
    return 1
  fi

  cc_pid=$(cat "$CC_PID_FILE" 2>/dev/null || true)
  if [ -n "$cc_pid" ]; then
    if ! ml3_store_process_identity_file "$CC_IDENTITY_FILE" "$cc_pid"; then
      printf '%s suite status regression could not capture compiler identity\n' "$name"
      cat "$log_file"
      return 1
    fi
  fi

  if ! ml3_signal_owned_process_identity_file TERM "$SUITE_IDENTITY_FILE"; then
    printf '%s suite status regression could not signal suite identity\n' "$name"
    cat "$log_file"
    return 1
  fi

  if ! ml3_wait_for_owned_process_identity_file "$SUITE_IDENTITY_FILE" $(( $(ml3_now_ms) + 5000 )); then
    printf '%s suite status regression timed out waiting for suite completion\n' "$name"
    cat "$log_file"
    return 1
  fi

  if wait "$suite_pid"; then
    status=0
  else
    status=$?
  fi
  SUITE_PID=

  if [ "$status" -ne "$expected_status" ]; then
    printf '%s suite status regression expected %s, got %s\n' "$name" "$expected_status" "$status"
    cat "$log_file"
    return 1
  fi

  if ! ml3_drain_owned_process_identity_file "$CC_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
    printf '%s suite status regression left compiler wrapper alive\n' "$name"
    return 1
  fi
  if ! ml3_drain_owned_process_identity_file "$SUITE_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
    return 1
  fi

  if grep -q 'unbound variable' "$log_file"; then
    printf '%s suite status regression log still contains unbound-variable error\n' "$name"
    cat "$log_file"
    return 1
  fi

  return 0
}

cleanup() {
  local status=0

  if [ -z "${TEST_TMPDIR:-}" ] || [ ! -d "$TEST_TMPDIR" ]; then
    return 0
  fi

  if [ -n "${SUITE_PID:-}" ]; then
    if ! ml3_drain_owned_process_identity_file "$SUITE_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
    SUITE_PID=
  fi
  if ! ml3_drain_owned_process_identity_file "$CC_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
    status=1
  fi
  if [ "$status" -ne 0 ]; then
    return 1
  fi
  return 0
}

trap 'if ! cleanup; then exit 1; fi' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

export GREP_SENTINEL

if ! run_case "suite_real_drain_failure" "0" 1 "$MUTATED_SUITE" "$LOG_FILE"; then
  exit 1
fi
if ! run_case "suite_flattened_drain_failure" "1" 143 "$MUTATED_SUITE_FLATTEN" "$LOG_FILE_FLATTEN"; then
  exit 1
fi

rm -rf "$TEST_TMPDIR"
