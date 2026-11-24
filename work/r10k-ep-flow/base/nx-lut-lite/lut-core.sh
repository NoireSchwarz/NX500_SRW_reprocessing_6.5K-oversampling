#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# NX_LUT_DYNAMIC24_V1
# NX_LUT_STRENGTH_PRESETS_V1
# Derived from metol scripts/lut.sh. Live preview + JPEG via A18 and
# View4/Still6/BV12. Preserve OLED preference; retain read-only Fn tracing.
set -u
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
LAB=${NX_LUT_LOOKS:-/mnt/mmc/looks}
RUN=/tmp/nx-lut-lite
ST=$RUN/session
PD=$D/state
SLOT=0x81119F00
SW=$D/bin/nx_slotwrite.arm
RW=$D/bin/nx_regtabwrite.arm
CV=$D/bin/cube2lut.arm
VC=$D/bin/nxl-tablecheck.arm
OPS=$D/bin/nxl-fileops
IDLE=$D/bin/nxl-lutidle.arm
GAMMA=$D/bin/nxl-gamma-reload.arm
CATALOG=$D/data/release-lut-allowlist.tsv
LUT_MODE=STD
LOAD=$D/bin/nxl-lutload.arm
LUT_LOAD_SHA=e1cc784e405e849c11fa99ee5f1fbd19616ac595c7c3ed722cf7fbda619cf401
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
LUT_IDLE_SHA=da2ba4416c36dbf1fa823edf70d019ef79f838df6e48da427ea5ed708ce327b7
LOG_LUT_POLICY_SHA=ce526682cb48f41fe98b89e0af180e2c5fc4e08502d4b12049059aeb725f8586
LUT_CATALOG_SHA=419ae486255df15850284e19eb9c25e2b3d2580d2b221ae01ab18ab13b262518
BLEND=$D/bin/nxl-lutblend.arm
BLEND_SHA=acb74f412adf89da4008fc58ac209120d72e940d29a703a0f8d7fd5c4754e0f4
SHARP=$D/bin/nxl-sharp.arm
SHARP_SHA=f4207bc4ac86c338aabebfafccfbffe7420d5003b2d53677960a347a8c42949d
die() { echo "ERR $*" >&2; exit 1; }
busy() { echo 'ERR backend busy' >&2; exit 75; }
valid_name() { case "$1" in ''|*[!A-Za-z0-9_-]*) return 1;; esac; [ ${#1} -le 80 ]; }
list() {
 [ -f "$CATALOG" ] && [ ! -L "$CATALOG" ] && [ "$("$OPS" sha256 "$CATALOG")" = "$LUT_CATALOG_SHA" ] || die LUT_CATALOG_IDENTITY_FAILED
 while IFS="$(printf '\t')" read -r id input checksum;do
  valid_name "$id" && [ -f "$LAB/$id.bin" ] && printf '%s\n' "$id"
 done < "$CATALOG"
}

case "${1:-status}" in
list) list; exit 0;;
trace) exec /bin/sh "$D/trace.sh";;
esac
read_version() {
    { IFS= read -r ver; IFS= read -r model; } < /etc/version.info || return 1
    [ "$ver" = 1.12 ] && [ "$model" = NX500 ]
}
read_version || die 'requires NX500 firmware 1.12'
[ -x "$SW" ] && [ -x "$RW" ] || die 'missing ARM LUT tools'
mkdir -p "$RUN" || die 'cannot create runtime directory'
lock=$RUN/engine.lock
if ! mkdir "$lock" 2>/dev/null; then
    if [ -r "$lock/pid" ]; then
        IFS= read -r old < "$lock/pid"
        case "$old" in ''|*[!0-9]*) die 'invalid lock; reboot to clear';; esac
        if ! kill -0 "$old" 2>/dev/null; then
            rm -f "$lock/pid"
            rmdir "$lock" 2>/dev/null || die 'cannot clear stale lock'
            mkdir "$lock" 2>/dev/null || busy
        else busy; fi
    else busy; fi
fi
echo $$ > "$lock/pid"
rollback=0
session_jpeg_only() {
    [ -r "$ST/pipeline" ] || return 1
    IFS= read -r pipeline < "$ST/pipeline"
    [ "$pipeline" = jpeg-only-v1 ]
}
session_supported() {
    [ -r "$ST/pipeline" ] || return 1
    IFS= read -r pipeline < "$ST/pipeline"
    case "$pipeline" in jpeg-only-v1|view-jpeg-v2) return 0;; *) return 1;; esac
}
oled_restore_factory() {
    [ -f "$PD/oled_orig" ] || return 0
    IFS= read -r value < "$PD/oled_orig"
    case "$value" in ''|*[!0-9]*) return 1;; esac
    prefman set 0 0x0a704 l "$value" >/dev/null 2>&1
}
video_restore() {
    if [ -f "$ST/video_enabled" ] || [ -f "$ST/video_pending" ]; then
        "$RW" dyn-restore > "$ST/off-video.log" 2>&1 || {
            echo 'ERR video pointer restore failed; keep session backups' >&2;return 1;
        }
        rm -f "$ST/video_enabled" "$ST/video_pending"
    fi
}
video_enable() {
    [ -f "$ST/ready" ] && [ -r "$ST/current" ] && [ -r "$ST/expected.bin" ] \
        || die 'apply a LUT first, then enable the video trial in standby'
    session_supported || die 'unsupported session; Restore LUT first'
    if [ -f "$ST/video_enabled" ]; then
        "$RW" dyn-check > "$ST/video-check.log" 2>&1 || die 'video pointers changed; Restore LUT first'
        echo 'OK VIDEO_TRIAL_ON';return 0
    fi
    [ ! -f "$ST/video_pending" ] || die 'video transition incomplete; Restore LUT first'
    "$SW" dry "$SLOT" "$ST/expected.bin" > "$ST/video-source.log" 2>&1 \
        || die 'cannot verify current LUT before video trial'
    grep -q '^SOURCE differs from current slot bytes = 0$' "$ST/video-source.log" \
        || die 'current slot differs from applied LUT; apply again before video trial'
    "$RW" dyn-save > "$ST/video-backup.log" 2>&1 || die 'video selector not recognized; no pointer write performed'
    : > "$ST/video_pending" || die 'cannot stage video rollback marker'
    rollback=1
    "$RW" dyn-write "$SLOT" > "$ST/video-write.log" 2>&1 || die 'video pointer write failed; restoring session'
    mv "$ST/video_pending" "$ST/video_enabled" || die 'cannot commit video trial marker'
    rollback=0
    echo 'OK VIDEO_TRIAL_ON; start a NEW recording to test; stop recording before changing LUT'
}
sharp_tools() {
    [ -f "$SHARP" ] && [ ! -L "$SHARP" ] && [ -x "$SHARP" ] || return 1
    h=$(sha256sum "$SHARP") && [ "${h%% *}" = "$SHARP_SHA" ] || return 1
    h=$(sha256sum /usr/lib/libcapture-fw-prod.so) && [ "${h%% *}" = "a07867292610ac6376e29e3cd64bc79a274452412387fb7853c5a3fb3f96bba5" ] || return 1
    h=$(sha256sum /usr/lib/libmulticore-bridge.so) && [ "${h%% *}" = "f14a12e4d9172989d73c8dff0fd59580c65ccd1972264c9d94c8d1a736820390" ] || return 1
}
sharp_read() {
    st cap capdtm varlist > "$ST/param-sharp-read.txt" 2>&1 || return 1
    value=$(awk -F'|' '/VARIABLE_PWSHARPNESS/ {n++; x=$4; gsub(/ /,"",x);gsub(/\r/,"",x);sub(/^0x/,"",x); print tolower(x)} END {if(n!=1)exit 1}' "$ST/param-sharp-read.txt") || return 1
    [ "${#value}" = 8 ] || return 1
    case "$value" in *[!0-9a-f]*) return 1;;esac
    printf '%s\n' "$value"
}
sharp_prepare() {
    sharp_target=;sharp_original=
    if [ "$sharpness" = 0 ] && [ ! -f "$ST/param-sharp.orig" ];then return 0;fi
    [ ! -f "$ST/param-sharp.pending" ] || die 'sharpness submission not verified; restore or restart before changing'
    sharp_tools || die 'sharpness tools/stock libraries changed'
    sharp_live=$(sharp_read) || die 'cannot read live sharpness'
    if [ -f "$ST/param-sharp.orig" ];then
        IFS= read -r sharp_original < "$ST/param-sharp.orig" || die 'sharpness baseline unreadable'
        IFS= read -r sharp_previous < "$ST/param-sharp.current" || die 'sharpness receipt missing'
        [ "$sharp_live" = "$sharp_previous" ] || die 'sharpness changed outside this panel'
    else sharp_original=$sharp_live;fi
    [ "${#sharp_original}" = 8 ] || die 'sharpness baseline invalid'
    case "$sharp_original" in *[!0-9a-f]*) die 'sharpness baseline invalid';;esac
    sharp_word=$((0x$sharp_original));[ $((sharp_word & 65535)) = 55306 ] || die 'sharpness encoding unsupported'
    sharp_raw=$((sharp_word >> 16));[ "$sharp_raw" -lt 32768 ] || sharp_raw=$((sharp_raw-65536))
    [ $(((sharp_raw-15)%16)) = 0 ] || die 'sharpness encoding unsupported'
    sharp_index=$((10+(sharp_raw-15)/16));[ "$sharp_index" -ge 0 ] && [ "$sharp_index" -le 20 ] || die 'sharpness range unsupported'
    sharp_index=$((sharp_index+sharpness));[ "$sharp_index" -ge 0 ] || sharp_index=0;[ "$sharp_index" -le 20 ] || sharp_index=20
    sharp_target=$(printf '%04xd80a' $(((sharp_index*16-145)&65535)))
}
sharp_apply() {
    [ -n "$sharp_target" ] || return 0
    if [ ! -f "$ST/param-sharp.orig" ];then printf '%s\n' "$sharp_original" > "$ST/param-sharp.orig" || return 1;fi
    if [ "$sharp_live" != "$sharp_target" ];then
        printf '%s\n' "$sharp_target" > "$ST/param-sharp.pending" || return 1
        LD_LIBRARY_PATH=/usr/lib:/lib LD_PRELOAD= "$SHARP" "$sharp_target" > "$ST/param-sharp-send.txt" 2>&1 || return 1
        observed=$(sharp_read) && [ "$observed" = "$sharp_target" ] || return 1
    fi
    printf '%s\n' "$sharp_target" > "$ST/param-sharp.current" || return 1
    rm -f "$ST/param-sharp.pending";return 0
}
sharp_restore() {
    [ -f "$ST/param-sharp.orig" ] || return 0
    sharp_tools || return 1
    IFS= read -r original < "$ST/param-sharp.orig" || return 1
    [ "${#original}" = 8 ] || return 1
    case "$original" in *[!0-9a-f]*) return 1;;esac
    current=$(sharp_read) || return 1
    if [ -f "$ST/param-sharp.pending" ];then
        IFS= read -r pending < "$ST/param-sharp.pending" || return 1
        [ "$current" = "$pending" ] || return 1
    fi
    if [ "$current" != "$original" ];then
        receipt=$ST/param-sharp.current;[ ! -f "$ST/param-sharp.pending" ] || receipt=$ST/param-sharp.pending
        IFS= read -r expected < "$receipt" || return 1
        [ "$current" = "$expected" ] || return 1
        printf '%s\n' "$original" > "$ST/param-sharp.pending" || return 1
        LD_LIBRARY_PATH=/usr/lib:/lib LD_PRELOAD= "$SHARP" "$original" > "$ST/param-sharp-return.txt" 2>&1 || return 1
        current=$(sharp_read) && [ "$current" = "$original" ] || return 1
    fi
    rm -f "$ST/param-sharp.orig" "$ST/param-sharp.current" "$ST/param-sharp.pending";return 0
}
reload_tools(){
 [ -f "$LOAD" ] && [ ! -L "$LOAD" ] && [ -x "$LOAD" ] && [ "$("$OPS" sha256 "$LOAD")" = "$LUT_LOAD_SHA" ] || die LUT_RELOAD_IDENTITY_FAILED
}
reload_pending(){
 [ "${NXL_SRW_LUT_RESET:-0}" = 1 ] && return 0
 printf 'NXL_LUT_RELOAD_PENDING1\n' > "$ST/reload.pending" || die LUT_RELOAD_MARKER_FAILED
}
require_reload_clear(){
 [ ! -e "$RUN/lutload.pending" ] && [ ! -L "$RUN/lutload.pending" ] || die LUT_RELOAD_RESTART_REQUIRED
}
live_reload(){
 # SRW cleanup restores its original tables only. It must never open EP here.
 if [ "${NXL_SRW_LUT_RESET:-0}" = 1 ];then rm -f "$ST/reload.pending";return 0;fi
 [ "${NXL_LUT_DEFER_RELOAD:-0}" != 1 ] || return 0
 require_reload_clear;reload_tools;lut_idle;reload_pending
 if [ -f "$ST/ready" ];then target=$SLOT
 elif [ -r "$ST/factory-slot" ];then IFS= read -r target < "$ST/factory-slot" || die LUT_FACTORY_SLOT_UNAVAILABLE
 else target=current;fi
 if [ "$target" = current ];then set -- current;else set -- once "$target";fi
 for reload_attempt in 1 2 3;do
  if "$LOAD" "$@" > "$ST/reload.log" 2>&1;then break;else reload_rc=$?;fi
  require_reload_clear
  # Exit 9 is before the DMA trigger. Require the original IRQ state back
  # before trying the next photo frame. Never retry a possibly active DMA.
  [ "$reload_attempt" -lt 3 ] && [ "$reload_rc" -eq 9 ] &&
   grep -qx 'REFUSE preview frame sync failed' "$ST/reload.log" &&
   grep -qx 'IRQ_RETURN EOF14=RETURNED DMA7=RETURNED baseline_restored=1 dma_confirmed=0' "$ST/reload.log" || die LUT_DMA_RELOAD_UNCONFIRMED
  sleep 0.15
  lut_idle
 done
 rm -f "$ST/reload.pending" || die LUT_RELOAD_RECEIPT_FAILED
 echo 'NXL_LUT_RELOAD_CONFIRMED1 EOF14 DMA7 ACCESS_CLEAR'
}
restore_factory() {
    reload_pending
    sharp_restore || return 1
    # Restore dynamic consumers before changing the shared A18 slot.
    video_restore || return 1
    photo_log_restore || return 1
    [ -f "$ST/ready" ] || return 0
    rc=0
    # Restore pointers first. Never reload a slot while replacing its bytes.
    # JPEG-only v1 never wrote View4. Preview v2 must restore it first.
    # Only pre-v1 legacy sessions wrote an OLED preference.
    if ! session_jpeg_only; then
        "$RW" view-restore > "$ST/off-view.log" 2>&1 || rc=1
    fi
    "$RW" still-restore > "$ST/off-still.log" 2>&1 || rc=1
    "$RW" bv-restore > "$ST/off-bv.log" 2>&1 || rc=1
    "$SW" restore "$SLOT" "$ST/a18_orig.bin" > "$ST/off-slot.log" 2>&1 || rc=1
    if ! session_supported; then oled_restore || rc=1; fi
    if [ "$rc" -eq 0 ]; then
        if ! session_supported; then rm -f "$PD/oled_orig"; fi
        # A retry retains the original photo bytes until both selectors are restored.
        rm -f "$PHOTO_INTENT" || return 1
        rm -f "$ST/current" "$ST/strength" "$ST/ready" "$ST/pipeline" "$ST/expected.bin" "$ST/detail-parameters"
        return 0
    fi
    echo "ERR restore incomplete; retain $ST and power-cycle camera" >&2
    return 1
}
cleanup() {
    code=$?
    trap - 0 HUP INT TERM
    if [ "$rollback" -eq 1 ]; then
        (restore) || code=1
    fi
    rm -f "$ST/expected.new" "$ST/base.new" "$ST/pipeline.new" "$ST/current.new" "$ST/strength.new" "$ST/detail-parameters.new"
    rm -f "$lock/pid"
    rmdir "$lock" 2>/dev/null || :
    exit "$code"
}
trap cleanup 0
trap 'exit 130' HUP INT TERM
mkdir -p "$ST" || die 'cannot create session'
begin() {
    if [ -f "$ST/ready" ]; then
        session_supported || die 'legacy preview session; select OFF before applying'
        return 0
    fi
    [ ! -f "$PD/oled_orig" ] || die 'legacy OLED backup; select OFF and cold-start before applying'
    "$RW" status > "$ST/before.log" 2>&1 || die 'registry read failed'
    grep -q '^VIEW4 *: 0x810fd100 0x81101e00 0x81106b00 0x81115200$' "$ST/before.log" \
        || die 'View4 is not factory; disable other LUT mods and power-cycle first'
    if [ "${NXL_SRW_LUT_RESET:-0}" != 1 ];then
        reload_tools
        "$LOAD" source > "$ST/factory-slot.new" || die LUT_FACTORY_SLOT_UNAVAILABLE
        mv "$ST/factory-slot.new" "$ST/factory-slot" || die LUT_FACTORY_SLOT_UNAVAILABLE
    fi
    "$SW" read "$SLOT" "$ST/a18_orig.bin" > "$ST/backup.log" 2>&1 || die 'slot backup failed'
    "$RW" view-save >> "$ST/backup.log" 2>&1 || die 'View4 backup failed'
    "$RW" still-save >> "$ST/backup.log" 2>&1 || die 'Still6 backup failed'
    "$RW" bv-save >> "$ST/backup.log" 2>&1 || die 'BV12 backup failed'
    printf 'view-jpeg-v2\n' > "$ST/pipeline" || die 'cannot save pipeline marker'
    : > "$ST/ready"
}
commit_staged() {
    # All validation and conversion precede the first camera mutation.
    begin
    # Mark before changing anything; a partial failure restores every backup.
    rollback=1
    reload_pending
    rm -f "$ST/detail-parameters" || die OLD_DETAIL_RECEIPT_INVALIDATION_FAILED
    # Promote an existing v1 session without replacing its original snapshots.
    # Commit this marker BEFORE View4 is written so rollback restores View4.
    printf 'view-jpeg-v2\n' > "$ST/pipeline.new" || die 'cannot stage pipeline marker'
    mv "$ST/pipeline.new" "$ST/pipeline" || die 'cannot commit pipeline marker'
    "$SW" write "$SLOT" "$ST/expected.new" > "$ST/write.log" 2>&1 || die 'slot write failed'
    "$RW" view-set "$SLOT" > "$ST/view.log" 2>&1 || die 'View4 write failed'
    "$RW" still-write "$SLOT" > "$ST/still.log" 2>&1 || die 'Still6 write failed'
    "$RW" bv-write "$SLOT" > "$ST/bv.log" 2>&1 || die 'BV12 write failed'
    mv "$ST/expected.new" "$ST/expected.bin" || die 'cannot commit expected LUT bytes'
    printf '%s\n' "$name" > "$ST/current.new"
    mv "$ST/current.new" "$ST/current" || die 'cannot commit state'
    printf '%s\n' "$strength" > "$ST/strength.new" || die 'cannot stage strength'
    mv "$ST/strength.new" "$ST/strength" || die 'cannot commit strength'
    rollback=0
    echo "STAGED LIVE_VIEW_JPEG $name strength=$strength; OLED preserved"

}
detail_source() {
    detail_id=$1;detail_out=$2
    valid_name "$detail_id" || die 'invalid detail LUT name'
    if [ -f "$LAB/$detail_id.bin" ];then cp "$LAB/$detail_id.bin" "$detail_out" || die 'cannot freeze detail LUT';
    elif [ -f "$LAB/$detail_id.cube" ];then "$CV" "$LAB/$detail_id.cube" "$detail_out" > "$ST/detail-convert.log" 2>&1 || die 'detail conversion failed';
    else die 'detail LUT missing';fi
}

[ -f "$D/log-lut-policy.sh" ] && [ ! -L "$D/log-lut-policy.sh" ] && [ "$("$OPS" sha256 "$D/log-lut-policy.sh")" = "$LOG_LUT_POLICY_SHA" ] || die LOG_LUT_POLICY_IDENTITY_FAILED
. "$D/log-lut-policy.sh"
restore(){
 if [ "$LUT_MODE" = VLOG ];then rollback=0;log_neutral;else restore_factory;fi
}
detail_receipt(){
 printf 'NX_LUT_DETAIL_APPLIED2 A=%s %s %s %s %s B=%s %s %s %s %s\n' "$request_a" "$as" "$ah" "$ap" "$ac" "$request_b" "$bs" "$bh" "$bp" "$bc" > "$ST/detail-parameters.new" || die DETAIL_RECEIPT_FAILED
 mv "$ST/detail-parameters.new" "$ST/detail-parameters" || die DETAIL_RECEIPT_FAILED
}
apply_detail(){
 entry=$1;shift
 [ "$#" = 10 ] || die INVALID_DETAIL_REQUEST
 name=$1;as=$2;ah=$3;ap=$4;ac=$5;other=$6;bs=$7;bh=$8;bp=$9;shift 9;bc=$1
 valid_name "$name" && valid_name "$other" || die INVALID_LUT_NAME
 for value in "$as" "$bs";do
  case "$value" in ''|*[!0-9]*) die INVALID_STRENGTH;;esac
  [ "${#value}" -le 3 ] && [ "$value" -le 100 ] && [ $((value%5)) = 0 ] || die INVALID_STRENGTH
 done
 for value in "$ah" "$ap" "$ac" "$bh" "$bp" "$bc";do
  case "$value" in 0|1|2|3|4|5|6|7|8|9|10|-1|-2|-3|-4|-5|-6|-7|-8|-9|-10) :;;*) die INVALID_ADJUSTMENT;;esac
 done
 . "$D/process.sh";nxl_require_normal_mode || die FACTORY_MODE_NOT_SUPPORTED
 lut_current_mode
 if [ "$entry" = log-detail ];then
  [ "$LUT_MODE" = VLOG ] && [ "$bs" = 0 ] && [ "$other" = "$name" ] || die LOG_SINGLE_LUT_REQUIRED
 else
  [ "$LUT_MODE" = STD ] || die RESTORE_LOG_BEFORE_STANDARD_LUT
  [ ! -f "$ST/video_pending" ] || die VIDEO_TRANSITION_PENDING_RESTORE_FIRST
 fi
 lut_check_source "$name"
 cp "$LAB/$name.bin" "$ST/detail-a.bin" || die LUT_FREEZE_FAILED
 # Verify the frozen bytes, not only an earlier removable-source read.
 line_a=$(awk -F '\t' -v id="$name" '$1==id{print $3}' "$CATALOG")
 [ "$("$OPS" sha256 "$ST/detail-a.bin")" = "$line_a" ] || die LUT_FROZEN_IDENTITY_FAILED
 lut_check_source "$other"
 cp "$LAB/$other.bin" "$ST/detail-b.bin" || die LUT_FREEZE_FAILED
 line_b=$(awk -F '\t' -v id="$other" '$1==id{print $3}' "$CATALOG")
 [ "$("$OPS" sha256 "$ST/detail-b.bin")" = "$line_b" ] || die LUT_FROZEN_IDENTITY_FAILED
 [ -f "$BLEND" ] && [ ! -L "$BLEND" ] && [ -x "$BLEND" ] && [ "$("$OPS" sha256 "$BLEND")" = "$BLEND_SHA" ] || die LUT_BLEND_IDENTITY_FAILED
 if [ "$LUT_MODE" = VLOG ];then cp "$D/data/clean-log/NXLOG_REF_V1.bin" "$ST/detail-stock.bin" || die LOG_BASELINE_FAILED
 else "$SW" read 0x81115200 "$ST/detail-stock.bin" > "$ST/detail-stock.log" 2>&1 || die FACTORY_BASELINE_FAILED;fi
 rm -f "$ST/expected.new"
 "$BLEND" "$ST/detail-stock.bin" "$ST/detail-a.bin" "$ST/detail-b.bin" "$as" "$ah" "$ap" "$ac" "$bs" "$bh" "$bp" "$bc" --output "$ST/expected.new" > "$ST/detail-build.log" 2>&1 || die LUT_BUILD_FAILED
 build=$(cat "$ST/detail-build.log") || die LUT_PLAN_UNAVAILABLE
 case "$build" in 'NXLUTBLEND2 bytes=19712 sharp_offset='*) sharpness=${build##*sharp_offset=};;*) die LUT_PLAN_INVALID;;esac
 case "$sharpness" in 0|1|2|3|4|5|6|7|8|9|10|-1|-2|-3|-4|-5|-6|-7|-8|-9|-10) :;;*) die SHARPNESS_PLAN_INVALID;;esac
 # Save request identity before the baseline-restore function reuses name.
 request_a=$name;request_b=$other
 require_reload_clear
 [ ! -f "$ST/param-sharp.pending" ] || die SHARPNESS_PENDING_RESTORE_FIRST
 sharp_prepare
 lut_idle
 if [ "$as" = 0 ] && [ "$bs" = 0 ];then
  restore
 else
  video_restore || die VIDEO_RESTORE_UNCONFIRMED
  strength=$as;commit_staged
  sharp_apply || die SHARPNESS_READBACK_UNCONFIRMED
  # A confirmed ordinary or Log LUT also applies to new video recordings.
  video_enable
 fi
 detail_receipt
 echo 'NXL_LUT_CONFIGURED1'
 live_reload
 cat "$ST/detail-parameters"
}
case "${1:-status}" in
clean-log-prepare) clean_log_prepare;;
clean-log-release) clean_log_release;;
detail|log-detail) verb=$1;shift;apply_detail "$verb" "$@";;
apply) [ "$#" = 3 ] || die INVALID_APPLY_REQUEST;apply_detail detail "$2" "$3" 0 0 0 "$2" 0 0 0 0;;
off)
 lut_current_mode;lut_idle
 [ "${NXL_SRW_LUT_RESET:-0}" = 1 ] || require_reload_clear
 restore || exit 1
 live_reload
 printf 'NX_LUT_BASELINE_APPLIED3 mode=%s\n' "$LUT_MODE";;
reload)
 lut_current_mode
 if [ "${NXL_LUT_DEFER_RELOAD:-0}" = 1 ] || [ "${NXL_SRW_LUT_RESET:-0}" = 1 ];then lut_idle;fi
 live_reload;;
recover-oled)
 lut_current_mode;[ "$LUT_MODE" = STD ] || die RESTORE_LOG_FIRST
 if [ ! -f "$ST/ready" ] && [ -f "$PD/oled_orig" ];then oled_restore || die OLED_RESTORE_FAILED;rm -f "$PD/oled_orig";fi;;
begin) lut_current_mode;lut_idle;begin;;
status)
 [ ! -r "$ST/current" ] || { printf 'ACTIVE ';cat "$ST/current"; }
 [ ! -r "$ST/strength" ] || { printf 'STRENGTH ';cat "$ST/strength"; }
 "$RW" status;;
*) die UNKNOWN_LUT_COMMAND;;
esac
