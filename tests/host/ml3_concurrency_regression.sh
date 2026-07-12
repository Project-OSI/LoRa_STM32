#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
RUNNER="$ROOT_DIR/tests/host/run_ml3_host_tests.sh"
REAL_CC="${CC:-gcc}"
PRE_STATUS=$(git -C "$ROOT_DIR" status --porcelain --untracked-files=all)
ROUND_ROOT=
RUNNER_A_PID=
RUNNER_B_PID=
RUNNER_A_IDENTITY_FILE=
RUNNER_B_IDENTITY_FILE=
WRAPPER_A_PID_FILE=
WRAPPER_B_PID_FILE=
WRAPPER_A_IDENTITY_FILE=
WRAPPER_B_IDENTITY_FILE=
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

  if [ -n "${RUNNER_A_IDENTITY_FILE:-}" ] && [ -f "$RUNNER_A_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$RUNNER_A_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  if [ -n "${RUNNER_B_IDENTITY_FILE:-}" ] && [ -f "$RUNNER_B_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$RUNNER_B_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  if [ -n "${WRAPPER_A_IDENTITY_FILE:-}" ] && [ -f "$WRAPPER_A_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$WRAPPER_A_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  if [ -n "${WRAPPER_B_IDENTITY_FILE:-}" ] && [ -f "$WRAPPER_B_IDENTITY_FILE" ]; then
    if ! ml3_drain_owned_process_identity_file "$WRAPPER_B_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi

  if [ -n "${ROUND_ROOT:-}" ] && [ -d "$ROUND_ROOT" ]; then
    rm -rf "$ROUND_ROOT"
  fi

  if [ "$status" -ne 0 ]; then
    return 1
  fi
}

trap 'if ! cleanup; then exit 1; fi' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

make_wrapper() {
  local wrapper=$1
  local pid_file=$2
  local arrival_file=$3
  local release_file=$4
  cat >"$wrapper" <<EOF
#!/usr/bin/env bash
set -eu
printf '%s\n' "\$\$" >"$pid_file"
: >"$arrival_file"
while [ ! -e "$release_file" ]; do
  sleep 0.05
done
exec "$REAL_CC" "\$@"
EOF
  chmod +x "$wrapper"
}

run_round() {
  local round_dir=$1
  local round_id=$2
  local wrapper_a="$round_dir/cc-a.sh"
  local wrapper_b="$round_dir/cc-b.sh"
  local wrapper_a_pid_file="$round_dir/cc-a.pid"
  local wrapper_b_pid_file="$round_dir/cc-b.pid"
  local wrapper_a_identity_file="$round_dir/cc-a.identity"
  local wrapper_b_identity_file="$round_dir/cc-b.identity"
  local arrival_a="$round_dir/runner-a.arrived"
  local arrival_b="$round_dir/runner-b.arrived"
  local release_file="$round_dir/release"
  local runner_a_tmp="$round_dir/runner-a-tmp"
  local runner_b_tmp="$round_dir/runner-b-tmp"
  local runner_a_log="$round_dir/runner-a.log"
  local runner_b_log="$round_dir/runner-b.log"
  local wait_deadline_ms
  local status_a=0
  local status_b=0
  local runner_a_identity_file="$round_dir/runner-a.identity"
  local runner_b_identity_file="$round_dir/runner-b.identity"

  mkdir -p "$runner_a_tmp" "$runner_b_tmp"
  make_wrapper "$wrapper_a" "$wrapper_a_pid_file" "$arrival_a" "$release_file"
  make_wrapper "$wrapper_b" "$wrapper_b_pid_file" "$arrival_b" "$release_file"

  WRAPPER_A_PID_FILE="$wrapper_a_pid_file"
  WRAPPER_B_PID_FILE="$wrapper_b_pid_file"

  setsid env TMPDIR="$runner_a_tmp" CC="$wrapper_a" "$RUNNER" >"$runner_a_log" 2>&1 &
  RUNNER_A_PID=$!
  setsid env TMPDIR="$runner_b_tmp" CC="$wrapper_b" "$RUNNER" >"$runner_b_log" 2>&1 &
  RUNNER_B_PID=$!

  if ! read -r RUNNER_A_PID READ_A_PGID READ_A_SESSION READ_A_STARTTIME \
    < <(ml3_capture_process_identity_with_deadline "$RUNNER_A_PID" $(( $(ml3_now_ms) + 1000 ))); then
    printf 'round %s could not capture runner A identity\n' "$round_id"
    cat "$runner_a_log"
    return 1
  fi
  printf '%s %s %s %s\n' "$RUNNER_A_PID" "$READ_A_PGID" "$READ_A_SESSION" "$READ_A_STARTTIME" >"$runner_a_identity_file"
  RUNNER_A_IDENTITY_FILE="$runner_a_identity_file"

  if ! read -r RUNNER_B_PID READ_B_PGID READ_B_SESSION READ_B_STARTTIME \
    < <(ml3_capture_process_identity_with_deadline "$RUNNER_B_PID" $(( $(ml3_now_ms) + 1000 ))); then
    printf 'round %s could not capture runner B identity\n' "$round_id"
    cat "$runner_b_log"
    return 1
  fi
  printf '%s %s %s %s\n' "$RUNNER_B_PID" "$READ_B_PGID" "$READ_B_SESSION" "$READ_B_STARTTIME" >"$runner_b_identity_file"
  RUNNER_B_IDENTITY_FILE="$runner_b_identity_file"

  wait_deadline_ms=$(( $(ml3_now_ms) + 4000 ))
  if ! wait_for_path "$arrival_a" "$wait_deadline_ms"; then
    printf 'round %s did not observe runner A arrival marker\n' "$round_id"
    cat "$runner_a_log"
    return 1
  fi
  if ! wait_for_path "$arrival_b" "$wait_deadline_ms"; then
    printf 'round %s did not observe runner B arrival marker\n' "$round_id"
    cat "$runner_a_log"
    cat "$runner_b_log"
    return 1
  fi

  if ! wait_for_path "$wrapper_a_pid_file" "$wait_deadline_ms"; then
    printf 'round %s did not capture wrapper A pid marker\n' "$round_id"
    return 1
  fi
  wrapper_a_pid=$(cat "$wrapper_a_pid_file" 2>/dev/null || true)
  if [ -z "$wrapper_a_pid" ]; then
    printf 'round %s did not capture wrapper A pid\n' "$round_id"
    return 1
  fi
  if ! ml3_store_process_identity_file "$wrapper_a_identity_file" "$wrapper_a_pid"; then
    printf 'round %s could not capture wrapper A identity\n' "$round_id"
    return 1
  fi
  WRAPPER_A_IDENTITY_FILE="$wrapper_a_identity_file"

  if ! wait_for_path "$wrapper_b_pid_file" "$wait_deadline_ms"; then
    printf 'round %s did not capture wrapper B pid marker\n' "$round_id"
    return 1
  fi
  wrapper_b_pid=$(cat "$wrapper_b_pid_file" 2>/dev/null || true)
  if [ -z "$wrapper_b_pid" ]; then
    printf 'round %s did not capture wrapper B pid\n' "$round_id"
    return 1
  fi
  if ! ml3_store_process_identity_file "$wrapper_b_identity_file" "$wrapper_b_pid"; then
    printf 'round %s could not capture wrapper B identity\n' "$round_id"
    return 1
  fi
  WRAPPER_B_IDENTITY_FILE="$wrapper_b_identity_file"

  : >"$release_file"

  if ! ml3_wait_for_owned_process_identity_file "$runner_a_identity_file" $(( $(ml3_now_ms) + 8000 )); then
    printf 'round %s runner A did not finish in time\n' "$round_id"
    cat "$runner_a_log"
    return 1
  fi
  if ! ml3_wait_for_owned_process_identity_file "$runner_b_identity_file" $(( $(ml3_now_ms) + 8000 )); then
    printf 'round %s runner B did not finish in time\n' "$round_id"
    cat "$runner_b_log"
    return 1
  fi

  if wait "$RUNNER_A_PID"; then
    status_a=0
  else
    status_a=$?
  fi
  if wait "$RUNNER_B_PID"; then
    status_b=0
  else
    status_b=$?
  fi
  RUNNER_A_PID=
  RUNNER_B_PID=

  if [ "$status_a" -ne 0 ] || [ "$status_b" -ne 0 ]; then
    printf 'round %s runner statuses were %s and %s\n' "$round_id" "$status_a" "$status_b"
    cat "$runner_a_log"
    cat "$runner_b_log"
    return 1
  fi

  if [ -e "$ROOT_DIR/tests/host/.build" ]; then
    printf 'round %s left repository build directory: %s\n' "$round_id" "$ROOT_DIR/tests/host/.build"
    return 1
  fi

  if ! ml3_drain_owned_process_identity_file "$wrapper_a_identity_file" $(( $(ml3_now_ms) + 1000 )); then
    printf 'round %s left wrapper A alive\n' "$round_id"
    return 1
  fi
  if ! ml3_drain_owned_process_identity_file "$wrapper_b_identity_file" $(( $(ml3_now_ms) + 1000 )); then
    printf 'round %s left wrapper B alive\n' "$round_id"
    return 1
  fi

  return 0
}

ROUND_COUNT=3
for round in $(seq 1 "$ROUND_COUNT"); do
  ROUND_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/ml3-concurrency.round${round}.XXXXXX")
  if ! run_round "$ROUND_ROOT" "$round"; then
    exit 1
  fi
  rm -rf "$ROUND_ROOT"
  ROUND_ROOT=
  RUNNER_A_PID=
  RUNNER_B_PID=
  RUNNER_A_IDENTITY_FILE=
  RUNNER_B_IDENTITY_FILE=
  WRAPPER_A_PID_FILE=
  WRAPPER_B_PID_FILE=
done

POST_STATUS=$(git -C "$ROOT_DIR" status --porcelain --untracked-files=all)
if [ "$POST_STATUS" != "$PRE_STATUS" ]; then
  printf 'concurrency regression changed worktree status\n'
  printf 'before:\n%s\n' "$PRE_STATUS"
  printf 'after:\n%s\n' "$POST_STATUS"
  exit 1
fi

if [ -e "$ROOT_DIR/tests/host/.build" ]; then
  printf 'concurrency regression left repository build directory: %s\n' "$ROOT_DIR/tests/host/.build"
  exit 1
fi

printf 'concurrency regression passed\n'
