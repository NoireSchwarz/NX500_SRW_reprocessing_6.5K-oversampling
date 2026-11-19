#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Normal-view read-only capture. Does not apply/off a LUT or send AF events.
set -u
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
. "$D/process.sh"
nxl_require_normal_mode || exit 1
{ IFS= read -r ver; IFS= read -r model; } < /etc/version.info || exit 1
[ "$ver" = 1.12 ] && [ "$model" = NX500 ] || exit 1
RUN=/tmp/nx-lut-lite
OUT=/mnt/mmc/NXDEMO51/logs/af-trace
[ ! -L /mnt/mmc/NXDEMO51 ] && [ ! -L /mnt/mmc/NXDEMO51/logs ] && [ ! -L "$OUT" ] || exit 1
mkdir -p "$RUN" "$OUT" || exit 1
lock=$RUN/trace.lock
if ! mkdir "$lock" 2>/dev/null; then
    old=
    [ ! -r "$lock/pid" ] || IFS= read -r old < "$lock/pid"
    case "$old" in ''|*[!0-9]*) echo 'Trace lock has no valid PID.' >&2; exit 1;; esac
    if kill -0 "$old" 2>/dev/null; then echo 'Trace already running.' >&2; exit 1; fi
    rm -f "$lock/pid"
    rmdir "$lock" 2>/dev/null && mkdir "$lock" || exit 1
fi
echo $$ > "$lock/pid"
child=
result_file=
finish() {
    code=$?; trap - 0 HUP INT TERM
    if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; wait "$child" 2>/dev/null || :; fi
    if [ "$code" -ne 0 ] && [ -n "$result_file" ]; then
        printf 'TRACE_ABORTED exit=%s\nINCOMPLETE\n' "$code" >> "$result_file" 2>/dev/null || :
    fi
    rm -f "$lock/pid"; rmdir "$lock" 2>/dev/null || :
    exit "$code"
}
trap finish 0
trap 'exit 130' HUP INT TERM
tag=OFF
[ ! -r "$RUN/session/current" ] || tag=ACTIVE
for suffix in before.txt after.txt csv result.txt; do
    f=$OUT/$tag.$suffix
    [ ! -f "$f" ] || cp "$f" "$f.previous" || exit 1
done
result_file=$OUT/$tag.result.txt
printf 'RUNNING tag=%s; previous reports retained as .previous\n' "$tag" > "$OUT/$tag.result.txt"
export DISPLAY=${DISPLAY:-:0} NX_LUT_ROOT=$D
before_rc=0
/bin/sh "$D/diagnose.sh" "$OUT/$tag.before.txt" || before_rc=$?
printf 'snapshot_before_exit=%s\n' "$before_rc" >> "$OUT/$tag.result.txt"
[ "$before_rc" -eq 0 ] || exit "$before_rc"
{
    echo '\n--- original A18 comparison (read only; no slot write) ---'
    if [ -r "$RUN/session/a18_orig.bin" ]; then
        "$D/bin/nx_slotwrite.arm" dry 0x81119f00 "$RUN/session/a18_orig.bin"
    else echo '(no original slot snapshot available)'; fi
} >> "$OUT/$tag.before.txt" 2>&1
echo "TRACE_BEGIN $tag: 200 samples, 100 ms interval, no AF/shutter events"
"$D/bin/nxl-lutobserve.arm" watch > "$OUT/$tag.csv" 2>> "$OUT/$tag.result.txt" &
child=$!
rc=0
wait "$child" || rc=$?
child=
printf 'observer_exit=%s\n' "$rc" >> "$OUT/$tag.result.txt"
if [ "$rc" -eq 0 ]; then echo 'samples_status=COMPLETE' >> "$OUT/$tag.result.txt";
else echo 'samples_status=INCOMPLETE' >> "$OUT/$tag.result.txt"; fi
after_rc=0
/bin/sh "$D/diagnose.sh" "$OUT/$tag.after.txt" || after_rc=$?
printf 'snapshot_after_exit=%s\n' "$after_rc" >> "$OUT/$tag.result.txt"
if [ "$rc" -eq 0 ] && [ "$after_rc" -ne 0 ]; then rc=10; fi
if [ "$rc" -eq 0 ]; then
    echo COMPLETE >> "$OUT/$tag.result.txt"
    echo "TRACE_SAVED $OUT/$tag.csv"
else
    echo INCOMPLETE >> "$OUT/$tag.result.txt"
    echo 'Trace or snapshots incomplete; retain the CSV, result and before/after reports.' >&2
fi
exit "$rc"
