"""Port the reviewed R8f grading engine, with distinct STD/Log entry points."""
from context import *
import re,shutil
ref=W/'r8f-reference/base/nx-lut-lite'
for n in ['lut_parameters.h','lut_detail_flow.h','menu_lut_detail_v2.inc','nxl-lutblend.c','nxl-sharp.c']:
 assert not(T/'base/nx-lut-lite/src'/n).exists()
 shutil.copyfile(ref/'src'/n,T/'base/nx-lut-lite/src'/n)
for n in ['nxl-lutguard.c','nxl-lutidle.c']:
 target=T/('core/src/'+n if n=='nxl-lutidle.c'else'base/nx-lut-lite/src/'+n)
 shutil.copyfile(A/n,target)
for n in ['lut.sh','log-lut-policy.sh']:shutil.copyfile(A/n,T/'base/nx-lut-lite'/n)
host('blend',T/'base/nx-lut-lite/src/nxl-lutblend.c',M/'bin/nxl-lutblend.arm')
assert sha(M/'bin/nxl-lutblend.arm')==sha(W/'r8f-reference/bin/nxl-lutblend.arm')
shutil.copyfile(W/'r8f-reference/bin/nxl-sharp.arm',M/'bin/nxl-sharp.arm')
host('lut-guard',T/'base/nx-lut-lite/src/nxl-lutguard.c',M/'bin/nxl-lutguard.arm')
host('lut-idle',T/'core/src/nxl-lutidle.c',M/'bin/nxl-lutidle.arm')

s=(ref/'lut.sh').read_text('utf8')
decl='''OPS=$D/bin/nxl-fileops
IDLE=$D/bin/nxl-lutidle.arm
GAMMA=$D/bin/nxl-gamma-reload.arm
CATALOG=$D/data/release-lut-allowlist.tsv
LUT_MODE=STD
OPS_SHA=0000000000000000000000000000000000000000000000000000000000000000
LUT_IDLE_SHA=0000000000000000000000000000000000000000000000000000000000000000
LOG_LUT_POLICY_SHA=0000000000000000000000000000000000000000000000000000000000000000
LUT_CATALOG_SHA=0000000000000000000000000000000000000000000000000000000000000000
'''
s=s.replace('VC=$D/bin/nxl-tablecheck.arm\n','VC=$D/bin/nxl-tablecheck.arm\n'+decl)
a=s.index('list() {');b=s.index('\ncase "${1:-status}" in',a)
s=s[:a]+'''list() {
 [ -f "$CATALOG" ] && [ ! -L "$CATALOG" ] && [ "$("$OPS" sha256 "$CATALOG")" = "$LUT_CATALOG_SHA" ] || die LUT_CATALOG_IDENTITY_FAILED
 while IFS="$(printf '\\t')" read -r id input checksum;do
  valid_name "$id" && [ -f "$LAB/$id.bin" ] && printf '%s\\n' "$id"
 done < "$CATALOG"
}
'''+s[b:]
s=s.replace('restore() {','restore_factory() {',1)
a=s.index('case "${1:-status}" in\nrecover-oled)')
prefix=s[:a]
extra=r'''
[ -f "$D/log-lut-policy.sh" ] && [ ! -L "$D/log-lut-policy.sh" ] && [ "$("$OPS" sha256 "$D/log-lut-policy.sh")" = "$LOG_LUT_POLICY_SHA" ] || die LOG_LUT_POLICY_IDENTITY_FAILED
. "$D/log-lut-policy.sh"
restore(){
 if [ "$LUT_MODE" = VLOG ];then rollback=0;log_neutral;else restore_factory;fi
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
  [ ! -f "$ST/video_enabled" ] && [ ! -f "$ST/video_pending" ] || die DISABLE_VIDEO_LUT_BEFORE_EDITING
 fi
 lut_check_source "$name";expected_a=$2
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
 sharp_prepare
 lut_idle
 # Save request identity before the baseline-restore function reuses name.
 request_a=$name;request_b=$other
 if [ "$as" = 0 ] && [ "$bs" = 0 ];then restore
 else
  if [ "$LUT_MODE" = VLOG ];then video_restore || die LOG_VIDEO_RESTORE_UNCONFIRMED;fi
  strength=$as;commit_staged
  sharp_apply || die SHARPNESS_READBACK_UNCONFIRMED
  if [ "$LUT_MODE" = VLOG ];then video_enable;fi
 fi
 printf 'NX_LUT_DETAIL_APPLIED2 A=%s %s %s %s %s B=%s %s %s %s %s\n' "$request_a" "$as" "$ah" "$ap" "$ac" "$request_b" "$bs" "$bh" "$bp" "$bc" > "$ST/detail-parameters.new" || die DETAIL_RECEIPT_FAILED
 mv "$ST/detail-parameters.new" "$ST/detail-parameters" || die DETAIL_RECEIPT_FAILED
 cat "$ST/detail-parameters"
}
case "${1:-status}" in
clean-log-prepare) clean_log_prepare;;
clean-log-release) clean_log_release;;
detail|log-detail) verb=$1;shift;apply_detail "$verb" "$@";;
apply) [ "$#" = 3 ] || die INVALID_APPLY_REQUEST;apply_detail detail "$2" "$3" 0 0 0 "$2" 0 0 0 0;;
off)
 lut_current_mode;lut_idle;restore || exit 1
 echo 'OK LUT_BASELINE_RESTORED';;
video-on|video-off|video-toggle)
 . "$D/process.sh";nxl_require_normal_mode || die FACTORY_MODE_NOT_SUPPORTED
 lut_current_mode;lut_idle
 if [ "$LUT_MODE" = VLOG ];then
  case "$1" in video-on) video_enable;;*) log_neutral;;esac
 elif [ "$1" = video-off ] || { [ "$1" = video-toggle ] && { [ -f "$ST/video_enabled" ] || [ -f "$ST/video_pending" ]; }; };then video_restore
 else video_enable;fi;;
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
'''
# The referenced shell already validates pending sharpness before another
# submission. This guard applies to the unified detail path as well.
extra=extra.replace(' sharp_prepare\n',' [ ! -f "$ST/param-sharp.pending" ] || die SHARPNESS_PENDING_RESTORE_FIRST\n sharp_prepare\n')
(T/'base/nx-lut-lite/lut-core.sh').write_text(prefix+extra,'utf8',newline='\n')
for n in ['lut.sh','lut-core.sh','log-lut-policy.sh']:
 shutil.copyfile(T/'base/nx-lut-lite'/n,M/n)
for n in ['looks-info.json','release-lut-catalog.json','release-lut-allowlist.tsv']:
 shutil.copyfile(T/'base/nx-lut-lite/data'/n,M/'data'/n)
put(W/'engine-build.json',dict(passed=True,blend_reproduced_exact_R8f=True,reused_stock_sharp_client_sha256=sha(M/'bin/nxl-sharp.arm'),
  camera_core_library_changed=False,Log_selection_only_via_VID_log_entry=True,standard_A_B_and_SRW_cannot_select_Log_LUT=True,
  Log_mixing_with_standard_B_disallowed=True,fast_state_reader_has_no_SD_report=True,SD_written=False,hardware_tested=False))
print('PASS separated release LUT engine and Linux-only serialization helpers',flush=True)
