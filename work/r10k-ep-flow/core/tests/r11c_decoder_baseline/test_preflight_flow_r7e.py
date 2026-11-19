"""Execute actual shell workflow functions with modeled native/OS endpoints."""
from pathlib import Path
import json,os,re,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
SHELL=Path('C:/Program Files/Git/usr/bin/sh.exe')
OUT=R/'validation/r7e';OUT.mkdir(parents=True,exist_ok=True)
def function(text,name):
 m=re.search(r'^'+re.escape(name)+r'\(\)\{.*?^\}',text,re.M|re.S)
 assert m,name
 return m.group()+'\n'
def write(p,s):p.write_text(s,encoding='utf-8',newline='\n')
def run(p,env):
 return subprocess.run([str(SHELL),str(p.resolve())],env={**os.environ,**env},capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=20)
tests=[]
source=(R/'base/nx-lut-lite/memory-test.sh').read_text(encoding='utf-8')
functions='\n'.join(function(source,n) for n in ['run_front','cleanup'])
with tempfile.TemporaryDirectory(dir=OUT,prefix='front-flow-') as d:
 p=Path(d).resolve();write(p/'session.sh','''#!/bin/sh
printf '%s\n' "$*" >> "$FIX/calls"
if [ "$2" = fail ];then
 if [ "$3" = SAFE_NO_SUBMIT ];then printf RETRYABLE > "$FIX/state";else printf RESTART_REQUIRED > "$FIX/state";fi
fi
exit 0
''')
 for rc in [42,1,143,0]:
  write(p/'front.sh',f'#!/bin/sh\nexit {rc}\n');write(p/'state','FRONT_RUNNING');write(p/'calls','')
  script='''#!/bin/sh
set -u
export PATH=/usr/bin:/bin:$PATH
job=$FIX/job;OUT=$FIX/out;RUN=$FIX/run;SESSION=$FIX/session.sh;FRONT=$FIX/front.sh
mkdir -p "$job";LOOK=raw422;STRENGTH=100
phase=SETUP;reason=NOT_STARTED;disposition=SAFE_NO_SUBMIT;active_kind=none;terminal=ERROR
prepare_full=0;child=;native_verified=0;front_no_submit=0;cold_restart_required=0
engine_owned=1;trace_owned=1;front_attempt=1;backend_attempt=0;jpeg=;report=;state=FRONT_RUNNING
front_grant_ready(){ :; }
status_read(){ state=$(cat "$FIX/state"); }
reset_live_lut(){ :; }
record(){ :; }
notice(){ :; }
timing_begin(){ :; }
timing_end(){ :; }
sync(){ :; }
lock_release(){ printf 'RELEASE %s\n' "$1" >> "$FIX/calls"; }
lock_hold(){ printf 'HOLD %s\n' "$1" >> "$FIX/calls"; }
valid_front_path(){ return 1; }
fail(){ reason=$1;exit 1; }
'''+functions+'''
trap cleanup 0
trap 'reason=WORKFLOW_INTERRUPTED;exit 130' HUP INT TERM
run_front
'''
  write(p/'test.sh',script)
  # Every attempt gets its own real directory; no reuse of partial output.
  attempt=p/f'attempt-{rc}';attempt.mkdir();script=script.replace('job=$FIX/job;',f'job={attempt.as_posix()};');write(p/'test.sh',script)
  result=run(p/'test.sh',{'FIX':p.as_posix()});assert result.returncode==1,(rc,result)
  calls=(p/'calls').read_text();state=(p/'state').read_text()
  if rc==42:
   assert 'fail SAFE_NO_SUBMIT FRONT_NEEDS_STILL_INIT' in calls and calls.count('RELEASE ')==2 and 'HOLD ' not in calls and state=='RETRYABLE',calls
  else:
   assert 'fail UNKNOWN_REQUIRE_RESTART ' in calls and calls.count('HOLD ')==2 and 'RELEASE ' not in calls and state=='RESTART_REQUIRED',calls
  tests.append(dict(case='front-exit-'+str(rc),passed=True,calls=calls.splitlines()))

source=(R/'base/nx-lut-lite/rearm-session.sh').read_text(encoding='utf-8')
names=['preflight_workers_idle','preflight_grant_ready','preflight_lock_check','recover_known_preflight']
functions='\n'.join(function(source,n) for n in names).replace('/proc/','"$PROC"/').replace('/tmp/nx-lut-lite/','$RUN/')
for case in ['both-locks','locks-absent','engine-absent','wrong-reason','pid-present','unknown-leaf','evidence-failed','worker-active','grant-started','cancelled','lock-changed']:
 with tempfile.TemporaryDirectory(dir=OUT,prefix='hold-flow-') as d:
  p=Path(d).resolve();job=p/'JOB_test';job.mkdir();module=p/'module';(module/'state').mkdir(parents=True)
  proc=p/'proc';(proc/'self').mkdir(parents=True);write(proc/'self/status','Name:\tsh\n')
  write(module/'state/p49-experiment.state','NXP33INSTALL1\n'+('STARTED' if case=='grant-started' else 'READY')+'=0.3.6-demo50\n')
  runtime=p/'run';runtime.mkdir()
  for name in ['engine','trace']:
   if case=='locks-absent' or case=='engine-absent' and name=='engine':continue
   lock=runtime/(name+'.lock');lock.mkdir()
   write(lock/'NX51_OWNER',f'NX51LOCK1\njob={job.as_posix()}\nboot=test-boot\n')
   reason='OTHER' if case=='wrong-reason' and name=='engine' else 'FRONT_FAILED_NATIVE_STATE_UNVERIFIED'
   write(lock/'RESTART_REQUIRED',f'NX51HOLD1\njob={job.as_posix()}\nboot=test-boot\nreason={reason}\n')
  if case=='pid-present':write(runtime/'trace.lock/pid','123\n')
  if case=='unknown-leaf':write(runtime/'engine.lock/foreign','x')
  if case=='worker-active':
   (proc/'999').mkdir();write(proc/'999/status','Name:\tnxl-demo-front\n')
  write(p/'ops.sh','''#!/bin/sh
case "$1" in same) cmp -s "$2" "$3";;unlock) rmdir "$2";;*) exit 19;;esac
''')
  write(p/'session.sh','''#!/bin/sh
case "$2" in
status) printf 'reason=FRONT_FAILED_NATIVE_STATE_UNVERIFIED\nboot=test-boot\n';;
verify-preflight-hold)
 [ "${CASE}" != evidence-failed ] || exit 1
 n=0;[ ! -f "$FIX/verifies" ] || n=$(cat "$FIX/verifies");n=$((n+1));printf %s "$n" > "$FIX/verifies"
 if [ "$CASE" = lock-changed ] && [ "$n" = 2 ];then printf altered >> "$RUN/engine.lock/RESTART_REQUIRED";fi;;
*) exit 7;;esac
''')
  script='''#!/bin/sh
set -u
export PATH=/usr/bin:/bin:$PATH
OUT=$FIX;job=$FIX/JOB_test;D=$FIX/module;SESSION=$FIX/session.sh;OPS=$FIX/ops.sh
PROC=$FIX/proc;RUN=$FIX/run;token=01234567890123456789012345678901;cancelled=0
[ "$CASE" != cancelled ] || cancelled=1
mkdir "$PROC/$PPID";printf 'Name:\tnxl-session.arm\n' > "$PROC/$PPID/status"
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
hash(){ sha256sum "$1" | cut -d ' ' -f1; }
sync(){ :; }
reject(){ printf 'HELD %s\n' "$1";exit 1; }
'''+functions+'''
recover_known_preflight
'''
  write(p/'test.sh',script)
  result=run(p/'test.sh',{'FIX':p.as_posix(),'RUN':runtime.as_posix(),'CASE':case})
  good=case in ['both-locks','locks-absent','engine-absent']
  assert (result.returncode==0)==good,(case,result.stdout,result.stderr,result.returncode)
  if good:
   assert not list(runtime.iterdir()),case
   assert 'R7E_PREFLIGHT_FALSE_HOLD_VERIFIED' in result.stdout
   if case=='both-locks':assert len(list(job.glob('preflight_recovery_*/*')))==4
  else:assert (runtime/'trace.lock/RESTART_REQUIRED').is_file(),case
  tests.append(dict(case=case,passed=True,exit=result.returncode,stdout=result.stdout.strip()))

assert 'if(poll_menu_rearm())draw();' in (R/'base/nx-lut-lite/src/menu.c').read_text(encoding='utf-8')
assert source.index('recover_known_preflight\n')<source.index('"$PROBE" --job')
result=dict(passed=True,hardware_tested=False,tests=tests,scope='Actual shell functions; modeled native exit, supervisor/proc and workflow-lock OS boundaries')
(OUT/'shell-flows.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('PASS',len(tests),'actual shell flow cases; init rejection releases workflow locks, unknown native outcomes remain held; legacy recovery rejects changed evidence/locks/workers')
