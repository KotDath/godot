#!/bin/sh
# SDK developer launches only. No Python, root access or audb on the device.
# Invocation: sh -c SCRIPT -- ACTION TOKEN [ABSOLUTE_EXECUTABLE ARGS...]
# The installed Aurora executable owns the detached native supervisor. Stop
# only writes a fence; it never sends a signal to a potentially reused PID.
set -f
umask 077
action=$1
token=$2
shift 2 || exit 64
case "$token" in ''|*[!0123456789abcdef]*) exit 64;; esac
[ "${#token}" = 32 ] || exit 64
cache="$HOME/.cache/godot-aurora"
sessions="$cache/sessions"
session="$sessions/$token"
record="$session/process"
boot=$(cat /proc/sys/kernel/random/boot_id) || exit 69

proc_start() {
    line=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    # comm can contain spaces and parentheses; fields after its LAST ')' are
    # unambiguous. Start time is field 20 of that remaining stat suffix.
    tail=${line##*) }
    set -- $tail
    [ "$#" -ge 20 ] || return 1
    case "$1" in Z|X) return 1;; esac
    shift 19
    printf '%s\n' "$1"
}

read_record() {
    [ -f "$1" ] || return 1
    IFS=' ' read -r recorded_boot recorded_pid recorded_start extra < "$1" || return 1
    [ -z "$extra" ] || return 1
    case "$recorded_boot" in ''|*[!0123456789abcdef-]*) return 1;; esac
    case "$recorded_pid" in ''|*[!0123456789]*) return 1;; esac
    case "$recorded_start" in ''|*[!0123456789]*) return 1;; esac
    [ "$recorded_pid" -gt 1 ] 2>/dev/null || return 1
}

owned_running() {
    read_record "$1" || return 1
    [ "$recorded_boot" = "$boot" ] || return 1
    actual_start=$(proc_start "$recorded_pid") || return 1
    [ "$actual_start" = "$recorded_start" ]
}

status() {
    state=missing
    recorded_pid=0
    recorded_start=0
    if owned_running "$record"; then
        state=running
        [ ! -f "$session/stop" ] || state=stopping
    elif [ -f "$record" ] && [ ! -f "$session/exit" ] && [ "$recorded_boot" = "$boot" ]; then
        child_pid=$recorded_pid
        child_start=$recorded_start
        state=interrupted
        if owned_running "$session/manager"; then
            state=finalizing
        fi
        recorded_pid=$child_pid
        recorded_start=$child_start
    elif [ -f "$session/error" ]; then
        state=failed
    elif [ -f "$session/stop" ]; then
        state=stopped
    elif [ -f "$record" ]; then
        state=exited
    elif [ -d "$session" ]; then
        state=preparing
    fi
    printf 'GODOT_AURORA_SESSION:%s:%s:%s:%s:%s\n' "$token" "$state" "$boot" "$recorded_pid" "$recorded_start"
}

case "$action" in
    status)
        status
        ;;
    launch)
        [ "$#" -gt 0 ] || exit 64
        case "$1" in /*) ;; *) exit 64;; esac
        [ -x "$1" ] || exit 69
        mkdir -p "$sessions" || exit 73
        executable=$1
        shift
        "$executable" --aurora-remote-session "$token" "$@" || exit "$?"
        count=0
        while [ ! -f "$record" ] && [ ! -f "$session/stop" ] && [ "$count" -lt 3 ]; do
            sleep 1
            count=$((count + 1))
        done
        status
        ;;
    stop)
        mkdir -p "$session" || exit 73
        # Fence requests that were sent but have not arrived/started yet. The
        # worker publishes its identity BEFORE testing this marker, so either
        # it observes Stop, or Stop can observe and terminate that exact worker.
        : > "$session/stop" || exit 74
        count=0
        while { owned_running "$record" || owned_running "$session/manager"; } && [ "$count" -lt 3 ]; do
            sleep 1
            count=$((count + 1))
        done
        status
        ;;
    *) exit 64;;
esac
