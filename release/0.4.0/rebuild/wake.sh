#!/bin/sh
# The single owner of Eye AF enablement and the full-mod key listener.
# A mounted SD card is optional. No SD callback is involved in starting either module.
set -u
umask 077
task_root=
rt=$task_root/usr/local/lib/nxl-full040
state=$task_root/opt/usr/nxl-full040
mod=$task_root/opt/usr/nx-lut-lite
run=$task_root/tmp/nx-lut-lite
ops=$rt/fileops.arm
sd=$task_root/mnt/mmc
regular() { [ -f "$1" ] && [ ! -L "$1" ]; }
pin() { regular "$1" && [ "$("$ops" sha256 "$1")" = "$2" ]; }
normal() {
    [ "$(cat "$task_root/sys/class/extcon/USB/state" 2>/dev/null)" = 0 ] &&
        [ -f "$task_root/tmp/.hib_post" ] && [ ! -e "$task_root/opt/etc/.hib_capturing" ] || return 1
    case "$(cat "$task_root/sys/power/bootmode" 2>/dev/null)" in 2|3) ;; *) return 1;; esac
    awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {busy=1} END {exit !(ok && !busy)}' "$task_root/proc/swaps"
}
card_up() {
    normal && awk '$1=="/dev/mmcblk1p1" && ($2=="/mnt/mmc" || $2=="/opt/storage/sdcard") {ok=1} END {exit !ok}' "$task_root/proc/mounts"
}
record() {
    regular "$state/armed" && [ -d "$state" ] && [ ! -L "$state" ] && [ ! -L "$state/normal-wake.status" ] || return 1
    tmp=$state/.unified-status.$$
    (set -C; printf 'interface=full040-unified-1\nsource=hib_post.service\nresult=%s\nboot_id=%s\ncamera_pid=%s\nuptime=%s\n' \
        "$1" "${current:-unknown}" "${pid:-0}" "$(cut -d' ' -f1 "$task_root/proc/uptime")" > "$tmp") &&
        mv -f "$tmp" "$state/normal-wake.status" || return 1
    if card_up && [ -n "${leaf:-}" ]; then
        parent=$sd/NXDEMO51/$leaf
        report=$parent/resume
        if [ -d "$parent" ] && [ ! -L "$parent" ] && [ ! -L "$report" ] && mkdir -p "$report" && [ ! -L "$report/normal-start.txt" ]; then
            cp "$state/normal-wake.status" "$report/normal-start.txt"
            # /run belongs to the captured RAM image. Export the two current-generation gates.
            for proof in "$task_root"/run/nxl-full040-capture/*.txt; do
                regular "$proof" || continue
                grep -Fqx "boot_id=${current:-unknown}" "$proof" && grep -Fqx 'decision=PASS' "$proof" || continue
                dest=$report/${proof##*/}
                [ ! -L "$dest" ] && cp "$proof" "$dest"
            done
        fi
    fi
}
stop() { record "$1"; exit 1; }
normal || exit 0
pin "$ops" @OPS@ && pin "$mod/process.sh" @PROCESS@ && pin "$rt/full-menu.sh" @MENU@ &&
    pin "$rt/nxl-eye-af.so" @EYE@ || exit 1
regular "$state/armed" && regular "$state/launch-claim" && regular "$rt/capture-origin" || exit 1
{ IFS= read -r previous && IFS= read -r policy && IFS= read -r leaf; } < "$state/armed" || exit 1
suffix=${leaf#FAST_START_EYE_AF_ROOTLIB_}
case "$suffix" in "$leaf"|''|*[!0-9_]*) exit 1;; esac
current=$(cat "$task_root/proc/sys/kernel/random/boot_id") || exit 1
[ "$policy" = EYE ] && [ -n "$previous" ] && [ "$previous" != "$current" ] &&
    "$ops" same "$state/armed" "$rt/capture-origin" || stop CAPTURE_GENERATION_MISMATCH
claim=$state/launch-claim
grep -Fqx "boot_id=$current" "$claim" && grep -Fqx 'policy=EYE' "$claim" &&
    dmesg | grep -Fq 'kernel_resume start!' || stop RESTORED_CAPTURE_UNCONFIRMED
pid=$(sed -n 's/^camera_pid=//p' "$claim")
case "$pid" in ''|0|*[!0-9]*) stop INVALID_CAMERA_PID;; esac
. "$mod/process.sh"
record WAITING_FOR_CAMERA
n=0
while normal; do
    if [ "$(readlink "$task_root/proc/$pid/exe" 2>/dev/null)" = /usr/apps/com.samsung.di-camera-app/bin/di-camera-app ] &&
        nxl_require_normal_mode >/dev/null 2>&1; then break; fi
    [ "$n" -lt 45 ] || stop CAMERA_NOT_READY
    sleep 1; n=$((n+1))
done
normal || exit 0
eye=/usr/local/lib/nxl-full040/nxl-eye-af.so
dev=$(sed -n 's/^mapped_dev=//p' "$claim"); ino=$(sed -n 's/^mapped_ino=//p' "$claim")
grep -Fqx "file=$eye" "$claim" &&
    awk -v d="$dev" -v i="$ino" -v p="$eye" '$4==d && $5==i && $2~/x/ && $6==p && NF==6 {ok=1} END {exit !ok}' \
        "$task_root/proc/$pid/maps" || stop EYE_LIBRARY_NOT_IN_CAPTURE_PROCESS
[ ! -e "$mod/state/disabled" ] || stop MODULE_DISABLED
[ ! -L "$run" ] && mkdir -p "$run" && mkdir "$run/unified-wake.lock" || exit 1
trap 'rmdir "$run/unified-wake.lock"' 0
token=$run/eye-af.resume-ready
if [ -e "$token" ] || [ -L "$token" ]; then
    regular "$token" && [ "$(cat "$token")" = "$pid" ] || stop EYE_READY_CONFLICT
else
    (set -C; printf '%s\n' "$pid" > "$token") || stop EYE_ENABLE_FAILED
fi
# Enabling the eye library precedes opening the menu's key inputs.
/bin/sh "$rt/full-menu.sh" || stop EYE_READY_MENU_START_FAILED
regular "$run/listener.ready" && IFS= read -r key_pid < "$run/listener.ready" || stop EYE_READY_MENU_UNCONFIRMED
case "$key_pid" in ''|0|*[!0-9]*) stop EYE_READY_MENU_UNCONFIRMED;; esac
[ "$(readlink "$task_root/proc/$key_pid/exe" 2>/dev/null)" = /opt/usr/nx-lut-lite/bin/nxl-keys ] || stop EYE_READY_MENU_UNCONFIRMED
record EYE_AND_MENU_READY
# Optional telemetry cannot delay or decide whether either feature starts.
if card_up && pin "$rt/collect-resume.sh" @COLLECTOR@; then
    /bin/sh "$rt/collect-resume.sh" </dev/null >/dev/null 2>&1 &
fi
exit 0
