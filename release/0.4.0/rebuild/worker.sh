#!/bin/sh
# Rebuild only the recognized 0.4.0 generation, through the proven OEM rescue path.
set -u
trap '' HUP
umask 077
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LC_ALL=C; export PATH LC_ALL
task_root=
SD=$task_root/mnt/mmc
PKG=$SD/nxl-full040-rebuild1
OPS=$PKG/fileops.arm
STATE=$task_root/opt/usr/nxl-full040
RT=$task_root/usr/local/lib/nxl-full040
ETC=$task_root/etc/systemd/system
CTL=$task_root/usr/bin/systemctl
USB=$task_root/sys/class/extcon/USB/state
DEV=$task_root/dev/mmcblk0p9
tag=$(date +%s)_$$
leaf=FAST_START_EYE_AF_ROOTLIB_$tag
REPORT=$SD/NXDEMO51/$leaf
reason=PREPARE_INCOMPLETE
remounted=0; changed=0; state_saved=0; rt_saved=0; state_new=0; rt_new=0; handed=0; locked=0; link_added=0; master_detached=0
regular() { [ -f "$1" ] && [ ! -L "$1" ]; }
absent() { [ ! -e "$1" ] && [ ! -L "$1" ]; }
pin() { regular "$1" && [ "$("$OPS" sha256 "$1")" = "$2" ]; }
fail() { reason=$1; printf 'STOPPED=%s\n' "$reason"; exit 1; }
idle() {
    [ "$(cat "$USB" 2>/dev/null)" = 0 ] && absent "$task_root/opt/etc/.hib_capturing" &&
        awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {busy=1} END {exit !(ok && !busy)}' "$task_root/proc/swaps" || return 1
    for p in run/nxl-eye-af-launch tmp/nx-lut-lite/gamma-r11h.lease tmp/nx-lut-lite/gamma-life.pending \
        tmp/nx-lut-lite/clean-log.policy tmp/nx-lut-lite/clean-log-command.lock tmp/nx-lut-lite/video-fullwidth.pending \
        tmp/nx-lut-lite/engine.lock; do absent "$task_root/$p" || return 1; done
}
root_rw() {
    mode=$(awk '$2=="/" {n=split($4,a,",");for(i=1;i<=n;i++)if(a[i]=="ro" || a[i]=="rw")v=a[i]}END{print v}' "$task_root/proc/mounts")
    case "$mode" in ro) "$task_root/bin/mount" -o remount,rw / && remounted=1;; rw) :;; *) return 1;; esac
}
root_back() {
    if [ "$remounted" = 1 ]; then "$task_root/bin/mount" -o remount,ro / || return 1; remounted=0; fi
}
put() {
    src=$1; dst=$2; perm=$3
    absent "$dst" && absent "$dst.new.$$" || return 1
    (set -C; cat "$src" > "$dst.new.$$") && chmod "$perm" "$dst.new.$$" &&
        "$OPS" same "$src" "$dst.new.$$" && mv "$dst.new.$$" "$dst" && "$OPS" same "$src" "$dst"
}
replace_file() {
    src=$1; rel=$2; perm=$3
    dst=$task_root/$rel
    absent "$dst.pre-full040-$tag" || return 1
    printf '%s\n' "$rel" >> "$REPORT/changed-files.txt" || return 1
    changed=1
    if ! absent "$dst"; then
        regular "$dst" && mv "$dst" "$dst.pre-full040-$tag" || return 1
        # Preserve ownership, mode and security xattrs of existing OEM files.
        cp --preserve=all "$dst.pre-full040-$tag" "$dst.new.$$" &&
            cat "$src" > "$dst.new.$$" && "$OPS" same "$src" "$dst.new.$$" &&
            mv "$dst.new.$$" "$dst" && "$OPS" same "$src" "$dst"
    else
        mkdir -p "$(dirname "$dst")" && put "$src" "$dst" "$perm"
    fi
}
finish() {
    rc=$?
    if [ "$handed" = 0 ]; then
        if [ "$changed" = 1 ] || [ "$rt_saved" = 1 ]; then root_rw || printf 'ROLLBACK_REMOUNT_FAILED\n'; fi
        if [ "$changed" = 1 ]; then
            while IFS= read -r rel; do
                dst=$task_root/$rel
                # Only this transaction's paths occur in this newly created journal.
                if regular "$dst"; then mv "$dst" "$dst.failed-full040-$tag" || printf 'ROLLBACK_FILE_SAVE_FAILED=%s\n' "$rel"; fi
                if regular "$dst.pre-full040-$tag"; then mv "$dst.pre-full040-$tag" "$dst" || printf 'ROLLBACK_FILE_FAILED=%s\n' "$rel"; fi
            done < "$REPORT/changed-files.txt"
        fi
        if [ "$link_added" = 1 ] && [ -L "$link" ] && [ "$(readlink "$link")" = /etc/systemd/system/nxl-full040-resume.service ]; then
            mv "$link" "$ETC/nxl-full040-link.failed-$tag" || printf 'ROLLBACK_LINK_FAILED\n'
        fi
        if [ "$master_detached" = 1 ]; then
            mv "$ETC/nx-on-wake.disabled-full040-$tag" "$masterwant" || printf 'ROLLBACK_OLD_MASTER_FAILED\n'
        fi
        if [ "$rt_new" = 1 ]; then mv "$RT" "$RT.failed-$tag" || printf 'ROLLBACK_RUNTIME_SAVE_FAILED\n'; fi
        if [ "$rt_saved" = 1 ]; then mv "$RT.previous-$tag" "$RT" || printf 'ROLLBACK_RUNTIME_FAILED\n'; fi
        if [ "$state_new" = 1 ]; then mv "$STATE" "$STATE.failed-$tag" || printf 'ROLLBACK_STATE_SAVE_FAILED\n'; fi
        if [ "$state_saved" = 1 ]; then mv "$STATE.previous-$tag" "$STATE" || printf 'ROLLBACK_STATE_FAILED\n'; fi
        if [ "$changed" = 1 ]; then "$CTL" daemon-reload || printf 'ROLLBACK_RELOAD_FAILED\n'; fi
    fi
    root_back || { rc=1; reason=ROOT_REPROTECT_FAILED; }
    if [ "$locked" = 1 ]; then rmdir "$task_root/run/nxl-full040-rebuild.lock" || :; fi
    if [ "$(cat "$USB" 2>/dev/null)" = 0 ]; then
        printf 'exit=%s\nreason=%s\n' "$rc" "$reason" > "$REPORT/result.txt"
        sync
    fi
    trap - 0
    exit "$rc"
}
[ "$(cat "$USB" 2>/dev/null)" = 0 ] && [ ! -L "$SD/NXDEMO51" ] && mkdir -p "$SD/NXDEMO51" && mkdir "$REPORT" || exit 70
exec > "$REPORT/prepare.log" 2>&1
trap finish 0
trap 'fail INTERRUPTED_BEFORE_HANDOFF' INT TERM
regular "$PKG/request" && [ "$(cat "$PKG/request")" = REBUILD ] || fail WRONG_REQUEST
for f in info.tg nx_cs.adj install.sh; do
    regular "$SD/$f" && "$OPS" same "$SD/$f" "$PKG/activation/$f" || fail TRIGGER_IDENTITY
done
mkdir "$REPORT/entry" || fail TRIGGER_ARCHIVE
for f in info.tg nx_cs.adj install.sh; do mv "$SD/$f" "$REPORT/entry/$f" || fail TRIGGER_ARCHIVE; done
mkdir "$task_root/run/nxl-full040-rebuild.lock" || fail REBUILD_ALREADY_RUNNING
locked=1
printf 'interface=full040-unified-1\noperation=REBUILD\n'
"$OPS" version "$task_root/etc/version.info" || fail WRONG_FIRMWARE
@PAYLOAD_CHECKS@
@STOCK_CHECKS@
case "$(cat "$task_root/sys/power/bootmode")" in 2|3) ;; *) fail NOT_A_NORMAL_INSTALL_BOOT;; esac
before=$(cat "$task_root/proc/sys/kernel/random/boot_id") || fail BOOT_ID_UNAVAILABLE
printf '%s\n' "$before" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' || fail INVALID_BOOT_ID
[ -d "$STATE" ] && [ ! -L "$STATE" ] && [ -d "$RT" ] && [ ! -L "$RT" ] &&
    regular "$STATE/armed" && [ "$(sed -n '2p' "$STATE/armed")" = EYE ] || fail SAME_GENERATION_REQUIRED
@INSTALLED_CHECKS@
absent "$STATE.previous-$tag" && absent "$RT.previous-$tag" || fail BACKUP_PATH_EXISTS
for d in "$task_root/usr" "$task_root/usr/local" "$task_root/usr/local/lib" "$task_root/opt/usr" "$ETC"; do
    [ -d "$d" ] && [ ! -L "$d" ] || fail UNSAFE_INSTALL_PARENT
done
rootdev=$(stat -c %d "$task_root/") && rtdev=$(stat -c %d "$RT") && optdev=$(stat -c %d "$task_root/opt/usr") || fail MOUNT_QUERY
[ "$rootdev" = "$rtdev" ] && [ "$rootdev" != "$optdev" ] || fail RUNTIME_MOUNT_NOT_SEPARATE_FROM_OPT
regular "$task_root/etc/snapshot" && idle || fail NOT_IDLE
mod=$task_root/opt/usr/nx-lut-lite
manifest=$("$OPS" sha256 "$mod/state/module.manifest") || fail MODULE_NOT_INSTALLED
case "$manifest" in @MANIFESTS@) ;; *) fail WRONG_FULL_MODULE;; esac
regular "$mod/state/demo51-installed" && grep -qx 'install_revision=release-0.4.0-rc6.14.12-log117' "$mod/state/demo51-installed" &&
    absent "$mod/state/disabled" || fail WRONG_FULL_MODULE
while read -r wanted file extra; do
    case "$file" in ''|/*|*..*|*\\*) fail INVALID_MODULE_MANIFEST;; esac
    [ -z "$extra" ] && pin "$mod/$file" "$wanted" || fail FULL_MODULE_FILE_MISMATCH
done < "$mod/state/module.manifest"
for conf in "$ETC/di-camera-app.service.d/"*.conf; do
    absent "$conf" && continue
    got=$("$OPS" sha256 "$conf") && grep -Fqx "${conf##*/} $got" "$PKG/allowed-overrides.txt" || fail FOREIGN_CAMERA_OVERRIDE
done
"$CTL" show di-camera-app.service -p ExecStart > "$REPORT/camera-before.txt" &&
    grep -Fq '/usr/local/lib/nxl-full040/launch.sh' "$REPORT/camera-before.txt" || fail CURRENT_LAUNCHER_NOT_SELECTED
for rel in etc/systemd/system/hibernation.service.d etc/systemd/system/hib_post.service.d \
    etc/systemd/system/nxl-full040-resume.service.d; do
    absent "$task_root/$rel" || fail FOREIGN_CAPTURE_OR_RESUME_DROPIN
done
for name in hib_post.service hib_post.path hibernation.service di-camera-app.service; do
    absent "$ETC/$name" || fail FOREIGN_STOCK_UNIT_SHADOW
done
unit=$ETC/nxl-full040-resume.service
absent "$unit" || pin "$unit" @OLD_UNIT@ || fail FOREIGN_RESUME_UNIT
absent "$ETC/hib_post.service.wants" || { [ -d "$ETC/hib_post.service.wants" ] && [ ! -L "$ETC/hib_post.service.wants" ]; } || fail FOREIGN_RESUME_DIRECTORY
link=$ETC/hib_post.service.wants/nxl-full040-resume.service
link_new=0
if absent "$link"; then link_new=1
else [ -L "$link" ] && [ "$(readlink "$link")" = /etc/systemd/system/nxl-full040-resume.service ] || fail FOREIGN_RESUME_LINK; fi
masterwant=$ETC/multi-user.target.wants/nx-on-wake.service
masterunit=$ETC/nx-on-wake.service
masterbin=$task_root/usr/local/bin/nx-on-wake
if absent "$masterunit" && absent "$masterbin" && absent "$masterwant"; then :
else
    pin "$masterunit" @OLD_MASTER_UNIT@ && pin "$masterbin" @OLD_MASTER@ || fail FOREIGN_WAKE_MASTER
    absent "$masterwant" || { [ -L "$masterwant" ] && [ "$(readlink "$masterwant")" = /etc/systemd/system/nx-on-wake.service ]; } || fail FOREIGN_WAKE_MASTER_LINK
fi
capacity=$(cat "$task_root/sys/class/power_supply/battery/capacity") || fail BATTERY_UNREADABLE
case "$capacity" in ''|*[!0-9]*) fail BATTERY_UNREADABLE;; esac
[ "$capacity" -ge 95 ] || fail FULL_BATTERY_REQUIRED
[ -b "$DEV" ] && [ ! -L "$DEV" ] && [ "$(cat "$task_root/sys/class/block/mmcblk0p9/size")" = 307199 ] || fail SNAPSHOT_DEVICE_MISMATCH
free=$("$task_root/bin/df" -Pk "$SD" | awk 'NR==2{print $4}')
case "$free" in ''|*[!0-9]*) fail SD_SPACE_UNAVAILABLE;; esac
[ "$free" -gt 262144 ] || fail INSUFFICIENT_SD_SPACE
# The complete existing 150 MiB image is read, compared and retained before any installation.
"$task_root/bin/dd" if="$DEV" of="$REPORT/mmcblk0p9-before.img.part" bs=1048576 || fail SNAPSHOT_BACKUP_READ
[ "$(stat -c %s "$REPORT/mmcblk0p9-before.img.part")" = 157285888 ] && idle &&
    "$OPS" same "$DEV" "$REPORT/mmcblk0p9-before.img.part" && idle || fail SNAPSHOT_BACKUP_COMPARE
mv "$REPORT/mmcblk0p9-before.img.part" "$REPORT/mmcblk0p9-before.img" || fail SNAPSHOT_BACKUP_PUBLISH
# This helper's sha256 endpoint is capped at 128 MiB. The whole image was
# already compared through EOF by 'same'; the PC computes its unrestricted hash.
printf 'bytes=157285888\nfull_byte_compare_verified=1\nlarge_image_sha256=DEFERRED_TO_PC\nboot_id=%s\n' "$before" > "$REPORT/BACKUP.VERIFIED" || fail BACKUP_RECEIPT
"$task_root/bin/tar" -cpf "$REPORT/startup-before.tar" -C "$task_root/" \
    opt/usr/nxl-full040 usr/local/lib/nxl-full040 etc/systemd/system usr/bin/hibernation.sh usr/bin/make_snapshot.sh || fail CONFIG_BACKUP
"$task_root/bin/tar" -tf "$REPORT/startup-before.tar" > "$REPORT/startup-before.files" || fail CONFIG_BACKUP_READBACK
"$OPS" sha256 "$REPORT/startup-before.tar" > "$REPORT/startup-before.sha256" || fail CONFIG_BACKUP_HASH
sync
idle || fail CAMERA_BECAME_BUSY
root_rw || fail ROOT_REMOUNT_RW
mv "$STATE" "$STATE.previous-$tag" || fail STATE_ARCHIVE
state_saved=1
mkdir "$STATE" || fail STATE_CREATE
state_new=1
for f in fileops.arm claim.arm; do put "$PKG/$f" "$STATE/$f" 755 || fail STATE_INSTALL; done
mv "$RT" "$RT.previous-$tag" || fail RUNTIME_ARCHIVE
rt_saved=1
mkdir "$RT" || fail RUNTIME_CREATE
rt_new=1
chmod 755 "$RT" || fail RUNTIME_CREATE
for f in launch.sh dispatch.sh readout full-menu.sh wake.sh collect-resume.sh fileops.arm verify-before-capture.sh; do
    put "$PKG/$f" "$RT/$f" 755 || fail RUNTIME_INSTALL
done
put "$PKG/nxl-eye-af.so" "$RT/nxl-eye-af.so" 644 || fail EYE_LIBRARY_INSTALL
(set -C; printf '%s\nEYE\n%s\n' "$before" "$leaf" > "$STATE/armed") &&
    put "$STATE/armed" "$RT/capture-origin" 600 || fail ARM_GENERATION
replace_file "$PKG/nxl-full040-resume.service" etc/systemd/system/nxl-full040-resume.service 644 || fail RESUME_SERVICE_INSTALL
if [ "$link_new" = 1 ]; then
    mkdir -p "$ETC/hib_post.service.wants" && ln -s /etc/systemd/system/nxl-full040-resume.service "$link" || fail RESUME_LINK_INSTALL
    link_added=1
fi
replace_file "$PKG/capture-profile.conf" etc/systemd/system/hibernation.service.d/75-nxl-full040-prewrite.conf 644 || fail CAPTURE_GATE_INSTALL
replace_file "$PKG/hibernation.sh" usr/bin/hibernation.sh 755 || fail HEADER_GUARD_INSTALL
replace_file "$PKG/make_snapshot.sh" usr/bin/make_snapshot.sh 755 || fail FACTORY_GUARD_INSTALL
if ! absent "$masterwant"; then
    mv "$masterwant" "$ETC/nx-on-wake.disabled-full040-$tag" || fail OLD_WAKE_MASTER_DETACH
    master_detached=1
fi
"$CTL" daemon-reload || fail MANAGER_RELOAD
"$CTL" --all show hib_post.service -p Wants > "$REPORT/installed-wants.txt" &&
    tr '= ' '\n\n' < "$REPORT/installed-wants.txt" | grep -Fqx nxl-full040-resume.service || fail RESUME_DEPENDENCY_NOT_LOADED
"$CTL" --all show nxl-full040-resume.service -p ExecStart > "$REPORT/installed-entry.txt" &&
    grep -Fq '/usr/local/lib/nxl-full040/wake.sh' "$REPORT/installed-entry.txt" || fail UNIFIED_ENTRY_NOT_SELECTED
root_back || fail ROOT_REPROTECT
"$PKG/nxl-menu" --fast-start-capture || fail NOTICE_NOT_SHOWN
idle || fail CAMERA_BECAME_BUSY_BEFORE_RESCUE
printf 'interface=full040-unified-1\nboot_id=%s\nreport=%s\n' "$before" "$leaf" > "$REPORT/CAPTURE_REQUESTED" || fail DISPATCH_RECEIPT
printf 'exit=DISPATCHING\nreason=OEM_RESCUE_REQUEST\n' > "$REPORT/result.txt" || fail DISPATCH_RECEIPT
sync
handed=1
cd / || fail FINAL_CWD
exec </dev/null >/dev/console 2>&1
exec /bin/sh "$RT/dispatch.sh" "$leaf"
