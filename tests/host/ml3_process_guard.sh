#!/usr/bin/env bash

ml3_now_ms() {
  date +%s%3N
}

ML3_STAT_STATE=
ML3_STAT_PGID=
ML3_STAT_SID=
ML3_STAT_STARTTIME=

ml3_read_process_stat_fields() {
  local pid=$1
  local stat=

  ML3_STAT_STATE=
  ML3_STAT_PGID=
  ML3_STAT_SID=
  ML3_STAT_STARTTIME=

  IFS= read -r -d '' stat 2>/dev/null <"/proc/$pid/stat" || [ -n "$stat" ] || return 1
  stat=${stat##*) }
  set -- $stat
  if [ "$#" -lt 20 ]; then
    return 1
  fi

  ML3_STAT_STATE=$1
  ML3_STAT_PGID=$3
  ML3_STAT_SID=$4
  ML3_STAT_STARTTIME=${20}
}

ml3_process_stat_fields() {
  local pid=$1

  if ! ml3_read_process_stat_fields "$pid"; then
    return 1
  fi
  printf '%s %s %s %s\n' \
    "$ML3_STAT_STATE" \
    "$ML3_STAT_PGID" \
    "$ML3_STAT_SID" \
    "$ML3_STAT_STARTTIME"
}

ml3_capture_process_stat_fields() {
  local pid=$1
  ml3_process_stat_fields "$pid"
}

ml3_capture_process_identity() {
  local pid=$1
  local state=
  local pgid=
  local sid=
  local starttime=

  if ! ml3_read_process_stat_fields "$pid"; then
    return 1
  fi
  state=$ML3_STAT_STATE
  pgid=$ML3_STAT_PGID
  sid=$ML3_STAT_SID
  starttime=$ML3_STAT_STARTTIME

  printf '%s %s %s %s\n' "$pid" "$pgid" "$sid" "$starttime"
}

ml3_capture_process_identity_with_deadline() {
  local pid=$1
  local deadline_ms=$2
  local identity=

  while :; do
    if identity=$(ml3_capture_process_identity "$pid"); then
      printf '%s\n' "$identity"
      return 0
    fi
    if [ ! -e "/proc/$pid" ]; then
      return 1
    fi
    if [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
      return 1
    fi
    sleep 0.05
  done
}

ml3_wait_for_owned_process_identity_file() {
  local identity_file=$1
  local deadline_ms=$2
  local pid=
  local pgid=
  local sid=
  local starttime=

  if ! read -r pid pgid sid starttime <"$identity_file"; then
    return 0
  fi

  while ml3_process_tree_alive "$pid" "$pgid" "$sid" "$starttime"; do
    if [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
      return 1
    fi
    sleep 0.05
  done
  return 0
}

ml3_process_identity_matches() {
  local pid=$1
  local expected_pgid=$2
  local expected_sid=$3
  local expected_starttime=$4
  local state=
  local pgid=
  local sid=
  local starttime=

  if ! ml3_read_process_stat_fields "$pid"; then
    return 1
  fi
  state=$ML3_STAT_STATE
  pgid=$ML3_STAT_PGID
  sid=$ML3_STAT_SID
  starttime=$ML3_STAT_STARTTIME

  [ "$state" != "Z" ] || return 1
  [ "$pgid" = "$expected_pgid" ] &&
    [ "$sid" = "$expected_sid" ] &&
    [ "$starttime" = "$expected_starttime" ]
}

ml3_process_identity_owned() {
  local pid=$1
  local expected_pgid=$2
  local expected_sid=$3
  local expected_starttime=$4
  local state=
  local pgid=
  local sid=
  local starttime=

  if ! ml3_read_process_stat_fields "$pid"; then
    return 1
  fi
  state=$ML3_STAT_STATE
  pgid=$ML3_STAT_PGID
  sid=$ML3_STAT_SID
  starttime=$ML3_STAT_STARTTIME

  [ "$pgid" = "$expected_pgid" ] &&
    [ "$sid" = "$expected_sid" ] &&
    [ "$starttime" = "$expected_starttime" ]
}

ml3_is_isolated_group_owner() {
  local pid=$1
  local expected_pgid=$2
  local expected_sid=$3

  [ "$pid" = "$expected_pgid" ] && [ "$pid" = "$expected_sid" ]
}

ml3_process_exists() {
  local pid=$1

  [ -e "/proc/$pid" ]
}

ml3_expected_owner_identity_safe() {
  local owner_pid=$1
  local expected_pgid=$2
  local expected_sid=$3
  local expected_starttime=$4

  if ml3_read_process_stat_fields "$owner_pid"; then
    [ "$ML3_STAT_PGID" = "$expected_pgid" ] && \
      [ "$ML3_STAT_SID" = "$expected_sid" ] && \
      [ "$ML3_STAT_STARTTIME" = "$expected_starttime" ]
    return
  fi

  ! ml3_process_exists "$owner_pid"
}

ml3_capture_owned_pids() {
  local owner_pid=$1
  local expected_pgid=$2
  local expected_sid=$3
  local expected_starttime=$4
  local pid=
  local state=
  local pgid=
  local sid=
  local starttime=
  local owned_pids=

  if [ -z "$owner_pid" ] || [ -z "$expected_pgid" ] || [ -z "$expected_sid" ] || [ -z "$expected_starttime" ]; then
    return 1
  fi

  if ! ml3_is_isolated_group_owner "$owner_pid" "$expected_pgid" "$expected_sid"; then
    return 1
  fi

  if ! ml3_expected_owner_identity_safe "$owner_pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 1
  fi

  for proc in /proc/[0-9]*; do
    pid=${proc##*/}

    if ! ml3_read_process_stat_fields "$pid"; then
      continue
    fi
    state=$ML3_STAT_STATE
    pgid=$ML3_STAT_PGID
    sid=$ML3_STAT_SID
    starttime=$ML3_STAT_STARTTIME

    if [ "$state" = "Z" ]; then
      continue
    fi

    if [ -n "$expected_starttime" ] && [ "$starttime" -lt "$expected_starttime" ]; then
      continue
    fi

    if [ "$pgid" = "$expected_pgid" ] && [ "$sid" = "$expected_sid" ]; then
      owned_pids="${owned_pids}${pid} ${pgid} ${sid} ${starttime}"$'\n'
    fi
  done

  if ! ml3_expected_owner_identity_safe "$owner_pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 1
  fi

  printf '%s' "$owned_pids"
}

ml3_owned_process_exists() {
  local owner_pid=$1
  local expected_pgid=$2
  local expected_sid=$3
  local expected_starttime=$4
  local pid=
  local state=
  local pgid=
  local sid=
  local starttime=
  local owned_process_found=0

  if [ -z "$owner_pid" ] || [ -z "$expected_pgid" ] || [ -z "$expected_sid" ] || [ -z "$expected_starttime" ]; then
    return 1
  fi

  if ! ml3_is_isolated_group_owner "$owner_pid" "$expected_pgid" "$expected_sid"; then
    return 1
  fi

  if ! ml3_expected_owner_identity_safe "$owner_pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 1
  fi

  for proc in /proc/[0-9]*; do
    pid=${proc##*/}

    if ! ml3_read_process_stat_fields "$pid"; then
      continue
    fi
    state=$ML3_STAT_STATE
    pgid=$ML3_STAT_PGID
    sid=$ML3_STAT_SID
    starttime=$ML3_STAT_STARTTIME

    if [ "$state" = "Z" ]; then
      continue
    fi

    if [ -n "$expected_starttime" ] && [ "$starttime" -lt "$expected_starttime" ]; then
      continue
    fi

    if [ "$pgid" = "$expected_pgid" ] && [ "$sid" = "$expected_sid" ]; then
      owned_process_found=1
    fi
  done

  if ! ml3_expected_owner_identity_safe "$owner_pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 1
  fi

  [ "$owned_process_found" -eq 1 ]
}

ml3_process_tree_alive() {
  local pid=$1
  local expected_pgid=$2
  local expected_sid=$3
  local expected_starttime=$4

  if ml3_process_identity_matches "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 0
  fi

  if ml3_owned_process_exists "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 0
  fi

  return 1
}

ml3_validate_owned_process() {
  local pid=$1
  local expected_pgid=$2
  local expected_sid=$3
  local expected_starttime=$4
  local state=
  local pgid=
  local sid=
  local starttime=

  if [ -z "$pid" ] || [ -z "$expected_pgid" ] || [ -z "$expected_sid" ] || [ -z "$expected_starttime" ]; then
    return 1
  fi

  if ml3_process_identity_owned "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 0
  fi

  if ml3_read_process_stat_fields "$pid"; then
    state=$ML3_STAT_STATE
    pgid=$ML3_STAT_PGID
    sid=$ML3_STAT_SID
    starttime=$ML3_STAT_STARTTIME
    [ "$state" != "Z" ] && return 1

    if ml3_owned_process_exists "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
      return 0
    fi
    return 0
  fi

  if ml3_owned_process_exists "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 0
  fi

  return 1
}

ml3_signal_process_list() {
  local signal=$1
  local owner_pid=$2
  local expected_pgid=$3
  local expected_sid=$4
  local expected_starttime=$5
  local owned_pids=
  local pid=
  local pgid=
  local sid=
  local starttime=
  local state=
  local current_pgid=
  local current_sid=
  local current_starttime=

  if ! owned_pids=$(ml3_capture_owned_pids "$owner_pid" "$expected_pgid" "$expected_sid" "$expected_starttime" 2>/dev/null); then
    return 1
  fi

  if ! ml3_expected_owner_identity_safe "$owner_pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 1
  fi

  while read -r pid pgid sid starttime; do
    [ -n "$pid" ] || continue
    [ "$pgid" = "$expected_pgid" ] || continue
    [ "$sid" = "$expected_sid" ] || continue
    [ -n "$expected_starttime" ] && [ "$starttime" -lt "$expected_starttime" ] && continue

    if ! ml3_read_process_stat_fields "$pid"; then
      continue
    fi
    state=$ML3_STAT_STATE
    current_pgid=$ML3_STAT_PGID
    current_sid=$ML3_STAT_SID
    current_starttime=$ML3_STAT_STARTTIME
    [ "$state" != "Z" ] || continue
    [ "$current_pgid" = "$pgid" ] || continue
    [ "$current_sid" = "$sid" ] || continue
    [ "$current_starttime" = "$starttime" ] || continue

    if ! ml3_expected_owner_identity_safe "$owner_pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
      return 1
    fi

    kill -s "$signal" "$pid" 2>/dev/null || true
  done <<EOF
$owned_pids
EOF

  if ml3_process_identity_matches "$owner_pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    kill -s "$signal" "$owner_pid" 2>/dev/null || true
  fi
  return 0
}

ml3_signal_owned_process_identity_file() {
  local signal=$1
  local identity_file=$2
  local pid=
  local pgid=
  local sid=
  local starttime=

  if ! read -r pid pgid sid starttime <"$identity_file"; then
    return 1
  fi

  if ! ml3_validate_owned_process "$pid" "$pgid" "$sid" "$starttime"; then
    return 1
  fi

  ml3_signal_process_list "$signal" "$pid" "$pgid" "$sid" "$starttime"
}

ml3_drain_direct_child_pid() {
  local pid=$1
  local term_deadline_ms=$2
  local signal=${3:-TERM}
  local kill_deadline_ms=
  local state=
  local pgid=
  local sid=
  local starttime=
  local now_ms=0
  local status=0

  if [ -z "$pid" ]; then
    return 1
  fi

  while :; do
    if [ ! -e "/proc/$pid" ]; then
      wait "$pid" 2>/dev/null || status=$?
      return "$status"
    fi

    if ml3_read_process_stat_fields "$pid"; then
      state=$ML3_STAT_STATE
      pgid=$ML3_STAT_PGID
      sid=$ML3_STAT_SID
      starttime=$ML3_STAT_STARTTIME
      if [ "$state" = "Z" ]; then
        wait "$pid" 2>/dev/null || status=$?
        return "$status"
      fi
    fi

    now_ms=$(ml3_now_ms)
    if [ -z "$kill_deadline_ms" ] && [ "$now_ms" -ge "$term_deadline_ms" ]; then
      kill_deadline_ms=$(( now_ms + 500 ))
      kill -s "$signal" "$pid" 2>/dev/null || true
    elif [ -n "$kill_deadline_ms" ] && [ "$now_ms" -ge "$kill_deadline_ms" ]; then
      kill -s KILL "$pid" 2>/dev/null || true
      kill_deadline_ms=$(( now_ms + 500 ))
      while :; do
        if [ ! -e "/proc/$pid" ]; then
          wait "$pid" 2>/dev/null || status=$?
          return "$status"
        fi
        if ml3_read_process_stat_fields "$pid"; then
          state=$ML3_STAT_STATE
          pgid=$ML3_STAT_PGID
          sid=$ML3_STAT_SID
          starttime=$ML3_STAT_STARTTIME
          if [ "$state" = "Z" ]; then
            wait "$pid" 2>/dev/null || status=$?
            return "$status"
          fi
        fi
        if [ "$(ml3_now_ms)" -ge "$kill_deadline_ms" ]; then
          return 1
        fi
        sleep 0.05
      done
    fi

    sleep 0.05
  done
}

ml3_drain_owned_process_tree() {
  local pid=$1
  local expected_pgid=$2
  local expected_sid=$3
  local expected_starttime=$4
  local term_deadline_ms=$5
  local signal=${6:-}
  local kill_deadline_ms=
  local now_ms=0

  if [ -z "$pid" ] || [ -z "$expected_pgid" ] || [ -z "$expected_sid" ] || [ -z "$expected_starttime" ]; then
    return 1
  fi

  if ! ml3_process_tree_alive "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    wait "$pid" 2>/dev/null || true
    return 0
  fi

  if ! ml3_validate_owned_process "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; then
    return 1
  fi

  if [ -z "$signal" ]; then
    signal=TERM
  fi

  while ml3_process_tree_alive "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"; do
    ml3_signal_process_list "$signal" "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"
    now_ms=$(ml3_now_ms)

    if [ -z "$kill_deadline_ms" ] && [ "$now_ms" -ge "$term_deadline_ms" ]; then
      kill_deadline_ms=$(( now_ms + 500 ))
      ml3_signal_process_list KILL "$pid" "$expected_pgid" "$expected_sid" "$expected_starttime"
    fi

    if [ -n "$kill_deadline_ms" ] && [ "$now_ms" -ge "$kill_deadline_ms" ]; then
      return 1
    fi

    sleep 0.05
  done

  wait "$pid" 2>/dev/null || true
  return 0
}

ml3_read_owned_process_identity_file() {
  local identity_file=$1
  local pid=
  local pgid=
  local sid=
  local starttime=

  if [ ! -f "$identity_file" ]; then
    return 1
  fi

  if ! read -r pid pgid sid starttime <"$identity_file"; then
    return 1
  fi

  printf '%s %s %s %s\n' "$pid" "$pgid" "$sid" "$starttime"
}

ml3_store_process_identity_file() {
  local identity_file=$1
  local pid=$2

  ml3_capture_process_identity "$pid" >"$identity_file"
}

ml3_drain_owned_process_identity_file() {
  local identity_file=$1
  local term_deadline_ms=$2
  local identity_dir=
  local pid=
  local pgid=
  local sid=
  local starttime=

  if ! ml3_read_owned_process_identity_file "$identity_file" >/dev/null; then
    identity_dir=$(dirname -- "$identity_file")
    if [ -d "$identity_dir" ]; then
      : >"$identity_file"
    fi
    return 0
  fi

  read -r pid pgid sid starttime <"$identity_file"

  if ! ml3_drain_owned_process_tree "$pid" "$pgid" "$sid" "$starttime" "$term_deadline_ms"; then
    return 1
  fi

  : >"$identity_file"
  return 0
}
