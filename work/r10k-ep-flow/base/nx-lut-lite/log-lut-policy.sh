# Loaded under the existing engine lock plus the video-mode flock and the original engine.lock SRW handoff.
CLEAN_LOG=$RUN/clean-log.policy
LOG_NEUTRAL_SHA=a0692509fba710a54cf47c9ce4eb05c3ef2c9a7e514b3948468b3d415f99b056
PHOTO_SLOT=0x8111EC00
PHOTO_SAFE_SLOT=0x81115200
PHOTO_INTENT=$ST/still-flat.intent
PHOTO_BACKUP=$ST/a19_orig.bin

photo_log_owned(){
 [ -f "$PHOTO_INTENT" ] && [ ! -L "$PHOTO_INTENT" ] &&
 [ -f "$PHOTO_BACKUP" ] && [ ! -L "$PHOTO_BACKUP" ] || return 1
 photo_backup_sha=$("$OPS" sha256 "$PHOTO_BACKUP") || return 1
 [ "$(cat "$PHOTO_INTENT")" = "NXL_STILL_FLAT1171 $photo_backup_sha" ]
}
photo_log_matches(){
 "$SW" dry "$PHOTO_SLOT" "$1" > "$ST/still-flat-check.log" 2>&1 &&
 grep -qx 'SOURCE differs from current slot bytes = 0' "$ST/still-flat-check.log"
}
photo_log_select(){
 "$RW" still-write "$1" > "$ST/still-flat-still.log" 2>&1 &&
 "$RW" bv-write "$1" > "$ST/still-flat-bv.log" 2>&1
}
# Restore the previous test's leased A19 only when its original bytes are available.
photo_log_restore(){
 [ -e "$PHOTO_INTENT" ] || [ -L "$PHOTO_INTENT" ] || return 0
 [ -f "$ST/ready" ] && photo_log_owned || return 1
 photo_log_matches "$PHOTO_BACKUP" && return 0
 photo_log_select "$PHOTO_SAFE_SLOT" &&
 "$SW" restore "$PHOTO_SLOT" "$PHOTO_BACKUP" > "$ST/still-flat-restore.log" 2>&1 &&
 photo_log_matches "$PHOTO_BACKUP"
}
clean_log_gamma_status(){
 [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || return 1
 "$IDLE" --gamma-state > "$ST/log-status.txt" 2>&1 || return 1
 grep -qx 'NXL_GAMMA_RELOAD1' "$ST/log-status.txt" && grep -qx 'end=NXL_GAMMA_RELOAD_END1' "$ST/log-status.txt" && grep -qx 'fault=0' "$ST/log-status.txt"
}
clean_log_factory_status(){
 clean_log_gamma_status && grep -qx 'mode=FACTORY' "$ST/log-status.txt" && grep -Eq '^phase=(OFF|FACTORY_QUEUED)$' "$ST/log-status.txt"
}
lut_current_mode(){
 [ ! -e "$RUN/gamma-life.pending" ] && [ ! -L "$RUN/gamma-life.pending" ] || die GAMMA_LIFECYCLE_RESTORE_REQUIRED
 [ ! -e "$PHOTO_INTENT" ] && [ ! -L "$PHOTO_INTENT" ] || die ORIGINAL_STILL_LUT_RESTORE_REQUIRED
 LUT_MODE=STD
 if [ -e "$CLEAN_LOG" ] || [ -L "$CLEAN_LOG" ];then
  [ -f "$CLEAN_LOG" ] && [ ! -L "$CLEAN_LOG" ] && [ "$(cat "$CLEAN_LOG")" = 'NXL_CLEAN_LOG1 READY' ] || die LOG_TRANSITION_PENDING
  clean_log_gamma_status && grep -qx 'mode=LOG' "$ST/log-status.txt" && grep -qx 'phase=NATIVE_QUEUED' "$ST/log-status.txt" || die LOG_STATE_UNCONFIRMED
  LUT_MODE=VLOG
 elif [ -e "$RUN/gamma-r11h.lease" ];then
  clean_log_factory_status || die GAMMA_RESTORE_REQUIRED
 fi
}
lut_idle(){
 [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || die LUT_IDLE_IDENTITY_FAILED
 "$IDLE" > "$ST/lut-idle.txt" 2>&1 || die STOP_RECORDING_AND_RETURN_TO_PHOTO
}
lut_check_source(){
 id=$1
 [ -f "$CATALOG" ] && [ ! -L "$CATALOG" ] && [ "$("$OPS" sha256 "$CATALOG")" = "$LUT_CATALOG_SHA" ] || die LUT_CATALOG_IDENTITY_FAILED
 line=$(awk -F '\t' -v id="$id" '$1==id{n++;print $2" "$3} END{if(n!=1)exit 1}' "$CATALOG") || die LUT_NOT_IN_RELEASE_LIBRARY
 set -- $line
 [ "$#" = 2 ] && [ "$1" = "$LUT_MODE" ] || die LUT_INPUT_MODE_MISMATCH
 [ -f "$LAB/$id.bin" ] && [ ! -L "$LAB/$id.bin" ] && [ "$("$OPS" sha256 "$LAB/$id.bin")" = "$2" ] || die LUT_SOURCE_IDENTITY_FAILED
}
log_neutral(){
 [ "$("$OPS" sha256 "$D/data/clean-log/NXLOG_REF_V1.bin")" = "$LOG_NEUTRAL_SHA" ] || die LOG_NEUTRAL_IDENTITY_FAILED
 sharp_restore || die LOG_SHARPNESS_RESTORE_UNCONFIRMED
 video_restore || die LOG_VIDEO_RESTORE_UNCONFIRMED
 cp "$D/data/clean-log/NXLOG_REF_V1.bin" "$ST/expected.new" || die LOG_NEUTRAL_COPY_FAILED
 name=NXLOG_REF_V1;strength=0;commit_staged
 video_enable
}
clean_log_prepare(){
 [ ! -e "$RUN/gamma-life.pending" ] && [ ! -L "$RUN/gamma-life.pending" ] || die GAMMA_LIFECYCLE_RESTORE_REQUIRED
 [ ! -e "$CLEAN_LOG" ] && [ ! -L "$CLEAN_LOG" ] || die LOG_RESTORE_FIRST
 lut_idle
 clean_log_factory_status || die GAMMA_RESTORE_REQUIRED
 restore_factory || die ORIGINAL_LUT_RESTORE_UNCONFIRMED
 marker=$CLEAN_LOG.new.$$
 (set -C;printf 'NXL_CLEAN_LOG1 PREPARE\n' > "$marker") || die LOG_PREPARE_MARKER_FAILED
 mv "$marker" "$CLEAN_LOG" || die LOG_PREPARE_PUBLISH_FAILED
 LUT_MODE=VLOG
 log_neutral
 printf 'NXL_CLEAN_LOG1 READY\n' > "$marker" && mv "$marker" "$CLEAN_LOG" || die LOG_READY_PUBLISH_FAILED
 live_reload
 echo 'OK CLEAN_LOG_NEUTRAL_ROUTE_READY'
}
clean_log_release(){
 clean_log_factory_status || die FACTORY_GAMMA_RESTORE_UNCONFIRMED
 lut_idle
 restore_factory || die ORIGINAL_LUT_RESTORE_UNCONFIRMED
 rm -f "$CLEAN_LOG"
 live_reload
 echo 'OK CLEAN_LOG_RELEASED_FACTORY_LOOKS_AVAILABLE'
}
