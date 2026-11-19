from context import *
import shutil,re
assert not W.exists();W.mkdir();(W/'logs').mkdir();shutil.copytree(B,S)
for n in ['base/nx-lut-lite/src','base/nx-lut-lite/vendor','base/nx-lut-lite/tools','base/nx-lut-lite/data','core/src','core/tools','core/tests','common']:
 shutil.copytree(SRC/n,T/n,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for p in (SRC/'base/nx-lut-lite').glob('*.sh'):shutil.copyfile(p,T/'base/nx-lut-lite'/p.name)
put(W/'source-before.json',{p.relative_to(T).as_posix():sha(p)for p in T.rglob('*')if p.is_file()})
put(W/'git-before.json',dict(head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True),status=subprocess.check_output(['git','-C',str(R),'status','--porcelain=v1','-uall'],text=True,encoding='utf8')))
U=T/'base/nx-lut-lite/src';shutil.copyfile(A/'menu_input_lease.h',U/'menu_input_lease.h')
p=U/'menu.c'
patch(p,' * No AF commands, synthetic shutter events, or input grabs are used.', ' * Modal input is exclusive only until returning to the parent menu.\n * No AF commands or synthetic shutter events are used.')
patch(p,'static pid_t rearm_child;', '''static pid_t rearm_child;
#include "menu_input_lease.h"
static struct nxl_input_lease menu_input_lease={{-1,-1},0};
static int menu_modal_input_take(void){
 if(raw_available&&nxl_input_lease_take(&menu_input_lease,menu_input_fd))return 1;
 strcpy(notice,"無法獨占按鍵，請關閉其他按鍵工具");return 0;
}
static void menu_modal_input_release(void){
 if(!nxl_input_lease_release(&menu_input_lease)){action=0;quitting=1;}
}
/* Closing a child's duplicate FDs must not issue UNGRAB on the parent's lease. */
static void menu_input_child_close(void){
 for(unsigned i=0;i<2;i++){if(menu_input_fd[i]>=0)close(menu_input_fd[i]);menu_input_fd[i]=-1;}
 menu_input_lease.mask=0;
}
''')
patch(p,'task_child_watch_detach();','task_child_watch_detach();menu_input_child_close();')
patch(p,'static void restore_and_close(void) {','static void restore_and_close(void) {\n    menu_modal_input_release();')
patch(p,'static void draw(void) {','static void draw(void) {\n    if(!detail_visible&&!lut_picker_open&&!video_picker_open)menu_modal_input_release();')
patch(p,'    for (int i=1;i<=2;++i) if (fds[i].fd>=0) close(fds[i].fd);\n    if(!browser_save())','    menu_modal_input_release();\n    for (int i=1;i<=2;++i) if (fds[i].fd>=0) close(fds[i].fd);\n    if(!browser_save())')
# Refresh menu caches from committed RAM state after a Log transition.
start=p.read_text('utf8').index('    video_enabled=access("/tmp/nx-lut-lite/session/video_enabled"',p.read_text('utf8').index('static void load_names'))
s=p.read_text('utf8');end=s.index('    browser_load();',start);block=s[start:end]
helper='static void refresh_lut_runtime(void){\n    FILE*f;char line[256];active_name[0]=0;active_strength=100;\n'+block+'}\n'
s=s[:start]+'    refresh_lut_runtime();\n'+s[end:];s=s.replace('static void load_names(void) {',helper+'static void load_names(void) {',1);s=s.replace('NX_LUT_RELEASE=0.4.0-RC2;','NX_LUT_RELEASE=0.4.0-RC3;');p.write_text(s,'utf8',newline='\n')
p=U/'menu_lut_detail_v2.inc'
patch(p,'if(count<=0||count>512)return 1;memcpy(detail_view','if(count<=0||count>512)return 1;if(!menu_modal_input_take())return 1;memcpy(detail_view')
patch(p,' detail_log_scope=1;ld_begin',' if(!menu_modal_input_take())return 1;\n detail_log_scope=1;ld_begin')
patch(p,'task_child_watch_detach();','task_child_watch_detach();menu_input_child_close();')
p=U/'menu_lut_picker.inc';patch(p,'static void lut_picker_begin(void){','static void lut_picker_begin(void){\n if(!menu_modal_input_take())return;')
p=U/'menu_video_picker.inc';patch(p,' video_picker_cursor=video_status.mode',' if(!menu_modal_input_take())return;\n video_picker_cursor=video_status.mode')
p=U/'menu_video.inc'
patch(p,'  gamma_command_failed=!ok;','  gamma_command_failed=!ok;\n  if(video_command==21||video_command==22)refresh_lut_runtime();')
patch(p,'FACTORY GAMMA QUEUED - RECORD TO VERIFY','原廠曲線已要求，LUT已還原')
patch(p,'LOG DIRECT / LOOKS LOCKED','純V‑Log已準備，原LUT已清除')
patch(p,'NX LUT Lite 0.4.0 RC2','NX LUT Lite 0.4.0 RC3')
# Reuse the existing verified OEM LUT restore transaction, without new ISP calls.
p=T/'base/nx-lut-lite/log-lut-policy.sh'
patch(p,' clean_log_factory_status || die GAMMA_RESTORE_REQUIRED\n marker=', ' clean_log_factory_status || die GAMMA_RESTORE_REQUIRED\n restore_factory || die ORIGINAL_LUT_RESTORE_UNCONFIRMED\n marker=')
patch(p,' [ -e "$CLEAN_LOG" ] || return 0\n clean_log_factory_status',' clean_log_factory_status')
p=T/'base/nx-lut-lite/gamma-reload.sh'
patch(p,'''        if [ -e "$RUN/clean-log.policy" ] || [ -L "$RUN/clean-log.policy" ];then
            engine_ok || fail CLEAN_LOG_GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED
            /bin/sh "$ENGINE" clean-log-release > "$RUN/clean-log-release.log" 2>&1 || fail CLEAN_LOG_GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED
        fi''','''        engine_ok || fail CLEAN_LOG_GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED
        /bin/sh "$ENGINE" clean-log-release > "$RUN/clean-log-release.log" 2>&1 || fail CLEAN_LOG_GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED''')
put(W/'preparation.json',dict(passed=True,release='0.4.0-RC3',temporary_input_ownership_for_modals=True,
 Log_toggles_reset_to_mode_baseline=True,SRW_native_Gamma_RTOS_unchanged=True,no_SD_writes=True))
print('PASS isolated RC3 changes',flush=True)
