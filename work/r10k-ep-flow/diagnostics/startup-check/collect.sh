#!/bin/sh
# NXL_STARTUP_READONLY_CHECK1. Read internal files; write only a new SD report.
set -u
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077
SYS=
SD=$SYS/mnt/mmc
PKG=$SD/nx-lut-startup-check
OPS=$SD/nx-lut-demo/patch-fileops
STORE=$SD/NXDEMO51
[ -d "$STORE" ] && [ ! -L "$STORE" ] || exit 70
stamp=$(date +%s) || exit 70
base=$STORE/STARTUP_CHECK_${stamp}_$$
report=$base
attempt=0
while ! mkdir "$report" 2>/dev/null;do
    attempt=$((attempt+1));[ "$attempt" -lt 128 ] || exit 71
    report=$base.$attempt
done
exec >"$report/diagnostic.log" 2>&1
printf 'NXL_STARTUP_READONLY_CHECK1\ninternal_writes=0\nreport=%s\n' "$report"
snapshot_file(){
    sf=$1;out=$2
    printf 'READ path=%s\n' "$sf"
    if [ -r "$sf" ];then cat "$sf" >"$report/$out";printf 'READ_EXIT=%s\n' "$?"
    else printf 'UNAVAILABLE path=%s\n' "$sf";fi
}
for item in version cmdline mounts uptime;do
    case "$item" in version) file=$SYS/proc/version;;*) file=$SYS/proc/$item;;esac
    snapshot_file "$file" "$item.txt"
done
snapshot_file "$SYS/proc/self/mountinfo" mountinfo.txt
snapshot_file "$SYS/etc/version.info" firmware-version.txt
snapshot_file "$SYS/proc/sys/kernel/random/boot_id" boot-id.txt
for item in bootmode noresume resume state disk image_size;do snapshot_file "$SYS/sys/power/$item" "power-$item.txt";done
for marker in "$SYS/etc/snapshot" "$SYS/tmp/.hib_ready" "$SYS/opt/etc/.hib_capturing";do
    printf 'BOOT_MARKER path=%s exists=' "$marker"
    if [ -e "$marker" ] || [ -L "$marker" ];then printf '1\n';ls -lidn "$marker";else printf '0\n';fi
done
for name in libmmfcore.so libmmfmodules.so libmmfosal.so;do
    path=$SYS/usr/lib/$name
    printf 'LIBRARY_BEGIN name=%s\n' "$name"
    ls -lidn "$path" || :
    if command -v stat >/dev/null 2>&1;then
        stat -c 'link dev=%d inode=%i links=%h bytes=%s mode=%f type=%F' "$path" || :
        stat -L -c 'target dev=%d inode=%i links=%h bytes=%s mode=%f type=%F' "$path" || :
    fi
    resolved=$(readlink -f "$path" 2>/dev/null) || resolved=
    printf 'resolved=%s\n' "$resolved"
    case "$resolved" in "$SYS"/usr/lib/*)
        digest=$("$OPS" sha256 "$resolved") || digest=UNAVAILABLE
        printf 'sha256=%s\n' "$digest"
        size=$(stat -L -c %s "$resolved" 2>/dev/null) || size=
        case "$size" in ''|*[!0-9]*) :;;*)
            if [ "$size" -le 1048576 ] && [ "$digest" != UNAVAILABLE ] && [ -f "$resolved" ] && [ ! -L "$resolved" ];then
                cat "$resolved" >"$report/current-$name"
                printf 'copy_exit=%s\n' "$?"
            fi;;
        esac;;
        *) printf 'COPY_SKIPPED unresolved_or_outside_library_directory=1\n';;
    esac
    if command -v getfattr >/dev/null 2>&1;then getfattr --absolute-names -d -m - "$path" || :;fi
    printf 'LIBRARY_END name=%s\n' "$name"
done
printf 'INSTALLED_COMPONENTS_BEGIN\n'
for leaf in state/module.manifest MODULE.id bin/nxl-video-probe.arm video-probe.sh memory-test.sh bin/nxl-menu;do
    path=$SYS/opt/usr/nx-lut-lite/$leaf
    printf 'INSTALLED_COMPONENT leaf=%s\n' "$leaf"
    ls -lidn "$path" || :
    "$OPS" sha256 "$path" || :
    case "$leaf" in state/module.manifest|MODULE.id|video-probe.sh|memory-test.sh)
        [ ! -L "$path" ] && [ -f "$path" ] && snapshot_file "$path" "installed-$(basename "$leaf")";;
    esac
done
current=$SYS/dev/shm/nx-lut-lite/video-probe.current
if [ -f "$current" ] && [ ! -L "$current" ];then
    snapshot_file "$current" ram-current.txt
    while IFS= read -r line;do
        case "$line" in /dev/shm/nx-lut-lite/video-buffer-*)
            suffix=${line#/dev/shm/nx-lut-lite/video-buffer-}
            case "$suffix" in ''|*[!0-9a-f-]*) continue;;esac
            path=$SYS$line
            if [ -f "$path" ] && [ ! -L "$path" ] && command -v head >/dev/null 2>&1;then head -c 16384 "$path" >"$report/ram-buffer-header.bin";fi;;
        esac
    done <"$current"
fi
if command -v find >/dev/null 2>&1 && [ -d "$SYS/tmp/nx-lut-lite" ];then
    find "$SYS/tmp/nx-lut-lite" -maxdepth 5 -type f \( -name nxl-video-probe.arm -o -name nxl-menu -o -name nxl-session.arm \) -print >"$report/private-program-paths.txt"
    while IFS= read -r path;do printf 'PRIVATE_PROGRAM path=%s\n' "$path";ls -lidn "$path" || :;"$OPS" sha256 "$path" || :;done <"$report/private-program-paths.txt"
fi
printf 'INSTALLED_COMPONENTS_END\n'
printf 'CORE_ALIASES_BEGIN\n'
for path in "$SYS"/usr/lib/libmmfcore.so*;do
    [ -e "$path" ] || [ -L "$path" ] || continue
    ls -lidn "$path" || :
    if command -v stat >/dev/null 2>&1;then stat -c 'alias dev=%d inode=%i links=%h bytes=%s mode=%f name=%n' "$path" || :;fi
    "$OPS" sha256 "$path" || :
done
printf 'CORE_ALIASES_END\n'
for path in "$SYS/etc/ld.so.cache" "$SYS/etc/ld.so.preload";do
    [ -f "$path" ] && [ ! -L "$path" ] || continue
    size=$(stat -c %s "$path" 2>/dev/null) || size=
    case "$size" in ''|*[!0-9]*) :;;*)
        if [ "$size" -le 1048576 ];then snapshot_file "$path" "$(basename "$path")";fi;;
    esac
done
found=0
for dir in "$SYS"/proc/[0-9]*;do
    [ -d "$dir" ] || continue
    exe=$(readlink "$dir/exe" 2>/dev/null) || continue
    case "$exe" in */di-camera-app|*/di-camera-app\ \(deleted\)) :;;*) continue;;esac
    pid=${dir##*/};case "$pid" in ''|*[!0-9]*) continue;;esac
    found=$((found+1))
    printf 'CAMERA pid=%s exe=%s\n' "$pid" "$exe"
    snapshot_file "$dir/maps" "camera-$pid-maps.txt"
    snapshot_file "$dir/stat" "camera-$pid-stat.txt"
    snapshot_file "$dir/mountinfo" "camera-$pid-mountinfo.txt"
    printf 'CAMERA_ROOT pid=%s target=' "$pid";readlink "$dir/root" || :
    for name in libmmfcore.so libmmfmodules.so libmmfosal.so;do
        visible=$dir/root/usr/lib/$name
        printf 'CAMERA_VISIBLE_LIBRARY pid=%s name=%s\n' "$pid" "$name"
        stat -L -c 'visible dev=%d inode=%i links=%h bytes=%s mode=%f' "$visible" || :
        "$OPS" sha256 "$visible" || :
        size=$(stat -L -c %s "$visible" 2>/dev/null) || size=
        case "$size" in ''|*[!0-9]*) :;;*)
            if [ "$size" -le 1048576 ] && [ -f "$visible" ] && [ ! -L "$visible" ];then cat "$visible" >"$report/camera-$pid-visible-$name";fi;;
        esac
    done
    # map_files opens the file backing an existing mapping, without reading or
    # writing process memory. Missing/denied kernel support stays explicit.
    while IFS=' ' read -r range permissions offset device inode mapped rest;do
        case "$mapped" in */libmmfcore.so*) :;;*) continue;;esac
        [ "$offset" = 00000000 ] || continue
        case "$range" in ''|*[!0-9a-fA-F-]*) continue;;esac
        backing=$dir/map_files/$range
        printf 'MAPPED_LIBRARY pid=%s range=%s inode=%s link=' "$pid" "$range" "$inode"
        readlink "$backing" || :
        stat -L -c 'mapped dev=%d inode=%i links=%h bytes=%s mode=%f' "$backing" || :
        size=$(stat -L -c %s "$backing" 2>/dev/null) || size=
        case "$size" in ''|*[!0-9]*) :;;*)
            if [ "$size" -le 1048576 ];then cat "$backing" >"$report/camera-$pid-mapped-libmmfcore.so";printf 'MAPPED_COPY_EXIT=%s\n' "$?";fi;;
        esac
        break
    done <"$dir/maps"
    if [ -r "$dir/environ" ];then tr '\000' '\n' <"$dir/environ" | grep -E '^(LD_|HIB|FAST|BOOT)' >"$report/camera-$pid-loader-environment.txt" || :;fi
done
printf 'CAMERA_MATCHES=%s\n' "$found"
if command -v dmesg >/dev/null 2>&1;then dmesg | tail -n 512 >"$report/kernel-tail.txt" || :;fi
# Preserve the consumed, hash-matched activation files in this new report.
# No recursive delete and no internal filesystem/snapshot/boot changes.
while IFS=' ' read -r want leaf extra;do
    [ -z "${extra:-}" ] && [ "${#want}" = 64 ] || continue
    case "$want" in *[!0-9a-f]*) continue;;esac
    case "$leaf" in info.tg|nx_cs.adj|install.sh) :;;*) continue;;esac
    [ -f "$SD/$leaf" ] && [ ! -L "$SD/$leaf" ] || continue
    actual=$("$OPS" sha256 "$SD/$leaf") || continue
    if [ "$actual" = "$want" ];then
        mv "$SD/$leaf" "$report/activation-$leaf" && printf 'ACTIVATION_ARCHIVED=%s\n' "$leaf"
    else printf 'ACTIVATION_PRESERVED_CHANGED=%s\n' "$leaf";fi
done <"$PKG/activation.sha256"
printf 'COLLECTION_FINISHED internal_writes=0 experiment_started=0\n'
exec >/dev/null 2>&1
for file in "$report"/*;do
    [ -f "$file" ] && [ ! -L "$file" ] || continue
    case "${file##*/}" in REPORT.sha256|DONE) continue;;esac
    digest=$("$OPS" sha256 "$file") || exit 72
    printf '%s %s\n' "$digest" "${file##*/}"
done >"$report/REPORT.sha256"
printf 'NXL_STARTUP_READONLY_CHECK1\ncomplete=1\ninternal_writes=0\nexperiment_started=0\n' >"$report/DONE"
sync
exit 0
