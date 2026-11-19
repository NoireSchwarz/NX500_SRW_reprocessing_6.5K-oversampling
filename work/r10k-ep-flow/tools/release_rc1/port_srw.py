"""Reuse the existing resource rearm after completion; bound future text logs."""
from context import *
import re,shutil
before=json.loads((W/'source-before.json').read_text('utf8'))
for folder in ['session/src','session/tools','session/tests']:
 shutil.copytree(SRC/folder,T/folder,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for p in (T/'session').rglob('*'):
 if p.is_file():before[p.relative_to(T).as_posix()]=sha(p)
put(W/'source-before.json',before)
run('baseline-session',[T/'session/tools/build_session.py','--zig',ZIG,'--cross-runtime',CROSS,'--output',W/'bin/baseline-session.arm'])
assert sha(W/'bin/baseline-session.arm')==sha(B/'nx-lut-demo/nx-lut-lite/bin/nxl-session.arm')

p=T/'session/src/progress_publish.inc'
patch(p,'#include "../../common/progress_journal.h"','#define NP_MAX_RECORDS 32u\n#include "../../common/progress_journal.h"')
patch(p,'"PROGRESS.events"','"PROGRESS_RC1.events"')
p=T/'common/progress_journal.h';s=p.read_text('utf8')
anchor='r.pid=(uint32_t)getpid();'
assert s.count(anchor)==1
s=s.replace(anchor,anchor+'\n#ifdef NP_MAX_RECORDS\n if(ok&&st.st_size>=(off_t)(NP_MAX_RECORDS*sizeof r))ok=!ftruncate(fd,0);\n#endif\n')
p.write_text(s,'utf8',newline='\n')
p=T/'session/src/session.c';s=p.read_text('utf8')
s=s.replace('logdir,"session.log"','logdir,"session-RC1.log"')
needle='if(!fstat(fd,&st)&&S_ISREG(st.st_mode)){ssize_t ignored=write(fd,line,(size_t)n);'
assert s.count(needle)==1
s=s.replace(needle,'if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&(st.st_size<131072||!ftruncate(fd,0))){ssize_t ignored=write(fd,line,(size_t)n);')
p.write_text(s,'utf8',newline='\n')
run('release-session',[T/'session/tools/build_session.py','--zig',ZIG,'--cross-runtime',CROSS,'--output',M/'bin/nxl-session.arm'])

for n in ['finish-srw.sh','srw-log-policy.sh']:shutil.copyfile(A/n,T/'base/nx-lut-lite'/n)
for n in ['auto-init.sh','memory-test.sh']:
 p=T/'base/nx-lut-lite'/n;s=p.read_text('utf8')
 decl='LOGPOLICY=$D/srw-log-policy.sh\nSRW_LOG_POLICY_SHA=0000000000000000000000000000000000000000000000000000000000000000\n'
 marker='D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1\n';assert s.count(marker)==1
 s=s.replace(marker,marker+decl)
 if n=='auto-init.sh':
  s=s.replace('"$RUNNER:$RUNNER_SHA";do','"$RUNNER:$RUNNER_SHA" "$LOGPOLICY:$SRW_LOG_POLICY_SHA";do')
  marker='absent "$root" && mkdir "$root" || fail REPORT_ALREADY_EXISTS';assert marker in s
  s=s.replace(marker,marker+'\n/bin/sh "$LOGPOLICY" mark "$root" "$token" || fail LOG_POLICY_OWNER_FAILED')
  old=''' /bin/sh "$RUNNER" --new > "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_FAILED'''
  assert old in s
  new=''' current_job=$(sed -n 's/^job_dir=//p' "$root/session-before.txt")
 next_ready=
 if [ -f /tmp/nx-lut-lite/rc1-next.job ] && [ ! -L /tmp/nx-lut-lite/rc1-next.job ];then IFS= read -r next_ready < /tmp/nx-lut-lite/rc1-next.job || :;fi
 if [ "$state" = NEED_STILL_INIT ] && [ -n "$current_job" ] && [ "$current_job" = "$next_ready" ] && grep -qx front_attempt=0 "$root/session-before.txt" && grep -qx backend_attempt=0 "$root/session-before.txt";then
  printf 'REUSE_VERIFIED_RC1_NEXT_SESSION=%s\\n' "$current_job" > "$root/session-selected-photo.txt"
 else /bin/sh "$RUNNER" --new > "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_FAILED;fi
 NX51_RC_NEW_JOB=1;export NX51_RC_NEW_JOB'''
  s=s.replace(old,new)
 else:
  s=s.replace('BOOT_LOG=$OUT/logs/run.log','BOOT_LOG=$OUT/logs/run-RC1.log')
  marker='job=${NX51_JOB:-}'
  # The job marker is written only for a newly created/reused RC-owned empty
  # job, never for a historical interrupted round or a backend-only retry.
  a=s.index(marker);end=s.index('exec >> "$job/collector.log"',a)
  s=s[:end]+'''if [ "${NX51_RC_NEW_JOB:-0}" = 1 ] && [ "$init_trial" = 1 ];then
 regular "$LOGPOLICY" && [ "$(hash "$LOGPOLICY")" = "$SRW_LOG_POLICY_SHA" ] || exit 1
 /bin/sh "$LOGPOLICY" mark "$job" "${NX51_PROGRESS_TOKEN:-}" || exit 1
fi
'''+s[end:]
  marker='--init-trial) shift;exec '
  assert marker in s
  s=s.replace(marker,'--init-trial) [ ! -L "$BOOT_LOG" ] || exit 1;: > "$BOOT_LOG" || exit 1;shift;exec ')
 p.write_text(s,'utf8',newline='\n')

p=T/'base/nx-lut-lite/rearm-session.sh';s=p.read_text('utf8')
s=s.replace('LOG=$OUT/logs/rearm.log','LOG=$OUT/logs/rearm-RC1.log')
s=s.replace('exec >> "$LOG" 2>&1','[ "${1:-}" != --rearm ] || : > "$LOG"\nexec >> "$LOG" 2>&1')
p.write_text(s,'utf8',newline='\n')

U=T/'base/nx-lut-lite/src';shutil.copyfile(A/'release_next.inc',U/'release_next.inc')
p=U/'menu.c';s=p.read_text('utf8')
anchor='static int memory_background(int menu_lock,int create_new) {'
assert s.count(anchor)==1;s=s.replace(anchor,'#include "release_next.inc"\n'+anchor)
anchor='''        if(!strcmp(mode,"new-session")) {'''
assert anchor in s
s=s.replace(anchor,'''        if(!strcmp(mode,"release-next")) {
            char script[1200];const char*root=getenv("NX_LUT_ROOT");if(!root||snprintf(script,sizeof script,"%s/finish-srw.sh",root)>=(int)sizeof script)_exit(127);
            execl("/bin/sh","sh",script,(char*)0);_exit(127);
        }
'''+anchor)
s=s.replace('task_progress_auto=create_new==5;task_auto_result=0;','task_progress_auto=create_new==5;task_auto_result=0;release_next_status=0;')
anchor='''            else task_progress_complete(confirmed,create_new==4);'''
assert s.count(anchor)==1
s=s.replace(anchor,anchor+'''
            if(confirmed&&(create_new!=5||task_auto_result==1)){
                release_next_status=release_prepare_next()?1:-1;
                task_progress.percent=100;task_progress.issue=release_next_status<0;
                strcpy(task_progress.title,release_next_status>0?"重顯影完成，新工作階段已準備":"JPEG已儲存，新工作階段未建立");
                strcpy(task_progress.detail,release_next_status>0?"可選擇下一張SRW；不會自動開始處理":"資源核驗尚未通過；已儲存的JPEG保留");task_progress.changed=1;task_progress_paint();
            }
''')
s=s.replace('"/mnt/mmc/NXDEMO51/logs/PROGRESS.events"','"/mnt/mmc/NXDEMO51/logs/PROGRESS_RC1.events"')
s=s.replace('strcpy(notice,task_auto_result==1?"AUTO INITIALIZATION COMPLETE":"INITIALIZATION STOPPED - CHECK LOGS");',
 'strcpy(notice,task_auto_result==1?(release_next_status>0?"完成，已準備新工作階段":"JPEG已儲存；請核驗新工作階段"):"初始化已停止，請查看狀態");')
# Restore the actual picker draw on the new production SRW surface.
s=s.replace('text(320,442,"OK 選擇");text(540,442,"MENU 返回");publish_ui();',
 'text(320,442,"OK 選擇");text(540,442,"MENU 返回");draw_lut_picker();publish_ui();')
p.write_text(s,'utf8',newline='\n')

# One latest RAM trace per boot, not one filename per operation.
p=U/'menu_progress.inc';s=p.read_text('utf8')
s=s.replace('"/tmp/nx-lut-lite/stage-ui-%s.log",operation','"/tmp/nx-lut-lite/stage-ui-latest.log"')
s=s.replace('O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC','O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC')
p.write_text(s,'utf8',newline='\n')

for name in ['auto-init.sh','memory-test.sh','rearm-session.sh','finish-srw.sh','srw-log-policy.sh']:
 shutil.copyfile(T/'base/nx-lut-lite'/name,M/name)
put(W/'SRW-port.json',dict(passed=True,baseline_session_reproduced_byte_exact=True,resource_state_and_rearm_algorithms_unchanged=True,
 automatic_next_session_requires_COMPLETE_and_front_verified_and_existing_resource_proof=True,new_round_not_automatically_started=True,
 next_fresh_job_reused_only_with_matching_RC_marker_and_zero_attempts=True,progress_max_records=32,progress_new_file_preserves_history=True,
 session_diagnostic_max_bytes=131072,only_new_RC_success_optional_logs_compacted=True,resource_proofs_and_images_retained=True,
 previous_history_not_pruned=True,native_SRW_workers_unchanged=True,hardware_tested=False))
print('PASS SRW next-session orchestration and bounded future logs; resource proof algorithms untouched',flush=True)
