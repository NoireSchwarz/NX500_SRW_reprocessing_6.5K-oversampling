#!/bin/sh
# One fresh-boot library transaction; original R11f2 retained-inode protocol.
set -eu
SYS=
H=$SYS/opt/usr/nxl-r11f7-activation
. "$H/common.sh"
cd "$ROOT"
verify_payload || exit 70
case "${1-}" in install|uninstall) ACTION=$1;;*) exit 71;;esac
exec >> "$STATE/commit-$ACTION.log" 2>&1
printf 'R11F7_NATIVE_TRANSACTION action=%s\n' "$ACTION"
task_root=$SYS
SRC=$H/module
transaction_suffix=r11f7.$(date +%s).$$
WORK=$STATE/transaction-$transaction_suffix
mkdir "$WORK"
for suffix in new before staged restore;do [ ! -e "$SYS/usr/lib/libmmfcore.so.nxl-$suffix.$transaction_suffix" ] || exit 72;done
success=0
hash(){ sha "$1"; }
die(){ printf 'R11F7_COMMIT_ERROR=%s\n' "$1";exit 73; }
mount_rw(){
 info=$(root_mode) || die MOUNT_IDENTITY_UNAVAILABLE
 set -- $info
 [ "$#" = 2 ] && [ "$1" = "$ROOT" ] && [ "$2" = ro ] || die ROOT_NOT_READ_ONLY
 mount -o remount,rw "$ROOT" || die REMOUNT_RW_FAILED
}
. "$H/module/hevc-gate-install.sh"
cleanup(){
 rc=$?;trap - EXIT HUP INT TERM
 if [ "$success" != 1 ];then
  if ! hg_native_recover;then printf 'R11F7_ROLLBACK_FAILED=1\n';rc=75;fi
 fi
 if ! mount -o remount,ro "$ROOT";then printf 'R11F7_RESTORE_RO_FAILED=1\n';rc=76;fi
 sync
 exit "$rc"
}
trap cleanup EXIT
trap 'exit 74' HUP INT TERM
hg_native_preflight
hg_native_prepare
hg_native_commit
mount -o remount,ro "$ROOT"
sync
success=1
printf 'R11F7_FILE_TRANSACTION_COMMITTED=1; live_activation_not_proven=1\n'
