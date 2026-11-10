#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only. Start one passive observer per camera.
set -u
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
WATCH=$D/bin/nxl-handoff-watch.arm
OPS=$D/bin/nxl-fileops
WATCH_SHA=5fdd4546dee2cdacb2fc77bc27537d3b20c237c862c85a8a5d03f0c5c053c90e
RUN=/tmp/nx-lut-lite
OUT=/mnt/mmc/NXDEMO51
case "${1:---start}" in --start) :;;*) exit 2;;esac
[ ! -L "$RUN" ] && [ ! -L "$OUT" ] || exit 1
[ -d "$RUN" ] || mkdir "$RUN" || exit 1
[ -d "$OUT" ] || mkdir "$OUT" || exit 1
active_pid=
if [ -f "$RUN/handoff-observer.active" ] && [ ! -L "$RUN/handoff-observer.active" ];then
    IFS= read -r active_pid < "$RUN/handoff-observer.active" || active_pid=
    case "$active_pid" in ''|*[!0-9]*) active_pid=;;esac
    if [ -n "$active_pid" ] && kill -0 "$active_pid" 2>/dev/null && [ "$(readlink "/proc/$active_pid/exe" 2>/dev/null)" = "$WATCH" ];then
        printf 'INIT_OBSERVER_ALREADY_ACTIVE pid=%s\n' "$active_pid"
        exit 0
    fi
fi
[ -f "$WATCH" ] && [ ! -L "$WATCH" ] && [ -x "$WATCH" ] || exit 1
actual=$("$OPS" sha256 "$WATCH") || exit 1
[ "${actual%% *}" = "$WATCH_SHA" ] || exit 1
stamp=$(date -u '+%Y%m%d_%H%M%S') || exit 1
case "$stamp" in ''|*[!0-9_]*) exit 1;;esac
trace=$OUT/INIT_${stamp}_$$
attempt=0
while [ -e "$trace" ] || [ -L "$trace" ];do
    [ "$attempt" -lt 100 ] || exit 1
    attempt=$((attempt+1));trace=$OUT/INIT_${stamp}_$$_$attempt
done
mkdir "$trace" || exit 1
"$WATCH" "$trace" > "$trace/worker.stdout.txt" 2>&1 &
printf 'INIT_OBSERVER_STARTED pid=%s directory=%s\n' "$!" "$trace"
