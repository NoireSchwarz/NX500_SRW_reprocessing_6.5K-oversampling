#!/bin/sh
# Called by the already-installed eye wake service after the OEM resume.
set -u
task_root=
mod=$task_root/opt/usr/nx-lut-lite
state=$task_root/opt/usr/nxl-full040
ops=$state/fileops.arm
manifest=e3520acc8a3adf788cf6bf1a5a9a927ee55ab5fd6cf296ffc35ca79286baa167

pin() {
    [ -f "$1" ] && [ ! -L "$1" ] && [ "$("$ops" sha256 "$1")" = "$2" ]
}

menu_ready() {
    IFS= read -r pid < "$task_root/tmp/nx-lut-lite/listener.ready" || return 1
    case "$pid" in ''|*[!0-9]*|0) return 1;; esac
    [ "$(readlink "$task_root/proc/$pid/exe")" = /opt/usr/nx-lut-lite/bin/nxl-keys ]
}

# A single replacing receipt supplements the normal listener log.
record() {
    (umask 077; printf 'bundle=0.4.0\nresult=%s\n' "$1" > "$state/menu-status.new.$$") &&
        mv "$state/menu-status.new.$$" "$state/menu-status" || return 1
    [ -f "$state/armed" ] && [ ! -L "$state/armed" ] || return 0
    leaf=$(sed -n '3p' "$state/armed")
    stamp=${leaf#FAST_START_EYE_AF_ROOTLIB_}
    case "$stamp" in "$leaf"|''|*[!0-9_]*) return 0;; esac
    [ "$(cat "$task_root/sys/class/extcon/USB/state")" = 0 ] &&
        awk '$1=="/dev/mmcblk1p1" && ($2=="/mnt/mmc" || $2=="/opt/storage/sdcard") {ok=1} END {exit !ok}' "$task_root/proc/mounts" || return 0
    parent=$task_root/mnt/mmc/NXDEMO51/$leaf
    report=$parent/resume
    [ -d "$parent" ] && [ ! -L "$parent" ] && [ ! -L "$report" ] &&
        mkdir -p "$report" && [ ! -L "$report/full-menu.txt" ] || return 0
    if (set -C; cat "$state/menu-status" > "$report/.full-menu.$$"); then
        mv "$report/.full-menu.$$" "$report/full-menu.txt"
    fi
}

[ -f "$task_root/tmp/.hib_post" ] && [ ! -e "$task_root/opt/etc/.hib_capturing" ] &&
    [ "$(cat "$task_root/sys/class/extcon/USB/state")" = 0 ] || exit 0
case "$(cat "$task_root/sys/power/bootmode")" in 2|3) ;; *) exit 0;; esac
awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {busy=1} END {exit !(ok && !busy)}' "$task_root/proc/swaps" || exit 0
[ -f "$mod/state/module.manifest" ] && [ ! -L "$mod/state/module.manifest" ] &&
    { pin "$mod/state/module.manifest" "$manifest" || pin "$mod/state/module.manifest" a9c5391ec02b77943a494f68d929ec6fbfe8b5d51ac36fe648ca5f49f1e606c3; } &&
    pin "$mod/bt-start.sh" a0ee502e8d4c195cb3664a27c68e655d3e3814a3489f74fea0684e6a4db296b5 ||
    { record MODULE_IDENTITY_FAILED; exit 1; }
if [ -e "$mod/state/disabled" ]; then record MODULE_DISABLED; exit 0; fi
if [ -r "$task_root/tmp/nx-lut-lite/listener.ready" ] && menu_ready; then
    record LISTENER_INPUTS_OPEN; exit $?
fi
/bin/sh "$mod/bt-start.sh" </dev/null || { record LISTENER_START_FAILED; exit 1; }
menu_ready || { record LISTENER_UNCONFIRMED; exit 1; }
record LISTENER_INPUTS_OPEN
