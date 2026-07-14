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
CC_DRAIN_IDENTITY_FILE="$TEST_TMPDIR/cc.drain.identity"
RUNNER_IDENTITY_FILE="$TEST_TMPDIR/runner.identity"
CC_WRAPPER="$TEST_TMPDIR/cc-wrapper.sh"
ORPHAN_WRAPPER="$TEST_TMPDIR/orphan-wrapper.sh"
ORPHAN_LEADER_PID_FILE="$TEST_TMPDIR/orphan-leader.pid"
ORPHAN_DESCENDANT_PID_FILE="$TEST_TMPDIR/orphan-descendant.pid"
ORPHAN_IDENTITY_FILE="$TEST_TMPDIR/orphan.identity"
SPECIAL_COMM_SOURCE="$TEST_TMPDIR/special-comm.c"
SPECIAL_COMM_BINARY="$TEST_TMPDIR/special-comm"
SPECIAL_COMM_IDENTITY_FILE="$TEST_TMPDIR/special-comm.identity"
ML3_STATUS_RUNNER_PID=
PROCESS_GUARD="$ROOT_DIR/tests/host/ml3_process_guard.sh"
EXACT_STAT_STATE=
EXACT_STAT_PGID=
EXACT_STAT_SID=
EXACT_STAT_STARTTIME=
RECORDED_OWNER_PID=
RECORDED_OWNER_PGID=
RECORDED_OWNER_SID=
RECORDED_OWNER_STARTTIME=
VERIFIED_OWNER_PID=
VERIFIED_OWNER_PGID=
VERIFIED_OWNER_SID=
VERIFIED_OWNER_STARTTIME=
VERIFIED_OWNER_WRAPPER=
UNRECORDED_OWNER_PID=
UNRECORDED_OWNER_PGID=
UNRECORDED_OWNER_SID=
UNRECORDED_OWNER_STARTTIME=
UNRECORDED_OWNER_WRAPPER=
SAFE_OWNER_STARTTIME=

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

assert_guard_function_uses_in_process_stat_reads() {
  local function_name=$1
  local guard_file=${2:-$PROCESS_GUARD}
  local body_file="$TEST_TMPDIR/$function_name.body"

  awk -v signature="$function_name() {" '
    $0 == signature {
      capture = 1
    }
    capture && $0 != signature && $0 ~ /^[A-Za-z_][A-Za-z0-9_]*\(\) \{$/ {
      exit
    }
    capture {
      print
    }
  ' "$guard_file" >"$body_file"

  if [ ! -s "$body_file" ]; then
    printf 'process guard structural oracle could not find %s\n' "$function_name"
    return 1
  fi
  if command grep -Eq '(^|[^[:alnum:]_])cat[[:space:]]|\$\(|<\(|>\(' "$body_file"; then
    printf 'process guard structural oracle found a subprocess read in %s\n' "$function_name"
    return 1
  fi
  if [ "$function_name" != "ml3_read_process_stat_fields" ] && \
    ! command grep -Fq 'ml3_read_process_stat_fields "$pid"' "$body_file"; then
    printf 'process guard structural oracle found no in-process stat read in %s\n' "$function_name"
    return 1
  fi

  return 0
}

assert_guard_scan_structure() {
  local guard_file=${1:-$PROCESS_GUARD}

  assert_guard_function_uses_in_process_stat_reads ml3_read_process_stat_fields "$guard_file" && \
    assert_guard_function_uses_in_process_stat_reads ml3_capture_owned_pids "$guard_file" && \
    assert_guard_function_uses_in_process_stat_reads ml3_owned_process_exists "$guard_file"
}

assert_guard_scan_mutations_rejected() {
  local mutation=
  local mutation_name=
  local mutated_guard=
  local oracle_log=

  while IFS='|' read -r mutation_name mutation; do
    mutated_guard="$TEST_TMPDIR/process-guard-$mutation_name.sh"
    oracle_log="$TEST_TMPDIR/process-guard-$mutation_name.log"
    awk -v mutation="$mutation" '
      { print }
      $0 == "ml3_capture_owned_pids() {" { print "  " mutation }
    ' "$PROCESS_GUARD" >"$mutated_guard"
    if assert_guard_scan_structure "$mutated_guard" >"$oracle_log"; then
      printf 'process guard structural oracle accepted %s mutation\n' "$mutation_name"
      return 1
    fi
    if ! command grep -q 'structural oracle found a subprocess read' "$oracle_log"; then
      printf 'process guard structural oracle rejected %s mutation for the wrong reason\n' "$mutation_name"
      return 1
    fi
  done <<'EOF'
command-substitution|ignored=$(true)
source-process-substitution|source <(true)
readarray-process-substitution|readarray -t ignored <(true)
sink-process-substitution|printf x > >(true)
EOF

  return 0
}

assert_process_guard_owner_reuse_races_rejected() {
  local fake_pid=$$
  local read_count_file="$TEST_TMPDIR/owner-reuse.read-count"
  local capture_output="$TEST_TMPDIR/owner-reuse.capture"
  local kill_sentinel="$TEST_TMPDIR/owner-reuse.kill"
  local status=0

  rm -f "$read_count_file" "$capture_output" "$kill_sentinel"
  if (
    ml3_process_exists() {
      return 1
    }
    ml3_read_process_stat_fields() {
      local pid=$1
      local read_count=0

      [ "$pid" = "$fake_pid" ] || return 1
      if [ -s "$read_count_file" ]; then
        IFS= read -r read_count <"$read_count_file"
      fi
      read_count=$(( read_count + 1 ))
      printf '%s\n' "$read_count" >"$read_count_file"
      [ "$read_count" -gt 1 ] || return 1
      ML3_STAT_STATE=S
      ML3_STAT_PGID=$fake_pid
      ML3_STAT_SID=$fake_pid
      ML3_STAT_STARTTIME=222
      return 0
    }
    ml3_capture_owned_pids "$fake_pid" "$fake_pid" "$fake_pid" 111 >"$capture_output"
  ); then
    printf 'process guard capture accepted owner reuse during scan\n'
    return 1
  fi
  if [ -s "$capture_output" ]; then
    printf 'process guard capture leaked candidates before owner reuse validation\n'
    return 1
  fi

  rm -f "$read_count_file" "$kill_sentinel"
  if (
    ml3_process_exists() {
      return 1
    }
    ml3_read_process_stat_fields() {
      local pid=$1
      local read_count=0

      [ "$pid" = "$fake_pid" ] || return 1
      if [ -s "$read_count_file" ]; then
        IFS= read -r read_count <"$read_count_file"
      fi
      read_count=$(( read_count + 1 ))
      printf '%s\n' "$read_count" >"$read_count_file"
      [ "$read_count" -gt 1 ] || return 1
      ML3_STAT_STATE=S
      ML3_STAT_PGID=$fake_pid
      ML3_STAT_SID=$fake_pid
      ML3_STAT_STARTTIME=222
      return 0
    }
    kill() {
      : >"$kill_sentinel"
      return 0
    }
    ml3_signal_process_list TERM "$fake_pid" "$fake_pid" "$fake_pid" 111
  ); then
    status=0
  else
    status=$?
  fi
  if [ -e "$kill_sentinel" ]; then
    printf 'process guard signal reached a reused owner group\n'
    return 1
  fi
  if [ "$status" -eq 0 ]; then
    printf 'process guard signal accepted owner reuse during capture\n'
    return 1
  fi

  rm -f "$read_count_file"
  if (
    ml3_process_exists() {
      return 1
    }
    ml3_read_process_stat_fields() {
      local pid=$1
      local read_count=0

      [ "$pid" = "$fake_pid" ] || return 1
      if [ -s "$read_count_file" ]; then
        IFS= read -r read_count <"$read_count_file"
      fi
      read_count=$(( read_count + 1 ))
      printf '%s\n' "$read_count" >"$read_count_file"
      [ "$read_count" -gt 1 ] || return 1
      ML3_STAT_STATE=S
      ML3_STAT_PGID=$fake_pid
      ML3_STAT_SID=$fake_pid
      ML3_STAT_STARTTIME=222
      return 0
    }
    ml3_owned_process_exists "$fake_pid" "$fake_pid" "$fake_pid" 111
  ); then
    printf 'process guard existence scan accepted owner reuse\n'
    return 1
  fi

  return 0
}

read_exact_process_fields() {
  local pid=$1
  local stat=

  EXACT_STAT_STATE=
  EXACT_STAT_PGID=
  EXACT_STAT_SID=
  EXACT_STAT_STARTTIME=

  case "$pid" in
    ''|*[!0-9]*) return 1 ;;
  esac

  IFS= read -r -d '' stat 2>/dev/null <"/proc/$pid/stat" || [ -n "$stat" ] || return 1
  stat=${stat##*) }
  set -- $stat
  [ "$#" -ge 20 ] || return 1

  EXACT_STAT_STATE=$1
  EXACT_STAT_PGID=$3
  EXACT_STAT_SID=$4
  EXACT_STAT_STARTTIME=${20}
}

exact_wrapper_cmdline_matches() {
  local pid=$1
  local wrapper=$2
  local argument=
  local matches=0

  [ -r "/proc/$pid/cmdline" ] || return 1
  while IFS= read -r -d '' argument; do
    if [ "$argument" = "$wrapper" ]; then
      matches=$(( matches + 1 ))
    fi
  done <"/proc/$pid/cmdline"

  [ "$matches" -eq 1 ]
}

persist_independent_cleanup_identity() {
  local pid=$1
  local wrapper=$2
  local identity_file=$3
  local deadline_ms=$4
  local identity_tmp="${identity_file}.tmp.${BASHPID:-$$}"

  UNRECORDED_OWNER_PID=
  UNRECORDED_OWNER_PGID=
  UNRECORDED_OWNER_SID=
  UNRECORDED_OWNER_STARTTIME=
  UNRECORDED_OWNER_WRAPPER=
  while :; do
    if read_exact_process_fields "$pid" && \
      [ "$EXACT_STAT_STATE" != "Z" ] && \
      [ "$EXACT_STAT_PGID" = "$pid" ] && \
      [ "$EXACT_STAT_SID" = "$pid" ] && \
      exact_wrapper_cmdline_matches "$pid" "$wrapper"; then
      UNRECORDED_OWNER_PID=$pid
      UNRECORDED_OWNER_PGID=$EXACT_STAT_PGID
      UNRECORDED_OWNER_SID=$EXACT_STAT_SID
      UNRECORDED_OWNER_STARTTIME=$EXACT_STAT_STARTTIME
      UNRECORDED_OWNER_WRAPPER=$wrapper
      if ! printf '%s %s %s %s\n' \
        "$pid" \
        "$EXACT_STAT_PGID" \
        "$EXACT_STAT_SID" \
        "$EXACT_STAT_STARTTIME" 2>/dev/null >"$identity_tmp"; then
        rm -f "$identity_tmp"
        return 1
      fi
      if ! mv -f -- "$identity_tmp" "$identity_file" 2>/dev/null; then
        rm -f "$identity_tmp"
        return 1
      fi
      VERIFIED_OWNER_PID=$pid
      VERIFIED_OWNER_PGID=$EXACT_STAT_PGID
      VERIFIED_OWNER_SID=$EXACT_STAT_SID
      VERIFIED_OWNER_STARTTIME=$EXACT_STAT_STARTTIME
      VERIFIED_OWNER_WRAPPER=$wrapper
      return 0
    fi
    if [ ! -e "/proc/$pid" ] || [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
      return 1
    fi
    sleep 0.05
  done
}

known_isolated_owner_not_reused() {
  local pid=$1
  local wrapper=$2
  local expected_starttime=$3

  if read_exact_process_fields "$pid"; then
    if [ "$EXACT_STAT_PGID" != "$pid" ] || \
      [ "$EXACT_STAT_SID" != "$pid" ] || \
      [ "$EXACT_STAT_STARTTIME" != "$expected_starttime" ]; then
      return 1
    fi
    if [ "$EXACT_STAT_STATE" != "Z" ] && ! exact_wrapper_cmdline_matches "$pid" "$wrapper"; then
      return 1
    fi
    return 0
  fi

  [ ! -e "/proc/$pid" ]
}

cleanup_known_isolated_child() {
  local pid=$1
  local wrapper=$2
  local starttime=${3:-}
  local deadline_ms=

  case "$pid" in ''|*[!0-9]*) return 1 ;; esac
  if [ -n "$starttime" ]; then
    case "$starttime" in *[!0-9]*) return 1 ;; esac
  else
    deadline_ms=$(( $(ml3_now_ms) + 1000 ))
    while :; do
      if read_exact_process_fields "$pid"; then
        if [ "$EXACT_STAT_STATE" = "Z" ]; then
          wait "$pid" 2>/dev/null || true
          return 0
        fi
        if [ "$EXACT_STAT_PGID" = "$pid" ] && \
          [ "$EXACT_STAT_SID" = "$pid" ] && \
          exact_wrapper_cmdline_matches "$pid" "$wrapper"; then
          starttime=$EXACT_STAT_STARTTIME
          break
        fi
      elif [ ! -e "/proc/$pid" ]; then
        wait "$pid" 2>/dev/null || true
        return 1
      fi
      if [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
        return 1
      fi
      sleep 0.05
    done
  fi

  if ! known_isolated_owner_not_reused "$pid" "$wrapper" "$starttime"; then
    return 1
  fi
  if exact_wrapper_group_has_non_zombie_process "$pid" "$pid" "$starttime"; then
    if ! known_isolated_owner_not_reused "$pid" "$wrapper" "$starttime"; then
      return 1
    fi
    kill -s KILL -- "-$pid" 2>/dev/null || true
  fi
  wait "$pid" 2>/dev/null || true
  deadline_ms=$(( $(ml3_now_ms) + 1000 ))
  while exact_wrapper_group_has_non_zombie_process "$pid" "$pid" "$starttime"; do
    if [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
      return 1
    fi
    sleep 0.05
  done
  return 0
}

cleanup_after_identity_persist_failure() {
  local pid=$1
  local wrapper=$2

  if [ -z "$UNRECORDED_OWNER_PID" ] && \
    [ -z "$UNRECORDED_OWNER_PGID" ] && \
    [ -z "$UNRECORDED_OWNER_SID" ] && \
    [ -z "$UNRECORDED_OWNER_STARTTIME" ] && \
    [ -z "$UNRECORDED_OWNER_WRAPPER" ]; then
    cleanup_known_isolated_child "$pid" "$wrapper"
    return
  fi
  if [ "$UNRECORDED_OWNER_PID" != "$pid" ] || \
    [ "$UNRECORDED_OWNER_PGID" != "$pid" ] || \
    [ "$UNRECORDED_OWNER_SID" != "$pid" ] || \
    [ "$UNRECORDED_OWNER_WRAPPER" != "$wrapper" ]; then
    return 1
  fi
  case "$UNRECORDED_OWNER_STARTTIME" in ''|*[!0-9]*) return 1 ;; esac

  cleanup_known_isolated_child "$pid" "$wrapper" "$UNRECORDED_OWNER_STARTTIME"
}

exact_wrapper_group_has_non_zombie_process() {
  local expected_pgid=$1
  local expected_sid=$2
  local expected_starttime=$3
  local proc=
  local pid=
  local state=
  local pgid=
  local sid=

  for proc in /proc/[0-9]*; do
    pid=${proc##*/}
    if ! read_exact_process_fields "$pid"; then
      continue
    fi
    state=$EXACT_STAT_STATE
    pgid=$EXACT_STAT_PGID
    sid=$EXACT_STAT_SID
    if [ "$state" != "Z" ] && \
      [ "$pgid" = "$expected_pgid" ] && \
      [ "$sid" = "$expected_sid" ] && \
      [ "$EXACT_STAT_STARTTIME" -ge "$expected_starttime" ]; then
      return 0
    fi
  done

  return 1
}

assert_cached_identity_reuse_rejected() {
  local identity_file="$TEST_TMPDIR/reused.identity"
  local kill_sentinel="$TEST_TMPDIR/reused.kill"
  local fake_pid=$$

  printf '%s %s %s %s\n' "$fake_pid" "$fake_pid" "$fake_pid" 111 >"$identity_file"
  rm -f "$kill_sentinel"
  if (
    VERIFIED_OWNER_PID=$fake_pid
    VERIFIED_OWNER_PGID=$fake_pid
    VERIFIED_OWNER_SID=$fake_pid
    VERIFIED_OWNER_STARTTIME=111
    VERIFIED_OWNER_WRAPPER=/simulated-wrapper
    REUSE_SCAN_CALLS=0
    read_exact_process_fields() {
      EXACT_STAT_STATE=S
      EXACT_STAT_PGID=$fake_pid
      EXACT_STAT_SID=$fake_pid
      EXACT_STAT_STARTTIME=222
      return 0
    }
    exact_wrapper_group_has_non_zombie_process() {
      REUSE_SCAN_CALLS=$(( REUSE_SCAN_CALLS + 1 ))
      [ "$REUSE_SCAN_CALLS" -eq 1 ]
    }
    kill() {
      : >"$kill_sentinel"
      return 0
    }
    cleanup_exact_wrapper_group "$identity_file" /simulated-wrapper
  ); then
    printf 'cached identity reuse oracle accepted a changed owner starttime\n'
    return 1
  fi
  if [ -e "$kill_sentinel" ]; then
    printf 'cached identity reuse oracle signalled a reused process group\n'
    return 1
  fi

  return 0
}

assert_cache_miss_identity_reuse_rejected() {
  local identity_file="$TEST_TMPDIR/reused-cache-miss.identity"
  local kill_sentinel="$TEST_TMPDIR/reused-cache-miss.kill"
  local fake_pid=$$

  printf '%s %s %s %s\n' "$fake_pid" "$fake_pid" "$fake_pid" 111 >"$identity_file"
  rm -f "$kill_sentinel"
  if (
    VERIFIED_OWNER_PID=
    VERIFIED_OWNER_PGID=
    VERIFIED_OWNER_SID=
    VERIFIED_OWNER_STARTTIME=
    VERIFIED_OWNER_WRAPPER=
    REUSE_SCAN_CALLS=0
    read_exact_process_fields() {
      EXACT_STAT_STATE=S
      EXACT_STAT_PGID=$fake_pid
      EXACT_STAT_SID=$fake_pid
      EXACT_STAT_STARTTIME=222
      return 0
    }
    exact_wrapper_cmdline_matches() {
      return 0
    }
    exact_wrapper_group_has_non_zombie_process() {
      REUSE_SCAN_CALLS=$(( REUSE_SCAN_CALLS + 1 ))
      [ "$REUSE_SCAN_CALLS" -eq 1 ]
    }
    kill() {
      : >"$kill_sentinel"
      return 0
    }
    cleanup_exact_wrapper_group "$identity_file" /simulated-wrapper
  ); then
    printf 'cache-miss identity reuse oracle accepted a changed owner starttime\n'
    return 1
  fi
  if [ -e "$kill_sentinel" ]; then
    printf 'cache-miss identity reuse oracle signalled a reused process group\n'
    return 1
  fi

  return 0
}

read_recorded_wrapper_identity() {
  local identity_file=$1

  RECORDED_OWNER_PID=
  RECORDED_OWNER_PGID=
  RECORDED_OWNER_SID=
  RECORDED_OWNER_STARTTIME=
  if [ ! -s "$identity_file" ]; then
    return 1
  fi
  if ! read -r RECORDED_OWNER_PID RECORDED_OWNER_PGID RECORDED_OWNER_SID RECORDED_OWNER_STARTTIME <"$identity_file"; then
    return 1
  fi
  case "$RECORDED_OWNER_PID" in ''|*[!0-9]*) return 1 ;; esac
  case "$RECORDED_OWNER_PGID" in ''|*[!0-9]*) return 1 ;; esac
  case "$RECORDED_OWNER_SID" in ''|*[!0-9]*) return 1 ;; esac
  case "$RECORDED_OWNER_STARTTIME" in ''|*[!0-9]*) return 1 ;; esac
  [ "$RECORDED_OWNER_PID" = "$RECORDED_OWNER_PGID" ] && \
    [ "$RECORDED_OWNER_PID" = "$RECORDED_OWNER_SID" ]
}

load_safe_recorded_wrapper_identity() {
  local identity_file=$1
  local wrapper=$2

  SAFE_OWNER_STARTTIME=
  if ! read_recorded_wrapper_identity "$identity_file"; then
    return 1
  fi
  if [ "$VERIFIED_OWNER_PID" = "$RECORDED_OWNER_PID" ] && \
    [ "$VERIFIED_OWNER_PGID" = "$RECORDED_OWNER_PGID" ] && \
    [ "$VERIFIED_OWNER_SID" = "$RECORDED_OWNER_SID" ] && \
    [ "$VERIFIED_OWNER_WRAPPER" = "$wrapper" ]; then
    case "$VERIFIED_OWNER_STARTTIME" in ''|*[!0-9]*) return 1 ;; esac
    if [ -e "/proc/$RECORDED_OWNER_PID" ]; then
      if read_exact_process_fields "$RECORDED_OWNER_PID"; then
        if [ "$EXACT_STAT_PGID" != "$VERIFIED_OWNER_PGID" ] || \
          [ "$EXACT_STAT_SID" != "$VERIFIED_OWNER_SID" ] || \
          [ "$EXACT_STAT_STARTTIME" != "$VERIFIED_OWNER_STARTTIME" ]; then
          return 1
        fi
      elif [ -e "/proc/$RECORDED_OWNER_PID" ]; then
        return 1
      fi
    fi
    SAFE_OWNER_STARTTIME=$VERIFIED_OWNER_STARTTIME
    return 0
  fi

  if ! read_exact_process_fields "$RECORDED_OWNER_PID" || \
    [ "$EXACT_STAT_STATE" = "Z" ] || \
    [ "$EXACT_STAT_PGID" != "$RECORDED_OWNER_PGID" ] || \
    [ "$EXACT_STAT_SID" != "$RECORDED_OWNER_SID" ] || \
    [ "$EXACT_STAT_STARTTIME" != "$RECORDED_OWNER_STARTTIME" ] || \
    ! exact_wrapper_cmdline_matches "$RECORDED_OWNER_PID" "$wrapper"; then
    return 1
  fi
  VERIFIED_OWNER_PID=$RECORDED_OWNER_PID
  VERIFIED_OWNER_PGID=$RECORDED_OWNER_PGID
  VERIFIED_OWNER_SID=$RECORDED_OWNER_SID
  VERIFIED_OWNER_STARTTIME=$EXACT_STAT_STARTTIME
  VERIFIED_OWNER_WRAPPER=$wrapper
  SAFE_OWNER_STARTTIME=$VERIFIED_OWNER_STARTTIME
  return 0
}

cleanup_exact_wrapper_group() {
  local identity_file=$1
  local wrapper=$2
  local deadline_ms=

  if [ ! -s "$identity_file" ]; then
    return 0
  fi
  if ! load_safe_recorded_wrapper_identity "$identity_file" "$wrapper"; then
    return 1
  fi
  if ! exact_wrapper_group_has_non_zombie_process \
    "$RECORDED_OWNER_PGID" \
    "$RECORDED_OWNER_SID" \
    "$SAFE_OWNER_STARTTIME"; then
    return 0
  fi
  if ! load_safe_recorded_wrapper_identity "$identity_file" "$wrapper"; then
    return 1
  fi

  kill -s KILL -- "-$RECORDED_OWNER_PGID" 2>/dev/null || true
  deadline_ms=$(( $(ml3_now_ms) + 1000 ))
  while exact_wrapper_group_has_non_zombie_process \
    "$RECORDED_OWNER_PGID" \
    "$RECORDED_OWNER_SID" \
    "$SAFE_OWNER_STARTTIME"; do
    if [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
      return 1
    fi
    sleep 0.05
  done
  return 0
}

assert_no_exact_wrapper_survivor() {
  local name=$1
  local identity_file=$2
  local wrapper=$3

  if ! load_safe_recorded_wrapper_identity "$identity_file" "$wrapper"; then
    printf '%s runner status regression could not validate recorded wrapper ownership\n' "$name"
    return 1
  fi
  if ! exact_wrapper_group_has_non_zombie_process \
    "$RECORDED_OWNER_PGID" \
    "$RECORDED_OWNER_SID" \
    "$SAFE_OWNER_STARTTIME"; then
    return 0
  fi

  printf '%s runner status regression independently found live compiler wrapper group %s after reported drain\n' \
    "$name" "$RECORDED_OWNER_PGID"
  if ! cleanup_exact_wrapper_group "$identity_file" "$wrapper"; then
    printf '%s runner status regression could not clean exact compiler wrapper group %s\n' \
      "$name" "$RECORDED_OWNER_PGID"
  fi
  return 1
}

assert_recorded_starttime_is_exact() {
  local name=$1
  local expected_pid=$2
  local identity_file=$3
  local wrapper=$4

  if ! read_recorded_wrapper_identity "$identity_file"; then
    printf '%s runner status regression could not read compiler identity\n' "$name"
    return 1
  fi
  if [ "$RECORDED_OWNER_PID" != "$expected_pid" ]; then
    printf '%s runner status regression compiler identity pid mismatch: expected %s, got %s\n' \
      "$name" "$expected_pid" "$RECORDED_OWNER_PID"
    return 1
  fi
  if ! read_exact_process_fields "$RECORDED_OWNER_PID" || \
    [ "$EXACT_STAT_STATE" = "Z" ] || \
    [ "$EXACT_STAT_PGID" != "$RECORDED_OWNER_PGID" ] || \
    [ "$EXACT_STAT_SID" != "$RECORDED_OWNER_SID" ] || \
    ! exact_wrapper_cmdline_matches "$RECORDED_OWNER_PID" "$wrapper"; then
    printf '%s runner status regression could not independently verify compiler identity %s\n' \
      "$name" "$RECORDED_OWNER_PID"
    return 1
  fi
  VERIFIED_OWNER_PID=$RECORDED_OWNER_PID
  VERIFIED_OWNER_PGID=$RECORDED_OWNER_PGID
  VERIFIED_OWNER_SID=$RECORDED_OWNER_SID
  VERIFIED_OWNER_STARTTIME=$EXACT_STAT_STARTTIME
  VERIFIED_OWNER_WRAPPER=$wrapper
  if [ "$RECORDED_OWNER_STARTTIME" != "$EXACT_STAT_STARTTIME" ]; then
    printf '%s runner status regression starttime mismatch for compiler %s: recorded %s, exact %s\n' \
      "$name" "$RECORDED_OWNER_PID" "$RECORDED_OWNER_STARTTIME" "$EXACT_STAT_STARTTIME"
    return 1
  fi

  return 0
}

run_special_comm_parser_case() {
  local mode=${1:-normal}
  local fixture_pid=
  local deadline_ms=
  local identity_file=$SPECIAL_COMM_IDENTITY_FILE
  local raw_stat=
  local recorded_identity=
  local production_identity=
  local production_state=
  local production_pgid=
  local production_sid=
  local production_starttime=

cat >"$SPECIAL_COMM_SOURCE" <<'EOF'
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

int main(void)
{
  if (prctl(PR_SET_NAME, "a) b) \nc) d", 0, 0, 0) != 0) {
    return 1;
  }
  (void)signal(SIGHUP, SIG_IGN);
  (void)signal(SIGINT, SIG_IGN);
  (void)signal(SIGTERM, SIG_IGN);
  for (;;) {
    (void)pause();
  }
}
EOF
  if ! "${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wpedantic \
    "$SPECIAL_COMM_SOURCE" -o "$SPECIAL_COMM_BINARY"; then
    printf 'special comm parser oracle could not build fixture\n'
    return 1
  fi

  if [ "$mode" = "identity_write_failure" ]; then
    identity_file="$TEST_TMPDIR/missing-identity-directory/special-comm.identity"
    rm -rf "$(dirname -- "$identity_file")"
  else
    : >"$identity_file"
  fi
  setsid "$SPECIAL_COMM_BINARY" &
  fixture_pid=$!
  if [ "$mode" = "pre_identity_failure" ]; then
    if ! cleanup_known_isolated_child "$fixture_pid" "$SPECIAL_COMM_BINARY"; then
      printf 'special comm pre-identity cleanup oracle failed\n'
      return 1
    fi
    return 0
  fi
  deadline_ms=$(( $(ml3_now_ms) + 1000 ))
  if [ "$mode" = "identity_write_failure" ]; then
    if persist_independent_cleanup_identity \
      "$fixture_pid" \
      "$SPECIAL_COMM_BINARY" \
      "$identity_file" \
      "$deadline_ms"; then
      cleanup_known_isolated_child "$fixture_pid" "$SPECIAL_COMM_BINARY" || true
      printf 'special comm identity-write oracle accepted a failed identity write\n'
      return 1
    fi
    if ! cleanup_after_identity_persist_failure "$fixture_pid" "$SPECIAL_COMM_BINARY"; then
      printf 'special comm identity-write oracle failed to clean known child\n'
      return 1
    fi
    return 0
  fi
  if ! persist_independent_cleanup_identity \
    "$fixture_pid" \
    "$SPECIAL_COMM_BINARY" \
    "$identity_file" \
    "$deadline_ms"; then
    if ! cleanup_after_identity_persist_failure "$fixture_pid" "$SPECIAL_COMM_BINARY"; then
      printf 'special comm parser oracle could not clean unrecorded fixture ownership\n'
      return 1
    fi
    printf 'special comm parser oracle could not verify isolated fixture ownership\n'
    return 1
  fi

  while :; do
    raw_stat=
    IFS= read -r -d '' raw_stat 2>/dev/null <"/proc/$fixture_pid/stat" || [ -n "$raw_stat" ] || true
    case "$raw_stat" in
      *$'(a) b) \nc) d)'*) break ;;
    esac
    if [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
      printf 'special comm parser oracle fixture lacks embedded newline and repeated delimiters\n'
      return 1
    fi
    sleep 0.05
  done
  if [ ! -e "/proc/$fixture_pid" ]; then
    printf 'special comm parser oracle fixture exited early\n'
    return 1
  fi
  if ! production_identity=$(ml3_capture_process_identity "$fixture_pid"); then
    printf 'special comm parser oracle production reader rejected full stat record\n'
    return 1
  fi
  if ! IFS= read -r recorded_identity <"$SPECIAL_COMM_IDENTITY_FILE" || \
    [ "$production_identity" != "$recorded_identity" ]; then
    printf 'special comm parser oracle production identity mismatched exact identity\n'
    return 1
  fi
  if ! assert_recorded_starttime_is_exact \
    "special_comm" \
    "$fixture_pid" \
    "$SPECIAL_COMM_IDENTITY_FILE" \
    "$SPECIAL_COMM_BINARY"; then
    printf 'special comm parser oracle could not verify fixture identity\n'
    return 1
  fi
  if ! ml3_read_process_stat_fields "$fixture_pid"; then
    printf 'special comm parser oracle production reader failed\n'
    return 1
  fi
  production_state=$ML3_STAT_STATE
  production_pgid=$ML3_STAT_PGID
  production_sid=$ML3_STAT_SID
  production_starttime=$ML3_STAT_STARTTIME
  if ! read_exact_process_fields "$fixture_pid" || \
    [ "$production_state" != "$EXACT_STAT_STATE" ] || \
    [ "$production_pgid" != "$EXACT_STAT_PGID" ] || \
    [ "$production_sid" != "$EXACT_STAT_SID" ] || \
    [ "$production_starttime" != "$EXACT_STAT_STARTTIME" ]; then
    printf 'special comm parser oracle readers disagreed\n'
    return 1
  fi
  if ! cleanup_exact_wrapper_group "$SPECIAL_COMM_IDENTITY_FILE" "$SPECIAL_COMM_BINARY"; then
    printf 'special comm parser oracle cleanup failed\n'
    return 1
  fi
  wait "$fixture_pid" 2>/dev/null || true
  : >"$SPECIAL_COMM_IDENTITY_FILE"
  return 0
}

run_orphaned_descendant_oracle_case() {
  local mode=${1:-normal}
  local leader_pid=
  local descendant_pid=
  local recorded_pid=
  local recorded_pgid=
  local recorded_sid=
  local recorded_starttime=
  local failed_identity_starttime=
  local identity_file=$ORPHAN_IDENTITY_FILE
  local oracle_log="$TEST_TMPDIR/orphan-oracle.log"
  local status=0

  rm -f \
    "$ORPHAN_LEADER_PID_FILE" \
    "$ORPHAN_DESCENDANT_PID_FILE" \
    "$ORPHAN_IDENTITY_FILE"
  if [ "$mode" = "identity_write_failure_after_leader_exit" ]; then
    identity_file="$TEST_TMPDIR/missing-orphan-identity-directory/orphan.identity"
    rm -rf "$(dirname -- "$identity_file")"
  fi
cat >"$ORPHAN_WRAPPER" <<'EOF'
#!/usr/bin/env bash
set -eu
printf '%s\n' "$$" >"$ORPHAN_LEADER_PID_FILE"
bash -c 'trap "" HUP INT TERM; while :; do sleep 1; done' &
printf '%s\n' "$!" >"$ORPHAN_DESCENDANT_PID_FILE"
trap '' HUP INT TERM
while :; do sleep 1; done
EOF
  chmod +x "$ORPHAN_WRAPPER"
  export ORPHAN_LEADER_PID_FILE ORPHAN_DESCENDANT_PID_FILE

  setsid "$ORPHAN_WRAPPER" &
  leader_pid=$!
  if ! persist_independent_cleanup_identity \
    "$leader_pid" \
    "$ORPHAN_WRAPPER" \
    "$identity_file" \
    $(( $(ml3_now_ms) + 1000 )); then
    failed_identity_starttime=$UNRECORDED_OWNER_STARTTIME
    if [ "$mode" = "identity_write_failure_after_leader_exit" ]; then
      if ! wait_for_path "$ORPHAN_DESCENDANT_PID_FILE" $(( $(ml3_now_ms) + 1000 )); then
        printf 'orphaned write-failure oracle did not start descendant\n'
        cleanup_after_identity_persist_failure "$leader_pid" "$ORPHAN_WRAPPER" || true
        return 1
      fi
      descendant_pid=$(cat "$ORPHAN_DESCENDANT_PID_FILE")
      kill -s KILL "$leader_pid" 2>/dev/null || true
      wait "$leader_pid" 2>/dev/null || true
      cleanup_after_identity_persist_failure "$leader_pid" "$ORPHAN_WRAPPER" || true
      if exact_wrapper_group_has_non_zombie_process "$leader_pid" "$leader_pid" "$failed_identity_starttime"; then
        printf 'orphaned write-failure oracle left descendant %s alive after leader exit\n' "$descendant_pid"
        kill -s KILL -- "-$leader_pid" 2>/dev/null || true
        return 1
      fi
      return 0
    fi
    if ! cleanup_after_identity_persist_failure "$leader_pid" "$ORPHAN_WRAPPER"; then
      printf 'orphaned descendant oracle could not clean unrecorded fixture ownership\n'
      return 1
    fi
    printf 'orphaned descendant oracle could not persist immediate cleanup identity\n'
    return 1
  fi
  if [ "$mode" = "pre_readiness_failure" ]; then
    if ! cleanup_exact_wrapper_group "$ORPHAN_IDENTITY_FILE" "$ORPHAN_WRAPPER"; then
      printf 'orphaned descendant pre-readiness cleanup oracle failed\n'
      return 1
    fi
    wait "$leader_pid" 2>/dev/null || true
    : >"$ORPHAN_IDENTITY_FILE"
    return 0
  fi
  if ! wait_for_path "$ORPHAN_LEADER_PID_FILE" $(( $(ml3_now_ms) + 1000 )) || \
    ! wait_for_path "$ORPHAN_DESCENDANT_PID_FILE" $(( $(ml3_now_ms) + 1000 )); then
    printf 'orphaned descendant oracle did not start fixture processes\n'
    return 1
  fi
  if [ "$(cat "$ORPHAN_LEADER_PID_FILE")" != "$leader_pid" ]; then
    printf 'orphaned descendant oracle leader pid mismatch\n'
    return 1
  fi
  descendant_pid=$(cat "$ORPHAN_DESCENDANT_PID_FILE")
  if ! read_recorded_wrapper_identity "$ORPHAN_IDENTITY_FILE"; then
    printf 'orphaned descendant oracle could not read persisted leader identity\n'
    return 1
  fi
  recorded_pid=$RECORDED_OWNER_PID
  recorded_pgid=$RECORDED_OWNER_PGID
  recorded_sid=$RECORDED_OWNER_SID
  recorded_starttime=$RECORDED_OWNER_STARTTIME

  kill -s KILL "$leader_pid" 2>/dev/null || true
  wait "$leader_pid" 2>/dev/null || true
  if ! read_exact_process_fields "$descendant_pid" || \
    [ "$EXACT_STAT_STATE" = "Z" ] || \
    [ "$EXACT_STAT_PGID" != "$recorded_pgid" ] || \
    [ "$EXACT_STAT_SID" != "$recorded_sid" ] || \
    [ "$EXACT_STAT_STARTTIME" -lt "$recorded_starttime" ]; then
    printf 'orphaned descendant oracle did not retain exact live descendant ownership\n'
    cleanup_exact_wrapper_group "$ORPHAN_IDENTITY_FILE" "$ORPHAN_WRAPPER" || true
    return 1
  fi

  if assert_no_exact_wrapper_survivor \
    "orphaned_descendant" \
    "$ORPHAN_IDENTITY_FILE" \
    "$ORPHAN_WRAPPER" >"$oracle_log"; then
    printf 'orphaned descendant oracle false-passed with live group member %s\n' "$descendant_pid"
    status=1
    cleanup_exact_wrapper_group "$ORPHAN_IDENTITY_FILE" "$ORPHAN_WRAPPER" || true
  elif ! command grep -q 'independently found live compiler wrapper group' "$oracle_log"; then
    printf 'orphaned descendant oracle failed without detecting the live recorded group\n'
    status=1
  fi
  if exact_wrapper_group_has_non_zombie_process "$recorded_pgid" "$recorded_sid" "$recorded_starttime"; then
    printf 'orphaned descendant oracle cleanup failed\n'
    return 1
  fi
  : >"$ORPHAN_IDENTITY_FILE"
  return "$status"
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
    if ! ml3_drain_owned_process_identity_file "$CC_DRAIN_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
      status=1
    fi
  fi
  if ! cleanup_exact_wrapper_group "$CC_IDENTITY_FILE" "$CC_WRAPPER"; then
    status=1
  fi
  if ! cleanup_exact_wrapper_group "$ORPHAN_IDENTITY_FILE" "$ORPHAN_WRAPPER"; then
    status=1
  fi
  if ! cleanup_exact_wrapper_group "$SPECIAL_COMM_IDENTITY_FILE" "$SPECIAL_COMM_BINARY"; then
    status=1
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

  rm -f \
    "$GREP_SENTINEL" \
    "$CC_SENTINEL" \
    "$CC_PID_FILE" \
    "$CC_IDENTITY_FILE" \
    "$CC_DRAIN_IDENTITY_FILE" \
    "$RUNNER_IDENTITY_FILE"

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
    if ! ml3_store_process_identity_file "$CC_DRAIN_IDENTITY_FILE" "$cc_pid"; then
      printf '%s runner status regression could not capture compiler drain identity\n' "$name"
      cat "$runner_log"
      return 1
    fi
  fi
  if ! assert_recorded_starttime_is_exact "$name" "$cc_pid" "$CC_IDENTITY_FILE" "$CC_WRAPPER"; then
    return 1
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

  if ! ml3_drain_owned_process_identity_file "$CC_DRAIN_IDENTITY_FILE" $(( $(ml3_now_ms) + 1000 )); then
    printf '%s runner status regression left compiler wrapper alive\n' "$name"
    return 1
  fi
  if ! assert_no_exact_wrapper_survivor "$name" "$CC_IDENTITY_FILE" "$CC_WRAPPER"; then
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

if ! assert_guard_scan_structure; then
  exit 1
fi
if ! assert_guard_scan_mutations_rejected; then
  exit 1
fi
if ! assert_process_guard_owner_reuse_races_rejected; then
  exit 1
fi
if ! assert_cached_identity_reuse_rejected; then
  exit 1
fi
if ! assert_cache_miss_identity_reuse_rejected; then
  exit 1
fi
if ! run_special_comm_parser_case pre_identity_failure; then
  exit 1
fi
if ! run_special_comm_parser_case identity_write_failure; then
  exit 1
fi
if ! run_special_comm_parser_case; then
  exit 1
fi
if ! run_orphaned_descendant_oracle_case pre_readiness_failure; then
  exit 1
fi
if ! run_orphaned_descendant_oracle_case identity_write_failure_after_leader_exit; then
  exit 1
fi
if ! run_orphaned_descendant_oracle_case; then
  exit 1
fi
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
