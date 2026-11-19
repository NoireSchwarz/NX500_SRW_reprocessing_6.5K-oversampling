from context import *
P=T/'base/nx-lut-lite'
patch(P/'memory-test.sh','    /bin/sh "$ENGINE" off > "$job/live-lut-off.log"','    NXL_SRW_LUT_RESET=1 /bin/sh "$ENGINE" off > "$job/live-lut-off.log"')
p=P/'lut-core.sh'
patch(p,'LUT_MODE=STD\n','LUT_MODE=STD\nLOAD=$D/bin/nxl-lutload.arm\nLUT_LOAD_SHA='+'0'*64+'\n',1)
patch(p,'\nrestore_factory() {','''
reload_tools(){
 [ -f "$LOAD" ] && [ ! -L "$LOAD" ] && [ -x "$LOAD" ] && [ "$("$OPS" sha256 "$LOAD")" = "$LUT_LOAD_SHA" ] || die LUT_RELOAD_IDENTITY_FAILED
}
reload_pending(){
 [ "${NXL_SRW_LUT_RESET:-0}" = 1 ] && return 0
 printf 'NXL_LUT_RELOAD_PENDING1\\n' > "$ST/reload.pending" || die LUT_RELOAD_MARKER_FAILED
}
live_reload(){
 # SRW cleanup restores its original tables only. It must never open EP here.
 if [ "${NXL_SRW_LUT_RESET:-0}" = 1 ];then rm -f "$ST/reload.pending";return 0;fi
 [ "${NXL_LUT_DEFER_RELOAD:-0}" != 1 ] || return 0
 reload_tools;lut_idle;reload_pending
 if [ -f "$ST/ready" ];then target=$SLOT
 elif [ -r "$ST/factory-slot" ];then IFS= read -r target < "$ST/factory-slot" || die LUT_FACTORY_SLOT_UNAVAILABLE
 else target=current;fi
 if [ "$target" = current ];then "$LOAD" current > "$ST/reload.log" 2>&1 || die LUT_DMA_RELOAD_UNCONFIRMED
 else "$LOAD" once "$target" > "$ST/reload.log" 2>&1 || die LUT_DMA_RELOAD_UNCONFIRMED;fi
 rm -f "$ST/reload.pending" || die LUT_RELOAD_RECEIPT_FAILED
 echo 'NXL_LUT_RELOAD_CONFIRMED1 EOF14 DMA7 ACCESS_CLEAR'
}
restore_factory() {''')
patch(p,'    sharp_restore || return 1\n    # Restore dynamic consumers','    reload_pending\n    sharp_restore || return 1\n    # Restore dynamic consumers')
patch(p,'    "$SW" read "$SLOT" "$ST/a18_orig.bin"','''    if [ "${NXL_SRW_LUT_RESET:-0}" != 1 ];then
        reload_tools
        "$LOAD" source > "$ST/factory-slot.new" || die LUT_FACTORY_SLOT_UNAVAILABLE
        mv "$ST/factory-slot.new" "$ST/factory-slot" || die LUT_FACTORY_SLOT_UNAVAILABLE
    fi
    "$SW" read "$SLOT" "$ST/a18_orig.bin"''')
patch(p,'    rollback=1\n    # Promote an existing','    rollback=1\n    reload_pending\n    # Promote an existing')
patch(p,'echo "OK LIVE_VIEW_JPEG $name strength=$strength; OLED preserved; HALF-PRESS to load preview, then shoot"','echo "STAGED LIVE_VIEW_JPEG $name strength=$strength; OLED preserved"')
patch(p,'  [ ! -f "$ST/video_enabled" ] && [ ! -f "$ST/video_pending" ] || die DISABLE_VIDEO_LUT_BEFORE_EDITING','  [ ! -f "$ST/video_pending" ] || die VIDEO_TRANSITION_PENDING_RESTORE_FIRST')
patch(p,' request_a=$name;request_b=$other\n',' request_a=$name;request_b=$other;keep_video=0\n [ ! -f "$ST/video_enabled" ] || keep_video=1\n')
patch(p,'  if [ "$LUT_MODE" = VLOG ];then video_restore || die LOG_VIDEO_RESTORE_UNCONFIRMED;fi','  video_restore || die VIDEO_RESTORE_UNCONFIRMED')
patch(p,'  if [ "$LUT_MODE" = VLOG ];then video_enable;fi','  if [ "$LUT_MODE" = VLOG ] || [ "$keep_video" = 1 ];then video_enable;fi')
patch(p," printf 'NX_LUT_DETAIL_APPLIED2", " live_reload\n printf 'NX_LUT_DETAIL_APPLIED2")
patch(p,' echo \'OK LUT_BASELINE_RESTORED\';;',' live_reload\n printf \'NX_LUT_BASELINE_APPLIED3 mode=%s\\n\' "$LUT_MODE";;\nreload) lut_current_mode;lut_idle;live_reload;;')
patch(p,' else video_enable;fi;;',' else video_enable;fi\n live_reload;;')
p=P/'log-lut-policy.sh'
patch(p," echo 'OK CLEAN_LOG_NEUTRAL_ROUTE_READY'"," live_reload\n echo 'OK CLEAN_LOG_NEUTRAL_ROUTE_READY'")
patch(p," echo 'OK CLEAN_LOG_RELEASED_FACTORY_LOOKS_AVAILABLE'"," live_reload\n echo 'OK CLEAN_LOG_RELEASED_FACTORY_LOOKS_AVAILABLE'")
p=P/'gamma-reload.sh'
patch(p,'IDLE=$D/bin/nxl-lutidle.arm','REFRESH=$D/bin/nxl-native-refresh.arm\nNATIVE_REFRESH_SHA='+'0'*64+'\nIDLE=$D/bin/nxl-lutidle.arm')
patch(p,'case "$1" in\nlog)','''gamma_wait(){
 wanted=$1;phase=$2;file=$RUN/gamma-confirm.txt
 [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || return 1
 for pass in 1 2 3 4 5 6 7 8;do
  if "$IDLE" --gamma-state > "$file" 2>&1 && grep -qx "mode=$wanted" "$file" && grep -qx "phase=$phase" "$file" && grep -qx 'fault=0' "$file";then
   e=$(sed -n 's/^epoch=//p' "$file");q=$(sed -n 's/^queued_epoch=//p' "$file")
   [ -n "$e" ] && [ "$e" = "$q" ] && return 0
  fi
  sleep 0.1
 done
 return 1
}
gamma_confirm(){
 wanted=$1;phase=$2
 if ! gamma_wait "$wanted" "$phase";then
  [ -f "$REFRESH" ] && [ ! -L "$REFRESH" ] && [ -x "$REFRESH" ] && [ "$("$OPS" sha256 "$REFRESH")" = "$NATIVE_REFRESH_SHA" ] || fail NATIVE_REFRESH_IDENTITY_FAILED
  "$REFRESH" photo > "$RUN/gamma-native-refresh.log" 2>&1 || fail GAMMA_NATIVE_REFRESH_UNCONFIRMED
  gamma_wait "$wanted" "$phase" || fail GAMMA_NATIVE_QUEUE_NOT_CONFIRMED
 fi
 # Keep the Gamma response as the final record, for the existing strict parser.
 /bin/sh "$ENGINE" reload > "$RUN/gamma-lut-reload.log" 2>&1 || fail GAMMA_LUT_RELOAD_UNCONFIRMED
 cat "$RUN/gamma-confirm.txt"
}
case "$1" in
log)''')
patch(p,'    /bin/sh "$ENGINE" clean-log-prepare','    NXL_LUT_DEFER_RELOAD=1 /bin/sh "$ENGINE" clean-log-prepare')
patch(p,'    "$GAMMA" log\n','    "$GAMMA" log > "$RUN/gamma-request.txt" || { cat "$RUN/gamma-request.txt";exit 1; }\n    gamma_confirm LOG NATIVE_QUEUED\n')
patch(p,'        /bin/sh "$ENGINE" clean-log-release','        NXL_LUT_DEFER_RELOAD=1 /bin/sh "$ENGINE" clean-log-release')
patch(p,'        cat "$result"\n','        gamma_confirm FACTORY FACTORY_QUEUED\n',1)
# Native restore may legitimately have no active lease (OFF, epoch=0).
patch(p,'grep -qx "phase=$phase" "$file"','{ grep -qx "phase=$phase" "$file" || { [ "$wanted" = FACTORY ] && grep -qx "phase=OFF" "$file"; }; }')
p=P/'video-mode.sh'
patch(p,'USB=$D/bin/nxl-usb-guard.arm','REFRESH=$D/bin/nxl-native-refresh.arm\nNATIVE_REFRESH_SHA='+'0'*64+'\nUSB=$D/bin/nxl-usb-guard.arm')
patch(p,'usb_check preflight BEFORE_VIDEO || exit 1','''usb_check preflight BEFORE_VIDEO || exit 1
[ -f "$REFRESH" ] && [ ! -L "$REFRESH" ] && [ -x "$REFRESH" ] && [ "$("$OPS" sha256 "$REFRESH")" = "$NATIVE_REFRESH_SHA" ] || { echo VIDEO_NATIVE_REFRESH_IDENTITY_FAILED;exit 1; }
native_reload(){
 mkdir -p /tmp/nx-lut-lite || return 1
 "$REFRESH" "$1" > /tmp/nx-lut-lite/video-native-refresh.log 2>&1 || {
  printf 'NXL_VIDEO_MODE1\\nstate=UNKNOWN\\nwidth=0\\nheight=0\\nrecording_or_capture=0\\nreason=VIDEO_NATIVE_RELOAD_UNCONFIRMED\\nend=NXL_VIDEO_MODE_END1\\n';return 1;
 }
}''')
patch(p,'full)\n    step BEFORE_NATIVE24_SELECTION','full)\n    native_reload video24 || exit 1\n    step BEFORE_NATIVE24_SELECTION')
patch(p,'    step AFTER_NATIVE24_STATUS "$FULL" status','    native_reload video24 || exit 1\n    step AFTER_NATIVE24_STATUS "$FULL" status')
patch(p,'crop80)\n','crop80)\n    native_reload video30 || exit 1\n')
patch(p,'    step AFTER_RATE_PATCH "$MODE" 30p || exit 1;;','    step AFTER_RATE_PATCH "$MODE" 30p || exit 1\n    native_reload video30 || exit 1;;')
patch(p,'2k|uhd)\n','2k|uhd)\n    native_reload video30 || exit 1\n')
patch(p,'    if [ "$1" = uhd ];then step AFTER_RATE_RESTORE "$MODE" rate70 || exit 1;fi;;','    if [ "$1" = uhd ];then step AFTER_RATE_RESTORE "$MODE" rate70 || exit 1;fi\n    native_reload video30 || exit 1;;')
print('PASS RC4 shell integrations; SRW EP path kept separate')
