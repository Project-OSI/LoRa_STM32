#!/usr/bin/env bash

EXACT_STAT_STATE=
EXACT_STAT_PGID=
EXACT_STAT_SID=
EXACT_STAT_STARTTIME=

read_exact_process_fields() {
  local pid=$1
  local stat=

  EXACT_STAT_STATE=
  EXACT_STAT_PGID=
  EXACT_STAT_SID=
  EXACT_STAT_STARTTIME=
  case "$pid" in ''|*[!0-9]*) return 1 ;; esac

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

persist_nested_wrapper_identities() {
  local member_pid=$1
  local wrapper=$2
  local owner_program=$3
  local member_identity_file=$4
  local owner_identity_file=$5
  local deadline_ms=$6
  local member_pgid=
  local member_sid=
  local member_starttime=
  local owner_pid=
  local owner_pgid=
  local owner_sid=
  local owner_starttime=
  local member_identity_tmp="${member_identity_file}.tmp.${BASHPID:-$$}"
  local owner_identity_tmp="${owner_identity_file}.tmp.${BASHPID:-$$}"

  rm -f "$member_identity_tmp" "$owner_identity_tmp"
  while :; do
    if read_exact_process_fields "$member_pid" && \
      [ "$EXACT_STAT_STATE" != "Z" ] && \
      [ "$EXACT_STAT_PGID" = "$EXACT_STAT_SID" ] && \
      exact_wrapper_cmdline_matches "$member_pid" "$wrapper"; then
      member_pgid=$EXACT_STAT_PGID
      member_sid=$EXACT_STAT_SID
      member_starttime=$EXACT_STAT_STARTTIME
      owner_pid=$member_pgid
      if [ "$owner_pid" = "$member_pid" ]; then
        return 1
      fi

      if read_exact_process_fields "$owner_pid" && \
        [ "$EXACT_STAT_STATE" != "Z" ] && \
        [ "$EXACT_STAT_PGID" = "$owner_pid" ] && \
        [ "$EXACT_STAT_SID" = "$owner_pid" ] && \
        [ "$EXACT_STAT_STARTTIME" -le "$member_starttime" ] && \
        exact_wrapper_cmdline_matches "$owner_pid" "$owner_program"; then
        owner_pgid=$EXACT_STAT_PGID
        owner_sid=$EXACT_STAT_SID
        owner_starttime=$EXACT_STAT_STARTTIME

        if ! printf '%s %s %s %s\n' \
          "$owner_pid" "$owner_pgid" "$owner_sid" "$owner_starttime" >"$owner_identity_tmp"; then
          rm -f "$member_identity_tmp" "$owner_identity_tmp"
          return 1
        fi
        if ! mv -f -- "$owner_identity_tmp" "$owner_identity_file"; then
          rm -f "$member_identity_tmp" "$owner_identity_tmp"
          return 1
        fi
        if ! printf '%s %s %s %s\n' \
          "$member_pid" "$member_pgid" "$member_sid" "$member_starttime" >"$member_identity_tmp"; then
          rm -f "$member_identity_tmp" "$owner_identity_tmp"
          return 1
        fi
        if read_exact_process_fields "$member_pid" && \
          [ "$EXACT_STAT_STATE" != "Z" ] && \
          [ "$EXACT_STAT_PGID" = "$member_pgid" ] && \
          [ "$EXACT_STAT_SID" = "$member_sid" ] && \
          [ "$EXACT_STAT_STARTTIME" = "$member_starttime" ] && \
          exact_wrapper_cmdline_matches "$member_pid" "$wrapper" && \
          read_exact_process_fields "$owner_pid" && \
          [ "$EXACT_STAT_STATE" != "Z" ] && \
          [ "$EXACT_STAT_PGID" = "$owner_pgid" ] && \
          [ "$EXACT_STAT_SID" = "$owner_sid" ] && \
          [ "$EXACT_STAT_STARTTIME" = "$owner_starttime" ] && \
          exact_wrapper_cmdline_matches "$owner_pid" "$owner_program" && \
          mv -f -- "$member_identity_tmp" "$member_identity_file"; then
          return 0
        fi
        rm -f "$member_identity_tmp" "$owner_identity_tmp"
        return 1
      fi
    fi

    if [ ! -e "/proc/$member_pid" ] || [ "$(ml3_now_ms)" -ge "$deadline_ms" ]; then
      rm -f "$member_identity_tmp" "$owner_identity_tmp"
      return 1
    fi
    sleep 0.05
  done
}

drain_recorded_isolated_owner() {
  local identity_file=$1
  local deadline_ms=$2
  local pid=
  local pgid=
  local sid=
  local starttime=

  if ! read -r pid pgid sid starttime <"$identity_file"; then
    return 1
  fi
  case "$pid" in ''|*[!0-9]*) return 1 ;; esac
  case "$pgid" in ''|*[!0-9]*) return 1 ;; esac
  case "$sid" in ''|*[!0-9]*) return 1 ;; esac
  case "$starttime" in ''|*[!0-9]*) return 1 ;; esac
  if [ "$pid" != "$pgid" ] || [ "$pid" != "$sid" ]; then
    return 1
  fi
  ml3_drain_owned_process_identity_file "$identity_file" "$deadline_ms"
}

recorded_process_identity_alive() {
  local identity_file=$1
  local pid=
  local pgid=
  local sid=
  local starttime=

  if ! read -r pid pgid sid starttime <"$identity_file"; then
    return 1
  fi
  case "$pid" in ''|*[!0-9]*) return 1 ;; esac
  case "$starttime" in ''|*[!0-9]*) return 1 ;; esac
  if ! read_exact_process_fields "$pid"; then
    [ -e "/proc/$pid" ]
    return
  fi
  [ "$EXACT_STAT_STATE" != "Z" ] && \
    [ "$EXACT_STAT_STARTTIME" = "$starttime" ]
}
