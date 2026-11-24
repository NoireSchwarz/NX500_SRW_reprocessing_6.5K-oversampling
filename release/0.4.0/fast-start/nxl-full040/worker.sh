#!/bin/sh
set -u
trap '' HUP
umask 077
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LC_ALL=C; export PATH LC_ALL
task_root=
SD=$task_root/mnt/mmc
PKG=$SD/nxl-full040
OPS=$PKG/fileops.arm
STATE=$task_root/opt/usr/nxl-full040
RUNTIME=$task_root/usr/local/lib/nxl-full040
V1=$task_root/opt/usr/nxl-eye-image
SYSTEMCTL=$task_root/usr/bin/systemctl
UNIT=$task_root/etc/systemd/system/nx-on-wake.service
MASTER=$task_root/usr/local/bin/nx-on-wake
WANT=$task_root/etc/systemd/system/multi-user.target.wants/nx-on-wake.service
DROP=$task_root/etc/systemd/system/di-camera-app.service.d
CONF=$DROP/68-nxl-full040.conf
new_conf_sha=3c2a29985fe89416c29b45ee425464e46a181c798c3eb3a45a6d23b70f004bf8
v22_conf_sha=6acb8bede9a44cc89cd14049015eb9f926e04416e4e84facbc1b68a7fb4f2c44
v22_wake_sha=55f0d6022306dd6fd9cac207413e20a1e7076f8cb0c7512fa6ab860504193016
v23_conf_sha=0ff3114fdba7278e517eedf47b39623d7e4742135d6b35617bcac874495a28f3
v23_wake_sha=9a589e922e69812a0905f593fc8c71f226b57f0e2acb297e77de5cc1e77d2bd4
v24_conf_sha=f26abdac599dd0889639afa7a6b6b2434cefe9714987280ec8cc001a908f46d6
v24_wake_sha=3dc969d2171bcb8872d83e3b8284382074551597b83948f68e321fbcbe56a66b
v25_conf_sha=700ed77e70e86624fe1d158dbc77eb61b5a714fc9c756fee849c30246ab1dec1
v25_wake_sha=db34c06e77e570a3a3f403c9454af627d111cad0443be7d927d5974af73ddf4e
v27_conf_sha=32afa1a259b228d71b14cb32438a06023bc7afeed56c8fe91e2735f1865ef1ae
v27_wake_sha=c48be208b3a60691234ae85e918e1e1eeef6b58c9207fddfa6fa688d5e35be52
v26_conf_sha=7fd3ef8f7ad4b2e91e7577766cd424f00ac90ff52ed587d7ee1f161b0411dcb4
v26_wake_sha=209e9792389b4679cf6ca3b2ac134161bca63f85fcb79019f76472de6d48d2b6
ENTRY=$SD/nx-on-wake/on-wake
USB=$task_root/sys/class/extcon/USB/state
ROOTOPS=$task_root/bin/mount
tag=$(date +%s)_$$
REPORT=$SD/NXDEMO51/FAST_START_EYE_AF_ROOTLIB_$tag
reason=UNEXPECTED_EXIT
restore_ro=0
root_writable=0
owned_state=0
owned_conf=0
owned_master=0
readout_armed=0
owned_runtime=0
prior_entry_saved=0

regular() { [ -f "$1" ] && [ ! -L "$1" ] && [ -r "$1" ]; }
absent() { [ ! -e "$1" ] && [ ! -L "$1" ]; }
pin() { regular "$1" && [ "$("$OPS" sha256 "$1")" = "$2" ]; }
v28_wake_sha=d3e7e697640056ee02fea543ebfb26a53c5d607530b83ea064fac19776e1e4f0
v28_conf_sha=c2a59b6af797805d893addc6313e4caf25b196002bc499bd055585d2d84ca3f7
known_prior_wake() { pin "$1" "$v28_wake_sha" || pin "$1" "$v27_wake_sha" || pin "$1" "$v26_wake_sha" || pin "$1" "$v25_wake_sha" || pin "$1" "$v24_wake_sha" || pin "$1" "$v23_wake_sha" || pin "$1" "$v22_wake_sha"; }
matching_prior_service() {
    if pin "$ENTRY" "$v28_wake_sha"; then
        pin "$DROP/67-nxl-eye-publication.conf" "$v28_conf_sha"
        return
    fi
    absent "$DROP/67-nxl-eye-publication.conf" || return 1
    if pin "$ENTRY" "$v27_wake_sha"; then
        pin "$DROP/66-nxl-eye-relock.conf" "$v27_conf_sha"
        return
    fi
    absent "$DROP/66-nxl-eye-relock.conf" || return 1
    if pin "$ENTRY" "$v26_wake_sha"; then
        pin "$DROP/65-nxl-eye-face-return.conf" "$v26_conf_sha"
        return
    fi
    absent "$DROP/65-nxl-eye-face-return.conf" || return 1
    if pin "$ENTRY" "$v25_wake_sha"; then
        pin "$DROP/64-nxl-eye-motion.conf" "$v25_conf_sha"
        return
    fi
    absent "$DROP/64-nxl-eye-motion.conf" || return 1
    if pin "$ENTRY" "$v24_wake_sha"; then
        pin "$DROP/63-nxl-eye-reacquire.conf" "$v24_conf_sha"
        return
    fi
    absent "$DROP/63-nxl-eye-reacquire.conf" || return 1
    if pin "$ENTRY" "$v23_wake_sha"; then
        pin "$DROP/62-nxl-eye-priority.conf" "$v23_conf_sha"
    else
        pin "$ENTRY" "$v22_wake_sha" && pin "$DROP/61-nxl-eye-image.conf" "$v22_conf_sha" &&
            absent "$DROP/62-nxl-eye-priority.conf"
    fi
}
fail() { reason=$1; printf 'STOPPED=%s\n' "$reason"; exit 1; }
install_new() {
    absent "$2" && (set -C; cat "$1" > "$2.new.$$") && chmod "$3" "$2.new.$$" &&
        "$OPS" same "$1" "$2.new.$$" && mv "$2.new.$$" "$2" && "$OPS" same "$1" "$2"
}
unprotect_root() {
    if [ "$root_writable" = 1 ]; then return 0; fi
    mode=$(awk '$2=="/" {n=split($4, flags, ","); for (i=1; i<=n; i++) if (flags[i]=="ro" || flags[i]=="rw") mode=flags[i]} END {print mode}' "$task_root/proc/mounts")
    case "$mode" in
        ro) "$ROOTOPS" -o remount,rw / || return 1; restore_ro=1;;
        rw) :;;
        *) return 1;;
    esac
    root_writable=1
}
reprotect_root() {
    if [ "$restore_ro" = 1 ]; then "$ROOTOPS" -o remount,ro / || return 1; restore_ro=0; fi
    root_writable=0
}
finish() {
    rc=$?
    if [ "$owned_state" = 1 ] && regular "$STATE/armed"; then mv "$STATE/armed" "$STATE/aborted" || :; fi
    if [ "$readout_armed" = 1 ] && "$OPS" same "$ENTRY" "$PKG/wake-entry"; then
        mv "$ENTRY" "$REPORT/readout.not-used" || :
    fi
    if [ "$prior_entry_saved" = 1 ] && absent "$ENTRY" && known_prior_wake "$REPORT/previous-on-wake"; then
        mv "$REPORT/previous-on-wake" "$ENTRY" || printf 'PREVIOUS_WAKE_RESTORE_FAILED\n'
    fi
    if [ "$owned_conf" = 1 ] && "$OPS" same "$CONF" "$PKG/capture.conf"; then
        if unprotect_root; then mv "$CONF" "$CONF.aborted.$tag" || printf 'OVERRIDE_RESTORE_FAILED\n'; fi
    fi
    if [ "$owned_runtime" = 1 ] && unprotect_root && absent "$RUNTIME.aborted.$tag"; then
        mv "$RUNTIME" "$RUNTIME.aborted.$tag" || printf 'RUNTIME_ARCHIVE_FAILED\n'
    fi
    if [ "$owned_master" = 1 ] && unprotect_root; then
        "$SYSTEMCTL" disable nx-on-wake.service || printf 'WAKE_DISABLE_FAILED\n'
        for pair in master unit; do
            case "$pair" in master) dst=$MASTER;; unit) dst=$UNIT;; esac
            if "$OPS" same "$dst" "$PKG/wake-$pair"; then mv "$dst" "$STATE/wake-$pair.aborted" || :; fi
        done
    fi
    if [ "$owned_conf" = 1 ]; then
        "$SYSTEMCTL" daemon-reload || printf 'SERVICE_RELOAD_FAILED\n'
    fi
    reprotect_root || printf 'ROOT_REMOUNT_RO_FAILED\n'
    if [ "$owned_state" = 1 ] && absent "$STATE.aborted.$tag"; then
        mv "$STATE" "$STATE.aborted.$tag" || printf 'STATE_ARCHIVE_FAILED\n'
    fi
    printf 'exit=%s\nreason=%s\n' "$rc" "$reason" > "$REPORT/result.txt"
    sync
}
check_mounts() {
    for p in "$task_root/usr" "$task_root/usr/local" "$task_root/usr/local/lib" "$task_root/opt/usr"; do
        [ -d "$p" ] && [ ! -L "$p" ] || return 1
    done
    root_dev=$(stat -c %d "$task_root/") && runtime_dev=$(stat -c %d "$task_root/usr/local/lib") &&
        opt_dev=$(stat -c %d "$task_root/opt/usr") || return 1
    printf 'root_device=%s\nlibrary_directory_device=%s\nopt_usr_device=%s\n' "$root_dev" "$runtime_dev" "$opt_dev"
    awk '$2=="/" || $2=="/opt" || $2=="/opt/usr" {print}' "$task_root/proc/mounts" > "$REPORT/mounts.txt" || return 1
    [ "$runtime_dev" = "$root_dev" ] && [ "$runtime_dev" != "$opt_dev" ]
}
idle() {
    [ "$(cat "$USB" 2>/dev/null)" = 0 ] &&
        awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {busy=1} END {exit !(ok && !busy)}' "$task_root/proc/swaps" || return 1
    for p in "$task_root/opt/etc/.hib_capturing" "$task_root/run/nxl-eye-af-launch" \
        "$task_root/tmp/nx-lut-lite/gamma-r11h.lease" "$task_root/tmp/nx-lut-lite/gamma-life.pending" \
        "$task_root/tmp/nx-lut-lite/clean-log.policy" "$task_root/tmp/nx-lut-lite/clean-log-command.lock" \
        "$task_root/tmp/nx-lut-lite/video-fullwidth.pending" "$task_root/tmp/nx-lut-lite/engine.lock"; do
        absent "$p" || { printf 'busy=%s\n' "$p"; return 1; }
    done
}

[ "$(cat "$USB" 2>/dev/null)" = 0 ] && [ ! -L "$SD/NXDEMO51" ] && mkdir -p "$SD/NXDEMO51" && mkdir "$REPORT" || exit 70
exec > "$REPORT/prepare.log" 2>&1
trap finish 0
trap 'fail INTERRUPTED_BEFORE_RESCUE' INT TERM
regular "$PKG/request" && IFS= read -r policy < "$PKG/request" || fail REQUEST_ABSENT
case "$policy" in CHECK|CONTROL|EYE|CLEAR|INSPECT) ;; *) fail REQUEST_UNKNOWN;; esac
printf 'NXL_FULL040_EYE28\npolicy=%s\n' "$policy"
for leaf in info.tg nx_cs.adj install.sh; do
    regular "$SD/$leaf" && regular "$PKG/activation/$leaf" && "$OPS" same "$SD/$leaf" "$PKG/activation/$leaf" || fail TRIGGER_IDENTITY
done
mkdir "$REPORT/entry" || fail TRIGGER_ARCHIVE_CREATE
for leaf in info.tg nx_cs.adj install.sh; do mv "$SD/$leaf" "$REPORT/entry/$leaf" || fail TRIGGER_ARCHIVE; done

"$OPS" version "$task_root/etc/version.info" || fail WRONG_FIRMWARE
pin "$SYSTEMCTL" 5ea6197e2d22f87cbf74f34eef289f7cabcfe0b96db92dcb45636ccefaa06d9f && pin "$task_root/usr/bin/hibernation.sh" dd3687d3231ec32bbdcaaf3aac3015842f4ec703269a5eef71213e3e75447361 &&
    pin "$task_root/usr/lib/libhibernation.so" fe10b00aea33234bb144c28f14892d4a0583a1b81d27f070a0e7e0f1b5198e03 &&
    pin "$task_root/usr/lib/systemd/system/di-camera-app.service" 5f7824afd875a7073618401d3e157af1f909530be12f1db7dee179c2a79d7f5c &&
    pin "$task_root/usr/apps/com.samsung.di-camera-app/bin/di-camera-app" 55dcc78143724435a2e38b9e0d809960bd728668634bbcf7532ce137f882eb04 &&
    pin "$task_root/usr/lib/libcapture-fw-prod.so" a07867292610ac6376e29e3cd64bc79a274452412387fb7853c5a3fb3f96bba5 && pin "$task_root/usr/lib/libface.so" f2936a156173d48b1a54a6647da6d169f3bd8276c3d8448eca1f784ddd29047f || fail STOCK_IDENTITIES_CHANGED
core=$("$OPS" sha256 "$task_root/usr/lib/libmmfcore.so") || fail CORE_UNREADABLE
case "$core" in b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8) printf 'core=stock\n';; 7743dd26b06fb2e600cc65aff6ad92913b43c3f75b9493437239006b24991cd9) printf 'core=known_legacy\n';; *) fail CORE_UNRECOGNIZED;; esac
IFS= read -r before < "$task_root/proc/sys/kernel/random/boot_id" && [ -n "$before" ] || fail BOOT_ID_UNAVAILABLE
printf 'boot_id=%s\nbootmode=%s\n' "$before" "$(cat "$task_root/sys/power/bootmode")"
check_mounts || fail RUNTIME_MOUNT_NOT_SEPARATE_FROM_OPT
# Preserve the last launch claim even if recovery restored the original service.
if pin "$V1/launch.sh" a4b9dbe612da3b0be699052bac07b91c92092b73a433a1d5b2b5d549d6fc4e42 && regular "$V1/launch-claim"; then
    cp "$V1/launch-claim" "$REPORT/previous-launch-claim" || fail PREVIOUS_CLAIM_COPY
fi
if regular "$task_root/tmp/var/log/hib.txt"; then
    tail -n 60 "$task_root/tmp/var/log/hib.txt" > "$REPORT/hib-before.txt"
fi
if [ "$policy" = INSPECT ]; then
    if regular "$STATE/menu-status"; then cp "$STATE/menu-status" "$REPORT/full-menu.txt" || fail MENU_RECEIPT_COPY; fi
    if regular "$task_root/tmp/nxl-eye-rootlib-rescue.return"; then
        cp "$task_root/tmp/nxl-eye-rootlib-rescue.return" "$REPORT/rescue-return.txt" || fail DISPATCH_RESULT_COPY
    fi
    if regular "$STATE/launch-claim"; then cp "$STATE/launch-claim" "$REPORT/launch-claim" || fail CLAIM_COPY; fi
    "$SYSTEMCTL" show di-camera-app.service -p MainPID -p ActiveState -p ExecStart > "$REPORT/service.txt" || fail SERVICE_READ
    pid=$(sed -n 's/^MainPID=//p' "$REPORT/service.txt")
    case "$pid" in ''|*[!0-9]*|0) printf 'camera_process=unavailable\n';;
        *) if [ "$(readlink "$task_root/proc/$pid/exe")" = /usr/apps/com.samsung.di-camera-app/bin/di-camera-app ]; then
               awk '$6~/\/nxl-eye-af\.so$/ {print}' "$task_root/proc/$pid/maps" > "$REPORT/eye-maps.txt"
           fi;;
    esac
    dmesg | grep -Ei 'kernel_resume start|hibernat|swsusp|PM: (Image|Writing|Error)' | tail -n 40 > "$REPORT/hibernate-kernel.txt"
    reason=INSPECTION_COMPLETE; exit 0
fi
if [ -e "$DROP" ] || [ -L "$DROP" ]; then
    [ -d "$DROP" ] && [ ! -L "$DROP" ] || fail SERVICE_DIRECTORY_UNSAFE
    for conf in "$DROP"/*.conf; do
        absent "$conf" && continue
        case "${conf##*/}" in
            50-nxl-eye-af.conf) pin "$conf" 714b38ab2d1b7f6841deb0b4686be0959215f4ac822c3ae22f55f43f85fe1c19 || fail FOREIGN_BASE_OVERRIDE;;
            60-nxl-eye-faststart.conf) pin "$conf" 060441c5617b47a98dfbd57095737ce3008649420c11e1babc4fe6ca1939896d || fail FOREIGN_CAPTURE_OVERRIDE;;
            61-nxl-eye-image.conf) pin "$conf" "$v22_conf_sha" || pin "$conf" 8377f036ef07652a0d8c8662bf5e7bc9414233eb60a4cad65d6eda874fcbd4ee || fail FOREIGN_POSTFLASH_OVERRIDE;;
            62-nxl-eye-priority.conf) pin "$conf" "$v23_conf_sha" || fail FOREIGN_PRIORITY_OVERRIDE;;
            63-nxl-eye-reacquire.conf) pin "$conf" "$v24_conf_sha" || fail FOREIGN_REACQUIRE_OVERRIDE;;
            64-nxl-eye-motion.conf) pin "$conf" "$v25_conf_sha" || fail FOREIGN_MOTION_OVERRIDE;;
            65-nxl-eye-face-return.conf) pin "$conf" "$v26_conf_sha" || fail FOREIGN_FACE_RETURN_OVERRIDE;;
            66-nxl-eye-relock.conf) pin "$conf" "$v27_conf_sha" || fail FOREIGN_RELOCK_OVERRIDE;;
            67-nxl-eye-publication.conf) pin "$conf" "$v28_conf_sha" || fail FOREIGN_PUBLICATION_OVERRIDE;;
            68-nxl-full040.conf) pin "$conf" "$new_conf_sha" || fail FOREIGN_PUBLICATION_OVERRIDE;;
            *) fail INDEPENDENT_SERVICE_OVERRIDE;;
        esac
        printf 'existing_service_override=%s\n' "${conf##*/}"
    done
fi
if absent "$MASTER" && absent "$UNIT" && absent "$WANT"; then
    master_new=1
    printf 'wake_master=absent\n'
elif pin "$MASTER" 80fe8ac29bca0e4a2e9f180137b0dad1b71e9f9f4971ee6743740bbc3f85bb66 && pin "$UNIT" af9f1ae1cae3fc18a1cb36145fed59ccf1930f37edb1e2d0e3de54da15472774 && [ -L "$WANT" ] &&
     [ "$(readlink "$WANT")" = /etc/systemd/system/nx-on-wake.service ]; then
    master_new=0
    printf 'wake_master=verified\n'
else
    fail UNRECOGNIZED_OR_PARTIAL_WAKE_MASTER
fi
prev=$task_root/opt/usr/nx-on-wake/eye-af-faststart
if [ "$policy" = CLEAR ]; then
    idle && [ -d "$STATE" ] && [ ! -L "$STATE" ] && [ -d "$RUNTIME" ] && [ ! -L "$RUNTIME" ] &&
        pin "$RUNTIME/launch.sh" 172c7eda83ba6f3e137b97b205b98f8c7eb101244580c41d4b1c8da4505854a6 || fail OWNED_STATE_UNAVAILABLE
    if ! absent "$CONF"; then pin "$CONF" "$new_conf_sha" || fail INDEPENDENT_SERVICE_OVERRIDE; fi
    if ! absent "$ENTRY"; then regular "$ENTRY" && "$OPS" same "$ENTRY" "$PKG/wake-entry" || fail INDEPENDENT_WAKE_ENTRY; fi
    if ! absent "$STATE/previous-on-wake"; then known_prior_wake "$STATE/previous-on-wake" || fail PREVIOUS_WAKE_UNRECOGNIZED; fi
    if regular "$STATE/armed"; then
        mv "$STATE/armed" "$STATE/disarmed.$$" || fail REQUEST_DISARM_FAILED
    fi
    if ! absent "$CONF"; then
        unprotect_root && mv "$CONF" "$CONF.disabled.$tag" || fail CAPTURE_OVERRIDE_REMOVE_FAILED
    fi
    if ! absent "$ENTRY"; then
        mv "$ENTRY" "$REPORT/readout.disabled" || fail INDEPENDENT_WAKE_ENTRY
    fi
    if known_prior_wake "$STATE/previous-on-wake"; then
        install_new "$STATE/previous-on-wake" "$ENTRY" 755 || fail PREVIOUS_WAKE_RESTORE_FAILED
    fi
    unprotect_root && absent "$RUNTIME.disabled.$tag" && mv "$RUNTIME" "$RUNTIME.disabled.$tag" &&
        "$SYSTEMCTL" daemon-reload && reprotect_root || fail RUNTIME_ARCHIVE_FAILED
    absent "$STATE.disabled.$tag" && mv "$STATE" "$STATE.disabled.$tag" || fail PREVIOUS_STATE_ARCHIVE_FAILED
    reason=OWNED_CAPTURE_DISABLED
    printf 'CAPTURE_DISABLED\n'
    exit 0
fi
for p in "$prev/pending" "$prev/retry/armed"; do absent "$p" || fail PREVIOUS_REQUEST_ACTIVE; done
if ! absent "$prev/guarded/armed"; then [ -d "$prev/guarded/capture-claimed" ] || fail PREVIOUS_REQUEST_ACTIVE; fi
absent "$STATE" && absent "$RUNTIME" && absent "$SD/nx-lut-demo/eye-af-bt-request" || fail EXISTING_CAPTURE_STATE
if ! absent "$ENTRY"; then
    matching_prior_service || fail INDEPENDENT_WAKE_ENTRY
fi
absent "$CONF" || fail EXISTING_CAPTURE_OVERRIDE
[ -e "$task_root/etc/snapshot" ] && idle || fail CAMERA_NOT_IDLE_OR_SNAPSHOT_UNAVAILABLE
mod=$task_root/opt/usr/nx-lut-lite
    actual_manifest=$("$OPS" sha256 "$mod/state/module.manifest") || fail FULL_MODULE_INSTALL_REQUIRED
    case "$actual_manifest" in e3520acc8a3adf788cf6bf1a5a9a927ee55ab5fd6cf296ffc35ca79286baa167|a9c5391ec02b77943a494f68d929ec6fbfe8b5d51ac36fe648ca5f49f1e606c3) ;; *) fail FULL_MODULE_INSTALL_REQUIRED;; esac
    regular "$mod/state/module.manifest" &&
        regular "$mod/state/demo51-installed" &&
        grep -qx 'install_revision=release-0.4.0-rc6.14.12-log117' "$mod/state/demo51-installed" ||
        fail FULL_MODULE_INSTALL_REQUIRED
    while read -r wanted file extra; do
        [ -z "$extra" ] && pin "$mod/$file" "$wanted" || fail FULL_MODULE_FILE_MISMATCH
    done < "$mod/state/module.manifest"
    absent "$mod/state/disabled" || fail FULL_MODULE_DISABLED
    printf 'full_module_manifest=%s\n' "$actual_manifest"
if [ "$policy" = CHECK ]; then reason=CHECKED_READY; printf 'PREFLIGHT_READY\n'; exit 0; fi
[ -d "$SD/nx-on-wake" ] && [ ! -L "$SD/nx-on-wake" ] || fail SD_WAKE_DIRECTORY_ABSENT
if [ -r "$task_root/sys/class/power_supply/battery/capacity" ]; then
    IFS= read -r capacity < "$task_root/sys/class/power_supply/battery/capacity" || fail BATTERY_STATUS_UNREADABLE
    case "$capacity" in ''|*[!0-9]*) fail BATTERY_STATUS_UNREADABLE;; esac
    [ "$capacity" -ge 95 ] || fail FULL_BATTERY_REQUIRED
fi
pin "$PKG/claim.arm" f5702a02ae73f2b375f3ee8ff30417e2c952631ff8c9f8fd3a5d9a78acc79036 || fail PAYLOAD_CLAIM_ARM
pin "$PKG/loader-observe.sh" 2481e6baf564393ed4eb91f4043ca266572ce81de6451a85aad10ec4cfef3dd7 || fail PAYLOAD_LOADER_OBSERVE_SH
pin "$PKG/nxl-eye-af.so" e167e84a6be88b2fc21da13a5952a923ec7a032cb37612a11e047e60dc6c2e6e || fail PAYLOAD_NXL_EYE_AF_SO
pin "$PKG/nxl-menu" 493010e458d81073e69f8634ec88ea1f03ba0ee910da17a90fae70bb82abbe57 || fail PAYLOAD_NXL_MENU
pin "$PKG/wake-master" 80fe8ac29bca0e4a2e9f180137b0dad1b71e9f9f4971ee6743740bbc3f85bb66 || fail PAYLOAD_WAKE_MASTER
pin "$PKG/wake-unit" af9f1ae1cae3fc18a1cb36145fed59ccf1930f37edb1e2d0e3de54da15472774 || fail PAYLOAD_WAKE_UNIT
pin "$PKG/wake-entry" 9b3154abf884f18b4e3b7009b2a00385b01c9ed8cbab6ead4a517f5b5bb61a18 || fail PAYLOAD_WAKE_ENTRY
pin "$PKG/launch.sh" 172c7eda83ba6f3e137b97b205b98f8c7eb101244580c41d4b1c8da4505854a6 || fail PAYLOAD_LAUNCH_SH
pin "$PKG/dispatch.sh" 3fa3edf734664df4a038438321b95668a6fbf95d74877bdccdd99302eae6baed || fail PAYLOAD_DISPATCH_SH
pin "$PKG/readout" bded257edb584f914f0dc2a0d9ea5f84949e2cc89b19ce31d2975ba5e9342bc0 || fail PAYLOAD_READOUT
pin "$PKG/capture.conf" 3c2a29985fe89416c29b45ee425464e46a181c798c3eb3a45a6d23b70f004bf8 || fail PAYLOAD_CAPTURE_CONF
pin "$PKG/lcd-observe.arm" fafbce9b63da331e62a47bd3952a11fbe9638dd37f8305647f3297755ff7cd93 || fail PAYLOAD_LCD_OBSERVE_ARM
pin "$PKG/full-menu.sh" 0655ae8a221a71e8a684de78a9df12c59bc7ae301b090bb1c145e95fa53563c4 || fail FULL_MENU_IDENTITY
stock_sleep=$(readlink -f "$task_root/bin/sleep") && pin "$stock_sleep" 8adde8f159709200cac6a4731c55438d343164ad0327b1fbc85c1c3b52ae9d16 || fail STOCK_SLEEP_IDENTITY
idle || fail CAMERA_BECAME_BUSY
mkdir "$STATE" || fail STATE_CREATE
owned_state=1
for leaf in fileops.arm claim.arm; do install_new "$PKG/$leaf" "$STATE/$leaf" 755 || fail STATE_INSTALL; done
unprotect_root || fail ROOT_REMOUNT_RW
mkdir "$RUNTIME" || fail ROOT_RUNTIME_CREATE
owned_runtime=1
chmod 755 "$RUNTIME" || fail ROOT_RUNTIME_MODE
for leaf in launch.sh readout dispatch.sh full-menu.sh; do install_new "$PKG/$leaf" "$RUNTIME/$leaf" 755 || fail ROOT_RUNTIME_INSTALL; done
mapped=$task_root/usr/apps/com.samsung.di-camera-app/bin/di-camera-app
if [ "$policy" = EYE ]; then
    install_new "$PKG/nxl-eye-af.so" "$RUNTIME/nxl-eye-af.so" 644 &&
        [ "$(stat -c %d "$RUNTIME/nxl-eye-af.so")" = "$root_dev" ] || fail ROOT_LIBRARY_INSTALL
    mapped=$RUNTIME/nxl-eye-af.so
fi
# Exercise the same claim binary on the actual user partition without consuming the launch token.
mkdir "$STATE/preflight" &&
    "$STATE/claim.arm" "$STATE/preflight" "$before" preflight "$$" "$policy" "$mapped" &&
    cp "$STATE/preflight/launch-claim" "$REPORT/claim-check.txt" || fail CLAIM_PREFLIGHT_FAILED
if [ "$policy" = EYE ]; then
    /bin/sh "$PKG/loader-observe.sh" "$mapped" "$REPORT/claim-check.txt" > "$REPORT/loader-check.txt" 2>&1
    loader_rc=$?
    printf 'observer_exit=%s\n' "$loader_rc" >> "$REPORT/loader-check.txt"
    [ "$loader_rc" = 0 ] || fail STOCK_PROCESS_LIBRARY_CHECK_FAILED
fi
if [ "$master_new" = 1 ]; then
    owned_master=1
    install_new "$PKG/wake-master" "$MASTER" 755 && install_new "$PKG/wake-unit" "$UNIT" 644 &&
        "$SYSTEMCTL" enable nx-on-wake.service || fail MASTER_SETUP
fi
mkdir -p "$DROP" || fail SERVICE_DIRECTORY_CREATE
owned_conf=1
install_new "$PKG/capture.conf" "$CONF" 644 || fail CAPTURE_OVERRIDE_INSTALL
"$SYSTEMCTL" daemon-reload && "$SYSTEMCTL" show di-camera-app.service -p ExecStart > "$REPORT/service.txt" &&
    grep -Fq '/usr/local/lib/nxl-full040/launch.sh' "$REPORT/service.txt" || fail ROOT_LAUNCHER_NOT_SELECTED
reprotect_root || fail ROOT_REMOUNT_RO
if ! absent "$ENTRY"; then
    matching_prior_service && install_new "$ENTRY" "$STATE/previous-on-wake" 755 &&
        mv "$ENTRY" "$REPORT/previous-on-wake" || fail PREVIOUS_WAKE_PRESERVE_FAILED
    prior_entry_saved=1
fi
install_new "$PKG/wake-entry" "$ENTRY" 755 || fail READOUT_INSTALL
readout_armed=1
(set -C; printf '%s\n%s\n%s\n' "$before" "$policy" "${REPORT##*/}" > "$STATE/armed") || fail REQUEST_ARM
sync
"$PKG/nxl-menu" --fast-start-capture || fail NOTICE_NOT_SHOWN
idle || fail CAMERA_BECAME_BUSY_BEFORE_RESCUE
printf 'RESCUE_DISPATCHING\n' > "$REPORT/CAPTURE_REQUESTED" || fail SD_JOURNAL_WRITE
printf 'exit=DISPATCHING\nreason=OEM_RESCUE_REQUEST\n' > "$REPORT/result.txt" || fail SD_RESULT_WRITE
sync
cd / || fail FINAL_CWD
exec </dev/null >/dev/console 2>&1
exec /bin/sh "$RUNTIME/dispatch.sh" "${REPORT##*/}"
