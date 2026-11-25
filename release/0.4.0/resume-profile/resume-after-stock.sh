#!/bin/sh
# Triggered by the factory's post-resume event. The installed helpers own eye and menu readiness.
set -u
umask 077
task_root=
state=$task_root/opt/usr/nxl-full040
rt=$task_root/usr/local/lib/nxl-full040
mod=$task_root/opt/usr/nx-lut-lite
run=$task_root/tmp/nx-lut-lite
usb=$task_root/sys/class/extcon/USB/state
ops=$state/fileops.arm
sd=$task_root/mnt/mmc
receipt=$state/normal-wake.status
card_report=
regular() { [ -f "$1" ] && [ ! -L "$1" ]; }
pin() { regular "$1" && [ "$("$ops" sha256 "$1")" = "$2" ]; }
usb_off() { [ "$(cat "$usb" 2>/dev/null)" = 0 ]; }
card_up() { awk '$1=="/dev/mmcblk1p1" && ($2=="/opt/storage/sdcard" || $2=="/mnt/mmc"){ok=1} END {exit !ok}' "$task_root/proc/mounts"; }
normal() {
    case "$(cat "$task_root/sys/power/bootmode" 2>/dev/null)" in 2|3) ;; *) return 1;; esac
    [ -f "$task_root/tmp/.hib_post" ] && [ ! -e "$task_root/opt/etc/.hib_capturing" ] && usb_off &&
        awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {busy=1} END {exit !(ok && !busy)}' "$task_root/proc/swaps"
}
record() {
    [ -d "$state" ] && [ ! -L "$state" ] && [ ! -L "$receipt" ] || return 1
    tmp=$receipt.new.$$
    (set -C; printf 'source=hib_post.service\nresult=%s\nuptime=%s\n' "$1" "$(cut -d' ' -f1 "$task_root/proc/uptime")" > "$tmp") &&
        mv -f "$tmp" "$receipt" || return 1
    # If this image has an existing report, publish an early refusal there even
    # when the service cannot dispatch. This is one replacing status file.
    if [ -z "$card_report" ] && normal && card_up && regular "$state/armed"; then
        label=$(sed -n '3p' "$state/armed")
        suffix=$(printf '%s' "$label" | sed 's/^FAST_START_EYE_AF_ROOTLIB_//')
        case "$suffix" in "$label"|''|*[!0-9_]*) :;;
            *) prior=$sd/NXDEMO51/$label
               if [ -d "$prior" ] && [ ! -L "$prior" ] && [ -d "$prior/resume" ] && [ ! -L "$prior/resume" ]; then
                   card_report=$prior/resume
               fi;;
        esac
    fi
    if [ -n "$card_report" ] && normal && card_up && [ ! -L "$card_report" ] &&
        { [ ! -e "$card_report/normal-start.txt" ] || regular "$card_report/normal-start.txt"; } &&
        [ ! -L "$card_report/normal-start.txt" ]; then
        tmp=$card_report/.normal-start.$$
        if (set -C; cat "$receipt" > "$tmp"); then mv -f "$tmp" "$card_report/normal-start.txt"; fi
    fi
    return 0
}
stop() { record "$1"; exit 0; }
normal || exit 0
regular "$ops" && [ -x "$ops" ] || exit 1
pin "$ops" c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d && pin "$rt/readout" bded257edb584f914f0dc2a0d9ea5f84949e2cc89b19ce31d2975ba5e9342bc0 && pin "$rt/full-menu.sh" 0655ae8a221a71e8a684de78a9df12c59bc7ae301b090bb1c145e95fa53563c4 &&
    pin "$mod/process.sh" cf0f12a1b5eba46ae9ec46c2d3b98ff16aed93582d29d378e157ea5d32589867 || stop PAYLOAD_MISMATCH
[ ! -e "$mod/state/disabled" ] || stop MODULE_DISABLED
. "$mod/process.sh"
record WAITING_FOR_NORMAL_MODE
n=0
while normal; do
    if card_up && nxl_require_normal_mode >/dev/null 2>&1; then break; fi
    [ "$n" -lt 45 ] || stop CARD_OR_NORMAL_MODE_TIMEOUT
    sleep 1; n=$((n+1))
done
normal && card_up || exit 0
claim=$state/launch-claim
regular "$claim" && regular "$state/armed" || stop GENERATION_UNAVAILABLE
{ IFS= read -r previous && IFS= read -r policy && IFS= read -r leaf; } < "$state/armed" || stop INVALID_CAPTURE_RECORD
case "$leaf" in FAST_START_EYE_AF_ROOTLIB_*) ;; *) stop INVALID_CAPTURE_RECORD;; esac
case "$(printf '%s\n' "$leaf" | sed 's/^FAST_START_EYE_AF_ROOTLIB_//')" in ''|*[!0-9_]*) stop INVALID_CAPTURE_RECORD;; esac
current=$(cat "$task_root/proc/sys/kernel/random/boot_id") || stop INVALID_CAPTURE_RECORD
[ -n "$previous" ] && [ -n "$current" ] && [ "$previous" != "$current" ] && [ "$policy" = EYE ] &&
    grep -Fqx "boot_id=$current" "$claim" && grep -Fqx 'policy=EYE' "$claim" || stop CAPTURE_MISMATCH
pid=$(sed -n 's/^camera_pid=//p' "$claim")
case "$pid" in ''|0|*[!0-9]*) stop CAMERA_UNAVAILABLE;; esac
eye=/usr/local/lib/nxl-full040/nxl-eye-af.so
dev=$(sed -n 's/^mapped_dev=//p' "$claim"); ino=$(sed -n 's/^mapped_ino=//p' "$claim")
[ "$(readlink "$task_root/proc/$pid/exe" 2>/dev/null)" = /usr/apps/com.samsung.di-camera-app/bin/di-camera-app ] &&
    grep -Fqx "file=$eye" "$claim" &&
    awk -v d="$dev" -v i="$ino" -v p="$eye" '$4==d && $5==i && $2~/x/ && $6==p && NF==6 {ok=1} END {exit !ok}' "$task_root/proc/$pid/maps" &&
    dmesg | grep -Fq 'kernel_resume start!' || stop CAMERA_IDENTITY_UNCONFIRMED
parent=$sd/NXDEMO51/$leaf
[ -d "$parent" ] && [ ! -L "$parent" ] && [ ! -L "$parent/resume" ] && mkdir -p "$parent/resume" || stop CAPTURE_REPORT_UNAVAILABLE
card_report=$parent/resume
eye_ready() {
    regular "$run/eye-af.resume-ready" && [ "$(wc -c < "$run/eye-af.resume-ready")" -eq "$(printf '%s\n' "$pid" | wc -c)" ] &&
        grep -Fqx "$pid" "$run/eye-af.resume-ready"
}
menu_ready() {
    regular "$run/listener.ready" && IFS= read -r key_pid < "$run/listener.ready" || return 1
    case "$key_pid" in ''|0|*[!0-9]*) return 1;; esac
    [ "$(readlink "$task_root/proc/$key_pid/exe" 2>/dev/null)" = /opt/usr/nx-lut-lite/bin/nxl-keys ]
}
normal && card_up && nxl_require_normal_mode >/dev/null 2>&1 || exit 0
if eye_ready; then
    if menu_ready; then stop ALREADY_READY; fi
    /bin/sh "$rt/full-menu.sh" || stop MENU_START_FAILED
else
    [ ! -L "$run" ] && [ ! -e "$run/eye-af.resume-ready" ] && [ ! -L "$run/eye-af.resume-ready" ] || stop READY_FILE_CONFLICT
    record REQUESTING_INSTALLED_WAKE
    /bin/sh "$rt/readout" || stop EXISTING_WAKE_FAILED
fi
n=0
while normal && card_up; do
    if eye_ready && menu_ready; then stop EYE_AND_MENU_READY; fi
    [ "$n" -lt 12 ] || break
    sleep 1; n=$((n+1))
done
if eye_ready; then stop EYE_READY_MENU_UNCONFIRMED; fi
stop READY_UNCONFIRMED
