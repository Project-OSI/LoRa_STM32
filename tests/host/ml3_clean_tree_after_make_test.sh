#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SUITE="${SUITE_PATH:-$ROOT_DIR/tests/host/run_ml3_host_suite.sh}"

ARTIFACT_FIXTURE_DIR=
CLEAN_FIXTURE_DIR=

write_stage() {
  local path=$1
  shift
  mkdir -p "$(dirname -- "$path")"
  cat >"$path" <<EOF
#!/usr/bin/env bash
set -eu
$*
EOF
  chmod +x "$path"
}

make_fixture() {
  local fixture_dir=$1
  local artifact_mode=$2

  mkdir -p "$fixture_dir/fixture/tests/host"
  git -C "$fixture_dir/fixture" init -q
  cp "$SUITE" "$fixture_dir/fixture/tests/host/run_ml3_host_suite.sh"
  cp "$ROOT_DIR/tests/host/ml3_process_guard.sh" "$fixture_dir/fixture/tests/host/ml3_process_guard.sh"
  chmod +x "$fixture_dir/fixture/tests/host/run_ml3_host_suite.sh"

  write_stage "$fixture_dir/fixture/tests/host/ml3_readiness_cohesion_contract.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_target_integration_contract.sh" ':'
  if [ "$artifact_mode" = "early-artifact" ]; then
    write_stage "$fixture_dir/fixture/tests/host/run_ml3_host_tests.sh" 'fixture_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
printf "%s\n" "EARLY artifact produced by fixture stage" >"$fixture_root/early-suite-artifact.txt"'
  else
    write_stage "$fixture_dir/fixture/tests/host/run_ml3_host_tests.sh" ':'
  fi
  write_stage "$fixture_dir/fixture/tests/host/ml3_marker_mutation_regression.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_signal_term_regression.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_runner_signal_status_regression.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_suite_signal_status_regression.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_runner_descendant_regression.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_concurrency_regression.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_clean_tree_after_make_test.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_suite_signal_term_regression.sh" ':'
  write_stage "$fixture_dir/fixture/tests/host/ml3_suite_descendant_regression.sh" ':'
}

run_fixture_suite() {
  local fixture_dir=$1
  local log_file=$2
  local status=0

  if timeout -k 5s 20s env ML3_SUITE_STAGE_TIMEOUT_MS=5000 "$fixture_dir/fixture/tests/host/run_ml3_host_suite.sh" >"$log_file" 2>&1; then
    status=0
  else
    status=$?
  fi
  printf '%s\n' "$status"
}

check_clean_status() {
  local before=$1
  local after=$2
  if [ "$before" != "$after" ]; then
    printf 'suite changed worktree status\n'
    printf 'before:\n%s\n' "$before"
    printf 'after:\n%s\n' "$after"
    return 1
  fi
  return 0
}

cleanup_fixture_dir() {
  local fixture_dir=$1
  if [ -n "$fixture_dir" ] && [ -d "$fixture_dir" ]; then
    rm -rf "$fixture_dir"
  fi
}

cleanup() {
  cleanup_fixture_dir "$ARTIFACT_FIXTURE_DIR"
  ARTIFACT_FIXTURE_DIR=
  cleanup_fixture_dir "$CLEAN_FIXTURE_DIR"
  CLEAN_FIXTURE_DIR=
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

ARTIFACT_FIXTURE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-suite-clean-artifact.XXXXXX")
artifact_fixture="$ARTIFACT_FIXTURE_DIR"
artifact_log="$artifact_fixture/suite.log"
make_fixture "$artifact_fixture" early-artifact
artifact_status=$(run_fixture_suite "$artifact_fixture" "$artifact_log")
if [ "$artifact_status" -eq 0 ]; then
  if [ -e "$artifact_fixture/fixture/early-suite-artifact.txt" ]; then
    printf 'suite incorrectly accepted early artifact: %s\n' "$artifact_fixture/fixture/early-suite-artifact.txt"
    cat "$artifact_log"
    rm -rf "$artifact_fixture"
    exit 1
  fi
  printf 'suite unexpectedly passed without exposing the early artifact miss\n'
  cat "$artifact_log"
  exit 1
fi
if ! grep -q 'suite changed worktree status' "$artifact_log"; then
  printf 'suite failed without cleanliness diagnostic\n'
  cat "$artifact_log"
  exit 1
fi

CLEAN_FIXTURE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/ml3-suite-clean-green.XXXXXX")
clean_fixture="$CLEAN_FIXTURE_DIR"
clean_log="$clean_fixture/suite.log"
make_fixture "$clean_fixture" clean
clean_before=$(git -C "$clean_fixture/fixture" status --porcelain --untracked-files=all)
clean_status=$(run_fixture_suite "$clean_fixture" "$clean_log")
clean_after=$(git -C "$clean_fixture/fixture" status --porcelain --untracked-files=all)
if [ "$clean_status" -ne 0 ]; then
  printf 'suite clean run unexpectedly failed with status %s\n' "$clean_status"
  cat "$clean_log"
  exit 1
fi
if ! check_clean_status "$clean_before" "$clean_after"; then
  cat "$clean_log"
  exit 1
fi

printf 'clean-tree regression passed\n'
