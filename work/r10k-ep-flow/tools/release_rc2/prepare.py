from context import *
import re,shutil
assert not W.exists();W.mkdir();(W/'logs').mkdir()
shutil.copytree(B,S)
for n in ['core/src','core/tools','core/tests','common','base/nx-lut-lite/src','base/nx-lut-lite/vendor','base/nx-lut-lite/tools','base/nx-lut-lite/data']:
 shutil.copytree(SRC/n,T/n,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for p in(SRC/'base/nx-lut-lite').glob('*.sh'):shutil.copyfile(p,T/'base/nx-lut-lite'/p.name)
put(W/'source-before.json',{p.relative_to(T).as_posix():sha(p)for p in T.rglob('*')if p.is_file()})
put(W/'git-before.json',dict(head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True),status=subprocess.check_output(['git','-C',str(R),'status','--porcelain=v1','-uall'],text=True,encoding='utf8')))
U=T/'base/nx-lut-lite/src';p=U/'menu_lut_detail_v2.inc'
patch(p,'if(!detail_load_parameters())detail_queue(0);return 1;','if(!detail_load_parameters())strcpy(detail_message,"調整參數或按OK套用");return 1;',2)
patch(p,'detail_output[0]=0;XUnmapWindow(dpy,win);if(detail_dialog)XUnmapWindow(dpy,detail_dialog);XFlush(dpy);detail_hidden=1;',
 'detail_output[0]=0;strcpy(detail_message,"套用中，請稍候");detail_hidden=0;')
patch(p,'XMapRaised(dpy,win);if(detail_dialog)XMapRaised(dpy,detail_dialog);XSetInputFocus(dpy,detail_dialog?detail_dialog:win,RevertToPointerRoot,CurrentTime);detail_hidden=0;return 1;',
 'detail_hidden=0;return 1;')
patch(p,' if(!detail_visible)return 0;\n if(detail_log_scope',
 ''' if(!detail_visible)return 0;
 if(detail_child){if(value==1){if(code==127)detail_close(1);else if(code==125||code==126||code==83)detail_close(2);}return 1;}
 if(detail_log_scope''')
patch(p,' if(!detail_visible||detail_hidden)return 0;',''' if(!detail_visible||detail_hidden)return 0;
 if(detail_child){if(e->type==ButtonRelease&&e->xbutton.button==1&&e->xany.window==win&&e->xbutton.y<38&&e->xbutton.x>=500)detail_close(1);return e->type==ButtonPress||e->type==ButtonRelease||e->type==MotionNotify;}''')
p=U/'menu_srw_layout.h';patch(p,'{0,2,3,4,5,7}','{0,2,3,4,7}')
p=U/'menu.c'
patch(p,'static int left_panel,sidebar_focus;','static int left_panel,sidebar_focus;\nstatic int video_picker_open;')
patch(p,'#include "release_next.inc"','/* New session preparation belongs to the next explicit SRW button press. */')
patch(p,'task_auto_result=0;release_next_status=0;','task_auto_result=0;')
s=p.read_text('utf8');a=s.index('            if(confirmed&&(create_new!=5||task_auto_result==1)){');b=s.index('\n            unsigned long until=',a);s=s[:a]+s[b:];p.write_text(s,'utf8',newline='\n')
patch(p,'strcpy(notice,task_auto_result==1?(release_next_status>0?"完成，已準備新工作階段":"JPEG已儲存；請核驗新工作階段"):"初始化已停止，請查看狀態");',
 'strcpy(notice,task_auto_result==1?"完成，JPEG已儲存；再按初始化即可繼續":"初始化已停止，請查看狀態");')
patch(p,'if(srw_row==5){start_menu_new_session();return;}','/* New-session row removed; the auto-init wrapper prepares each run. */')
patch(p,'if(detail_key(code,value))return;','if(detail_key(code,value))return;\n if(video_picker_key(code,value))return;')
patch(p,'if(lut_picker_pointer(&ev))','if(video_picker_pointer(&ev)){if(!quitting)draw();continue;}\n            if(lut_picker_pointer(&ev))')
old='''            if(ev.type==FocusOut&&detail_visible){if(!detail_hidden){Window focus;int revert;XGetInputFocus(dpy,&focus,&revert);if(focus!=win&&focus!=detail_dialog)detail_close(2);}continue;}
            if (ev.type==FocusOut) {launch_event("MENU_FOCUS_OUT",argc>1?argv[1]:"open",ev.xfocus.mode,ev.xfocus.detail);action=0;quitting=1;}'''
patch(p,old,'            if(ev.type==FocusOut){menu_focus_out(&ev.xfocus);continue;}')
patch(p,'static void draw(void) {','''/* X focus notifications are not confirmation/cancel input in a modal.
 * Refreshes and native updates must not dismiss an open selector. */
static void menu_focus_out(const XFocusChangeEvent*e){
 if(detail_visible||lut_picker_open||video_picker_open||video_child>0)return;
 if(e->mode!=NotifyNormal||e->detail==NotifyInferior)return;
 launch_event("MENU_FOCUS_OUT","external top-level focus",e->mode,e->detail);action=0;quitting=1;
}
static void draw(void) {''')
patch(p,'NX_LUT_RELEASE=0.4.0-RC1;','NX_LUT_RELEASE=0.4.0-RC2;')
p=U/'menu_video.inc';s=p.read_text('utf8');a=s.index('static const unsigned char release_video_rows[]');s=s[:a]+s[a:].replace('{0,1,2,3,4,6,8}','{0,9,6,8}',1)
s=s.replace('static const unsigned char release_video_rows[]','#include "menu_video_picker.inc"\nstatic const unsigned char release_video_rows[]',1)
s=s.replace(' if(video_row==8){',' if(video_row==9){video_picker_begin();return;}\n if(video_row==8){',1)
s=s.replace('NX LUT Lite 0.4.0 RC1','NX LUT Lite 0.4.0 RC2')
s=s.replace('const char*title=id==0?"輸出狀態":','const char*title=id==9?"畫面模式":id==0?"輸出狀態":')
s=s.replace('const char*value=id==0?','const char*value=id==9?"開啟三項畫面設定":id==0?')
s=s.replace('text(540,442,"MENU 返回");publish_ui();','text(540,442,"MENU 返回");draw_video_picker();publish_ui();')
p.write_text(s,'utf8',newline='\n');shutil.copyfile(A/'video_picker.inc',U/'menu_video_picker.inc')
# Preserve native resource proof, source configuration and render workers.
# Only select/reuse the fresh empty job created by this button's own rearm.
p=T/'base/nx-lut-lite/auto-init.sh';s=p.read_text('utf8')
s=s.replace('case "$state" in\n COMPLETE|RESTART_REQUIRED)', 'original_job=$(sed -n \'s/^job_dir=//p\' "$root/session-before.txt")\nrearmed=0\ncase "$state" in\n COMPLETE|RESTART_REQUIRED)',1)
s=s.replace('|| fail PREVIOUS_TASK_RESOURCE_CHECK_FAILED;;','|| fail PREVIOUS_TASK_RESOURCE_CHECK_FAILED;rearmed=1;;',1)
a=s.index('# Revalidate after a possible rearm transaction.');b=s.index('# Each selected experiment has a fresh immutable source configuration.',a)
s=s[:a]+'''# A single explicit button owns resource verification and a fresh round.
if [ -n "${NX51_SOURCE_SRW:-}" ];then
 selected_status=$root/session-before.txt
 if [ "$rearmed" = 1 ];then
  /bin/sh "$RUNNER" --status > "$root/session-prepared.txt" || fail PREPARED_SESSION_STATUS_FAILED
  selected_status=$root/session-prepared.txt
 fi
 current_job=$(sed -n 's/^job_dir=//p' "$selected_status")
 if [ "$rearmed" = 1 ] && [ -n "$current_job" ] && [ "$current_job" != "$original_job" ] &&
    grep -qx state=NEED_STILL_INIT "$selected_status" && grep -qx requires_reset=0 "$selected_status" &&
    grep -qx front_attempt=0 "$selected_status" && grep -qx backend_attempt=0 "$selected_status" &&
    grep -qx look_id=- "$selected_status";then
  printf 'REUSE_THIS_BUTTON_VERIFIED_FRESH_SESSION=%s\\n' "$current_job" > "$root/session-selected-photo.txt"
 else /bin/sh "$RUNNER" --new > "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_FAILED;fi
 NX51_RC_NEW_JOB=1;export NX51_RC_NEW_JOB
fi
''' +s[b:]
p.write_text(s,'utf8',newline='\n')
put(W/'changes.json',dict(passed=True,version='0.4.0-RC2',source_changed_only_UI_and_auto_init=True,
 observed_280ms_self_unmap_path_removed=True,no_auto_apply_on_detail_entry=True,modal_focus_refresh_not_cancel=True,
 SRW_rows=5,VID_rows=4,video_popup_routes=[3,1,2],removed_test_route=4,SRW_new_round_at_explicit_start=True,
 hardware_confirmation_pending=True))
print('PASS isolated RC2 changes; no source publication or SD writes',flush=True)
