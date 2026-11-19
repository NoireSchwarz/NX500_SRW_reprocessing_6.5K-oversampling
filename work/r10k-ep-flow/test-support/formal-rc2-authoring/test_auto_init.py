"""Actual auto-init shell: session/resource/native endpoints are explicitly modeled."""
from context import *
import re
V=next(W/('tests/auto-init-'+str(i))for i in range(1,100)if not(W/('tests/auto-init-'+str(i))).exists());V.mkdir(parents=True)
def posix(p):
 s=Path(p).absolute().as_posix();return '/'+s[0].lower()+s[2:]
def write(p,s):
 p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,'utf8',newline='\n')
rows=[]
cases=[
 ('first',{},True,['--new','--init-trial raw422 100']),
 ('complete',dict(BEFORE_STATE='COMPLETE'),True,['--rearm','--init-trial raw422 100']),
 ('restart',dict(BEFORE_STATE='RESTART_REQUIRED'),True,['--rearm','--init-trial raw422 100']),
 ('cold',dict(READY_BEFORE=42,READY_AGAIN=42),True,['--new','--init-trial raw422 100']),
 ('rearm-fails',dict(BEFORE_STATE='COMPLETE',REARM_FAIL=1),False,['--rearm']),
 ('rearm-held',dict(BEFORE_STATE='COMPLETE',AFTER_STATE='RESTART_REQUIRED'),False,['--rearm']),
 ('rearm-reset',dict(BEFORE_STATE='COMPLETE',AFTER_RESET=1),False,['--rearm']),
 ('rearm-same-job',dict(BEFORE_STATE='COMPLETE',AFTER_JOB='OLD'),True,['--rearm','--new','--init-trial raw422 100']),
 ('rearm-used-job',dict(BEFORE_STATE='COMPLETE',AFTER_FA=1),True,['--rearm','--new','--init-trial raw422 100']),
 ('rearm-look',dict(BEFORE_STATE='COMPLETE',AFTER_LOOK='film'),True,['--rearm','--new','--init-trial raw422 100']),
 ('new-fails',dict(NEW_FAIL=1),False,['--new']),
 ('new-held',dict(NEW_STATE='RESTART_REQUIRED'),False,['--new']),
 ('readiness-changes',dict(READY_BEFORE=42,READY_AGAIN=0),False,['--new']),
 ('pipeline-fails',dict(PIPE_FAIL=1),False,['--new','--init-trial raw422 100']),
 ('no-return-proof',dict(NO_RETURN=1),False,['--new','--init-trial raw422 100']),
 ('not-complete',dict(FINAL_STATE='FRONT_READY'),False,['--new','--init-trial raw422 100']),
]
runner=r'''#!/bin/sh
set -eu
. "$FX_CONTROL"
status(){
 phase=before;[ ! -f "$FX_PHASE" ] || phase=$(cat "$FX_PHASE")
 state=$BEFORE_STATE;job=OLD;reset=0;fa=0;look=-
 case "$phase" in
 rearm) state=$AFTER_STATE;job=$AFTER_JOB;reset=$AFTER_RESET;fa=$AFTER_FA;look=$AFTER_LOOK;;
 new) state=$NEW_STATE;job=SELECTED;;
 complete) state=$FINAL_STATE;job=SELECTED;;
 esac
 printf 'NX51STATUS1\nstate=%s\njob_dir=%s/JOB_%s\nrequires_reset=%s\nfront_attempt=%s\nbackend_attempt=0\nlook_id=%s\nfront_verified=1\nend=NX51END1\n' "$state" "$FX_OUT" "$job" "$reset" "$fa" "$look"
}
case "$1" in
 --status) status;;
 --rearm) printf '%s\n' "$*" >> "$FX_CALLS";[ "$REARM_FAIL" = 0 ] || exit 1;printf rearm > "$FX_PHASE";status;;
 --new) printf '%s\n' "$*" >> "$FX_CALLS";[ "$NEW_FAIL" = 0 ] || exit 1;printf new > "$FX_PHASE";status;;
 --init-trial)
  printf '%s\n' "$*" >> "$FX_CALLS";[ "${NX51_RC_NEW_JOB:-}" = 1 ];[ "$PIPE_FAIL" = 0 ] || exit 1
  [ "$NO_RETURN" = 1 ] || { mkdir -p "$NX51_INIT_REPORT/ep-close";printf returned > "$NX51_INIT_REPORT/ep-close/EP.returned"; }
  printf complete > "$FX_PHASE";;
 *) exit 2;;
esac
'''
for label,options,ok,expected in cases:
 c=V/label;runtime=c/'runtime';out=c/'NXDEMO51';rr=c/'run';out.mkdir(parents=True);rr.mkdir()
 vals=dict(BEFORE_STATE='NEED_STILL_INIT',AFTER_STATE='NEED_STILL_INIT',AFTER_RESET=0,AFTER_FA=0,AFTER_LOOK='-',AFTER_JOB='FRESH',NEW_STATE='NEED_STILL_INIT',FINAL_STATE='COMPLETE',REARM_FAIL=0,NEW_FAIL=0,PIPE_FAIL=0,NO_RETURN=0,READY_BEFORE=0,READY_AGAIN=0)
 vals.update(options);write(c/'control.sh',''.join(k+"='"+str(v)+"'\n"for k,v in vals.items()))
 write(runtime/'bin/nxl-fileops','#!/bin/sh\n[ "$1" = sha256 ] || exit 2\nsha256sum "$2" | cut -d " " -f 1\n')
 write(runtime/'bin/nxl-init-test.arm',r'''#!/bin/sh
. "$FX_CONTROL"
case "$1" in --ready-idle) case "$2" in */before) exit "$READY_BEFORE";;*) exit "$READY_AGAIN";;esac;;--readiness) exit 0;;*) exit 2;;esac
''')
 write(runtime/'memory-test.sh',runner)
 write(runtime/'lens-capture.sh','#!/bin/sh\nexit 1\n')
 write(runtime/'srw-sap-service.sh','#!/bin/sh\nprintf "sap %s\\n" "$1" >> "$FX_AUX"\nexit 0\n')
 write(runtime/'srw-log-policy.sh','#!/bin/sh\nprintf "log %s\\n" "$1" >> "$FX_AUX"\nexit 0\n')
 write(runtime/'process.sh','nxl_require_normal_mode(){ return 0; }\n')
 write(runtime/'progress.sh','nxl_progress(){ printf "%s %s\\n" "$1" "$2" >> "$FX_PROGRESS"; }\n')
 write(runtime/'bin/sync','#!/bin/sh\nexit 0\n')
 s=(T/'base/nx-lut-lite/auto-init.sh').read_text('utf8').replace('/mnt/mmc/NXDEMO51',posix(out)).replace('/tmp/nx-lut-lite',posix(rr))
 for key,name in [('OPS_SHA','bin/nxl-fileops'),('INIT_TEST_SHA','bin/nxl-init-test.arm'),('RUNNER_SHA','memory-test.sh'),('LENS_CAPTURE_SHA','lens-capture.sh'),('SAP_GUARD_SHA','srw-sap-service.sh'),('SRW_LOG_POLICY_SHA','srw-log-policy.sh')]:
  s,n=re.subn('^'+key+'=[0-9a-f]{64}$',key+'='+sha(runtime/name),s,flags=re.M);assert n==1
 write(runtime/'auto-init.sh',s)
 env=dict(ENV,PATH=str(runtime/'bin')+';'+r'C:\Program Files\Git\usr\bin;'+os.environ.get('PATH',''),MSYS2_ARG_CONV_EXCL='*',
  FX_CONTROL=posix(c/'control.sh'),FX_PHASE=posix(c/'phase'),FX_CALLS=posix(c/'calls'),FX_OUT=posix(out),FX_AUX=posix(c/'aux'),FX_PROGRESS=posix(c/'progress'),
  NX51_PROGRESS_TOKEN='0123456789abcdef0123456789abcdef',NX51_SOURCE_SRW='/mnt/mmc/DCIM/TEST.SRW')
 p=subprocess.run([r'C:\Program Files\Git\usr\bin\sh.exe',posix(runtime/'auto-init.sh')],env=env,capture_output=True,text=True,encoding='utf8',timeout=30)
 calls=(c/'calls').read_text().splitlines()if(c/'calls').exists()else[]
 reports=list(out.glob('INIT_AUTO_*'));assert len(reports)==1,(label,p.stdout,p.stderr)
 root=reports[0];log=(root/'runner.log').read_text()if(root/'runner.log').exists()else''
 assert (p.returncode==0)==ok,(label,p.returncode,p.stdout,p.stderr,log,(c/'progress').read_text())
 assert calls==expected,(label,calls,expected)
 assert (root/'MENU.result').exists()==ok,label
 aux=(c/'aux').read_text();assert ('log complete\n'in aux)==ok,(label,aux)
 # No asynchronous next-session call after rendering. The session remains COMPLETE.
 if ok:assert calls[-1]=='--init-trial raw422 100'and(c/'phase').read_text()=='complete'
 rows.append(dict(case=label,passed=True,exit=p.returncode,session_calls=calls,ready_receipt=ok))
 print('PASS shell',label,flush=True)
put(W/'auto-init-validation.json',dict(passed=True,cases=rows,production_shell_control_flow=True,
 modeled_endpoints=['session resource proof/status/new/rearm/native rendering','readiness/SAP/fileops/sync/log compaction'],
 source_sha256=sha(T/'base/nx-lut-lite/auto-init.sh'),no_post_completion_new_session=True,hardware_tested=False))
