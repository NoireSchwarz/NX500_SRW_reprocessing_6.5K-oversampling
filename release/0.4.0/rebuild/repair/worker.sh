#!/bin/sh
# Keep the preceding generation intact and use the original rescue handoff.
set -u
trap '' HUP
umask 077
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LC_ALL=C; export PATH LC_ALL
root=
SD=$root/mnt/mmc
PKG=$SD/@GENERATION@
OP=$PKG/fileops.arm
ETC=$root/etc/systemd/system
ctl=$root/usr/bin/systemctl
usb=$root/sys/class/extcon/USB/state
prev=$root/opt/usr/nxl-full040
canon=$root/usr/local/lib/nxl-full040
state=$root/opt/usr/@GENERATION@
rt=$root/usr/local/lib/@GENERATION@
conf=$ETC/di-camera-app.service.d/68-nxl-full040.conf
unit=$ETC/nxl-full040-resume.service
link=$ETC/hib_post.service.wants/nxl-full040-resume.service
profile=$ETC/hibernation.service.d/75-nxl-full040-prewrite.conf
header=$root/usr/bin/hibernation.sh
factory=$root/usr/bin/make_snapshot.sh
snapshot=$root/etc/snapshot
lock=$root/run/nxl-full040-rebuild.lock
tag=$(date +%s)_$$
leaf=FAST_START_EYE_AF_ROOTLIB_$tag
report=$SD/NXDEMO51/$leaf
reason=PREPARE_INCOMPLETE
locked=0; remounted=0; armed=0; new_link=0; new_canon=0; created_snapshot=0; handed=0

regular() { [ -f "$1" ] && [ ! -L "$1" ]; }
absent() { [ ! -e "$1" ] && [ ! -L "$1" ]; }
directory() { [ -d "$1" ] && [ ! -L "$1" ]; }
pin() { regular "$1" && [ "$("$OP" sha256 "$1")" = "$2" ]; }
stop() { reason=$1; printf 'STOPPED=%s\n' "$reason"; exit 1; }
card_up() {
    [ "$(cat "$usb" 2>/dev/null)" = 0 ] &&
        awk '$1=="/dev/mmcblk1p1" && ($2=="/mnt/mmc" || $2=="/opt/storage/sdcard") {ok=1} END {exit !ok}' "$root/proc/mounts"
}
idle() {
    card_up && absent "$root/opt/etc/.hib_capturing" &&
        awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {busy=1} END {exit !(ok && !busy)}' "$root/proc/swaps" || return 1
    for p in run/nxl-eye-af-launch tmp/nx-lut-lite/gamma-r11h.lease tmp/nx-lut-lite/gamma-life.pending \
        tmp/nx-lut-lite/clean-log.policy tmp/nx-lut-lite/clean-log-command.lock tmp/nx-lut-lite/video-fullwidth.pending \
        tmp/nx-lut-lite/engine.lock; do absent "$root/$p" || return 1; done
}
root_rw() {
    mode=$(awk '$2=="/" {n=split($4,a,",");for(i=1;i<=n;i++)if(a[i]=="ro" || a[i]=="rw")v=a[i]}END{print v}' "$root/proc/mounts")
    case "$mode" in ro) "$root/bin/mount" -o remount,rw / && remounted=1;; rw) :;; *) return 1;; esac
}
root_back() {
    if [ "$remounted" = 1 ]; then "$root/bin/mount" -o remount,ro / || return 1; remounted=0; fi
}
put() {
    absent "$2" && absent "$2.new.$tag" && (set -C; cat "$1" > "$2.new.$tag") &&
        chmod "$3" "$2.new.$tag" && "$OP" same "$1" "$2.new.$tag" && mv "$2.new.$tag" "$2"
}
replace_file() {
    from=$1; to=$2; mode=$3
    absent "$to.pre-repair-$tag" && absent "$to.new.$tag" || return 1
    if regular "$to"; then
        cp --preserve=all "$to" "$to.pre-repair-$tag" && "$OP" same "$to" "$to.pre-repair-$tag" &&
            cp --preserve=all "$to" "$to.new.$tag" && cat "$from" > "$to.new.$tag" || return 1
    else
        absent "$to" && (set -C; cat "$from" > "$to.new.$tag") && chmod "$mode" "$to.new.$tag" || return 1
    fi
    "$OP" same "$from" "$to.new.$tag" && printf '%s\n' "$to" >> "$report/changed-files.txt" &&
        mv -f "$to.new.$tag" "$to" && "$OP" same "$from" "$to"
}
finish() {
    rc=$?
    if [ "$handed" = 0 ] && [ "$armed" = 1 ]; then
        if root_rw; then
            if regular "$report/changed-files.txt"; then
                while IFS= read -r to; do
                    old=$to.pre-repair-$tag
                    if regular "$old"; then
                        if ! "$OP" same "$old" "$to"; then mv -f "$old" "$to" || printf 'ROLLBACK_FILE_FAILED=%s\n' "$to"; fi
                    elif regular "$to"; then
                        mv "$to" "$to.failed-$tag" || printf 'ROLLBACK_FILE_FAILED=%s\n' "$to"
                    fi
                done < "$report/changed-files.txt"
            fi
            if [ "$new_link" = 1 ]; then mv "$link" "$ETC/nxl-full040-link.failed-$tag" || printf 'ROLLBACK_LINK_FAILED\n'; fi
            if [ "$created_snapshot" = 1 ]; then mv "$snapshot" "$snapshot.failed-$tag" || printf 'ROLLBACK_SNAPSHOT_FLAG_FAILED\n'; fi
            if regular "$rt/capture-origin"; then mv "$rt/capture-origin" "$rt/rejected-$tag" || :; fi
            if [ "$new_canon" = 1 ]; then mv "$canon" "$canon.failed-$tag" || printf 'ROLLBACK_CANON_FAILED\n'; fi
            "$ctl" daemon-reload || printf 'ROLLBACK_RELOAD_FAILED\n'
        else
            printf 'ROLLBACK_REMOUNT_FAILED\n'
        fi
        if regular "$state/armed"; then mv "$state/armed" "$state/rejected-$tag" || :; fi
        # Only paths created by this transaction can be archived here.
        if [ "$state_new" = 1 ]; then mv "$state" "$state.failed-$tag" || :; fi
        if [ "$rt_new" = 1 ] && root_rw; then mv "$rt" "$rt.failed-$tag" || :; fi
    fi
    root_back || { rc=1; reason=ROOT_REPROTECT_FAILED; }
    if [ "$locked" = 1 ]; then rmdir "$lock" || :; fi
    if card_up; then
        printf 'exit=%s\nreason=%s\n' "$rc" "$reason" > "$report/result.txt"
        sync
    fi
    trap - 0
    exit "$rc"
}
current_launcher() {
    launcher=$("$ctl" --all show di-camera-app.service -p ExecStart) || return 1
    printf '%s\n' "$launcher" | awk -F ' ; ' -v expected="$1" '
        $2=="argv[]=" expected && $3=="ignore_errors=no" && $0!~/}.*\{/ {ok=1} END {exit !ok}'
}

card_up && [ ! -L "$SD/NXDEMO51" ] && mkdir -p "$SD/NXDEMO51" && mkdir "$report" || exit 70
exec > "$report/prepare.log" 2>&1
trap finish 0
trap 'stop INTERRUPTED_BEFORE_HANDOFF' INT TERM
regular "$PKG/request" && IFS= read -r request < "$PKG/request" || stop REQUEST_MISSING
case "$request" in CHECK|REBUILD) ;; *) stop INVALID_REQUEST;; esac
printf 'interface=full040-repair-1\neye_build=@GENERATION@\noperation=%s\n' "$request"
for f in info.tg nx_cs.adj install.sh; do
    regular "$SD/$f" && regular "$PKG/activation/$f" && "$OP" same "$SD/$f" "$PKG/activation/$f" || stop TRIGGER_IDENTITY
done
mkdir "$report/entry" || stop TRIGGER_ARCHIVE
for f in info.tg nx_cs.adj install.sh; do mv "$SD/$f" "$report/entry/$f" || stop TRIGGER_ARCHIVE; done
mkdir "$lock" || stop REBUILD_ALREADY_RUNNING
locked=1
"$OP" version "$root/etc/version.info" || stop WRONG_FIRMWARE
@PAYLOAD_CHECKS@
@STOCK_CHECKS@
case "$(cat "$root/sys/power/bootmode")" in 2|3) ;; *) stop NOT_A_NORMAL_INSTALL_BOOT;; esac
before=$(cat "$root/proc/sys/kernel/random/boot_id") || stop BOOT_ID_UNAVAILABLE
printf '%s\n' "$before" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' || stop INVALID_BOOT_ID
for d in "$root/usr" "$root/usr/local" "$root/usr/local/lib" "$root/opt/usr" "$ETC"; do
    directory "$d" || stop UNSAFE_INSTALL_PARENT
done
for d in "$ETC/di-camera-app.service.d" "$ETC/hibernation.service.d" "$ETC/hib_post.service.wants"; do
    absent "$d" || directory "$d" || stop UNSAFE_UNIT_DIRECTORY
done
for rel in hib_post.service.d nxl-full040-resume.service.d hib_post.service hib_post.path hibernation.service di-camera-app.service; do
    absent "$ETC/$rel" || stop FOREIGN_CAPTURE_OR_RESUME_OVERRIDE
done
for f in "$ETC/di-camera-app.service.d/"*.conf; do
    absent "$f" && continue
    got=$("$OP" sha256 "$f") && regular "$f" && grep -Fqx "${f##*/} $got" "$PKG/allowed-overrides.txt" || stop FOREIGN_CAMERA_OVERRIDE
done
for f in "$ETC/hibernation.service.d/"*.conf "$ETC/hib_post.service.wants/"*; do
    absent "$f" && continue
    [ "$f" = "$profile" ] || [ "$f" = "$link" ] || stop FOREIGN_CAPTURE_OR_RESUME_OVERRIDE
done
master=$root/usr/local/bin/nx-on-wake
masterunit=$ETC/nx-on-wake.service
masterwant=$ETC/multi-user.target.wants/nx-on-wake.service
absent "$masterwant" || stop LEGACY_WAKE_ENABLED
if ! absent "$master" || ! absent "$masterunit"; then
    pin "$master" @OLD_MASTER@ && pin "$masterunit" @OLD_MASTER_UNIT@ || stop FOREIGN_WAKE_MASTER
    info=$("$ctl" --all show nx-on-wake.service -p ActiveState) || stop LEGACY_WAKE_STATE_UNKNOWN
    [ "$info" = ActiveState=inactive ] || stop LEGACY_WAKE_RUNNING
fi
absent "$root/opt/usr/nx-on-wake/eye-af-faststart/pending" &&
    absent "$root/opt/usr/nx-on-wake/eye-af-faststart/retry/armed" || stop LEGACY_CAPTURE_PENDING
absent "$SD/nx500.bin" && absent "$SD/nx-on-wake/on-wake" && absent "$SD/install/install.sh" || stop FOREIGN_SD_BOOT_ENTRY

predecessor=stock
if absent "$prev" && absent "$canon"; then
    absent "$conf" && absent "$unit" && absent "$link" && absent "$profile" || stop PARTIAL_OR_FOREIGN_INSTALL
    @CLEAN_CHECKS@
    current_launcher /usr/apps/com.samsung.di-camera-app/bin/di-camera-app || stop WRONG_ACTIVE_LAUNCHER
else
    directory "$prev" && directory "$canon" && regular "$prev/armed" && regular "$canon/capture-origin" &&
        [ "$(sed -n '2p' "$prev/armed")" = EYE ] && "$OP" same "$prev/armed" "$canon/capture-origin" || stop PARTIAL_OR_FOREIGN_INSTALL
    @R1_CHECKS@
    [ -L "$link" ] && [ "$(readlink "$link")" = /etc/systemd/system/nxl-full040-resume.service ] || stop FOREIGN_RESUME_LINK
    current_launcher '/bin/sh /usr/local/lib/nxl-full040/launch.sh' || stop WRONG_ACTIVE_LAUNCHER
    predecessor=r1
fi
printf 'predecessor=%s\nboot_id=%s\n' "$predecessor" "$before"
rootdev=$(stat -c %d "$root/") && rtdev=$(stat -c %d "$root/usr/local/lib") && optdev=$(stat -c %d "$root/opt/usr") || stop MOUNT_QUERY
[ "$rootdev" = "$rtdev" ] && [ "$rootdev" != "$optdev" ] || stop RUNTIME_MOUNT_NOT_SEPARATE_FROM_OPT
mod=$root/opt/usr/nx-lut-lite
manifest=$("$OP" sha256 "$mod/state/module.manifest") || stop MODULE_NOT_INSTALLED
case "$manifest" in @MANIFESTS@) ;; *) stop WRONG_FULL_MODULE;; esac
regular "$mod/state/demo51-installed" && grep -qx 'install_revision=release-0.4.0-rc6.14.12-log117' "$mod/state/demo51-installed" &&
    absent "$mod/state/disabled" || stop WRONG_FULL_MODULE
while read -r wanted file extra; do
    case "$file" in ''|/*|*..*|*\\*) stop INVALID_MODULE_MANIFEST;; esac
    [ -z "$extra" ] && pin "$mod/$file" "$wanted" || stop FULL_MODULE_FILE_MISMATCH
done < "$mod/state/module.manifest"
idle || stop CAMERA_NOT_IDLE
if [ "$request" = CHECK ]; then reason=CHECKED_READY; exit 0; fi
absent "$rt" && absent "$state" || stop CANDIDATE_ALREADY_STAGED
capacity=$(cat "$root/sys/class/power_supply/battery/capacity") || stop BATTERY_UNREADABLE
case "$capacity" in ''|*[!0-9]*) stop BATTERY_UNREADABLE;; esac
[ "$capacity" -ge 95 ] || stop FULL_BATTERY_REQUIRED
dev=$root/dev/mmcblk0p9
[ -b "$dev" ] && [ ! -L "$dev" ] && [ "$(cat "$root/sys/class/block/mmcblk0p9/size")" = 307199 ] || stop SNAPSHOT_DEVICE_MISMATCH
absent "$snapshot" || regular "$snapshot" || stop UNSAFE_SNAPSHOT_FLAG
idle && root_rw || stop CAMERA_BUSY_OR_ROOT_UNAVAILABLE
armed=1; state_new=0; rt_new=0
mkdir "$state" || stop STATE_CREATE
state_new=1
mkdir -m 755 "$rt" || stop RUNTIME_CREATE
rt_new=1
for f in fileops.arm claim.arm; do put "$PKG/$f" "$state/$f" 755 || stop STATE_INSTALL; done
for f in launch.sh dispatch.sh full-menu.sh wake.sh collect-resume.sh fileops.arm; do put "$PKG/$f" "$rt/$f" 755 || stop RUNTIME_INSTALL; done
put "$PKG/nxl-eye-af.so" "$rt/nxl-eye-af.so" 644 || stop EYE_LIBRARY_INSTALL
if [ "$predecessor" = r1 ]; then
    for f in launch wake; do put "$canon/$f.sh" "$rt/legacy-$f.sh" 755 || stop LEGACY_SELECTOR_SAVE; done
fi
mkdir "$state/preflight" && "$state/claim.arm" "$state/preflight" "$before" preflight "$$" EYE "$rt/nxl-eye-af.so" &&
    /bin/sh "$PKG/loader-observe.sh" "$rt/nxl-eye-af.so" "$state/preflight/launch-claim" > "$report/loader-check.txt" 2>&1 || stop ARM_LIBRARY_LOAD_FAILED
if [ "$predecessor" = stock ]; then
    mkdir -m 755 "$canon" || stop SELECTOR_DIRECTORY_CREATE
    new_canon=1
fi
mkdir -p "${profile%/*}" "${link%/*}" "${conf%/*}" || stop UNIT_DIRECTORY_CREATE
for change in "capture.conf:$conf:644" "nxl-full040-resume.service:$unit:644" "capture-profile.conf:$profile:644" \
    "hibernation.sh:$header:755" "make_snapshot.sh:$factory:755" "launch-select.sh:$canon/launch.sh:755" \
    "wake-select.sh:$canon/wake.sh:755" "verify-before-capture.sh:$canon/verify-before-capture.sh:755"; do
    f=${change%%:*}; rest=${change#*:}; target=${rest%:*}; perm=${rest##*:}
    replace_file "$PKG/$f" "$target" "$perm" || stop BOOT_CONFIG_INSTALL
done
if absent "$link"; then
    ln -s /etc/systemd/system/nxl-full040-resume.service "$link" || stop RESUME_LINK_INSTALL
    new_link=1
fi
"$ctl" daemon-reload && current_launcher '/bin/sh /usr/local/lib/nxl-full040/launch.sh' || stop CURRENT_LAUNCHER_NOT_SELECTED
"$ctl" --all show hib_post.service -p Wants > "$report/installed-wants.txt" &&
    tr '= ' '\n\n' < "$report/installed-wants.txt" | grep -Fqx nxl-full040-resume.service || stop RESUME_DEPENDENCY_NOT_LOADED
"$ctl" --all show nxl-full040-resume.service -p ExecStart > "$report/installed-entry.txt" &&
    grep -Fq 'argv[]=/bin/sh /usr/local/lib/nxl-full040/wake.sh ;' "$report/installed-entry.txt" || stop UNIFIED_ENTRY_NOT_SELECTED
if absent "$snapshot"; then
    (set -C; : > "$snapshot") || stop SNAPSHOT_FLAG_INSTALL
    created_snapshot=1
fi
(set -C; printf '%s\nEYE\n%s\n' "$before" "$leaf" > "$state/armed") &&
    put "$state/armed" "$rt/capture-origin" 600 || stop ARM_GENERATION
root_back && idle || stop FINAL_IDLE_OR_ROOT_REPROTECT_FAILED
printf 'interface=full040-repair-1\neye_build=@GENERATION@\nboot_id=%s\nreport=%s\n' "$before" "$leaf" > "$report/CAPTURE_REQUESTED" || stop DISPATCH_RECEIPT
printf 'exit=DISPATCHING\nreason=OEM_RESCUE_REQUEST\n' > "$report/result.txt" || stop DISPATCH_RECEIPT
"$PKG/nxl-menu" --fast-start-capture </dev/null >/dev/null 2>&1 &
sync
cd / || stop FINAL_CWD
handed=1
exec </dev/null >/dev/console 2>&1
exec /bin/sh "$rt/dispatch.sh" "$leaf"
