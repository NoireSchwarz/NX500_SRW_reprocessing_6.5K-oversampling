"""Real completion/log shells with only session/resource and durability endpoints modeled."""
from context import *
import re,shutil
sys.path[:0]=[str(PY),str(SRC/'tools/log_video')]
from test_reload import posix
V=next(W/('tests/srw-'+str(i))for i in range(1,100)if not(W/('tests/srw-'+str(i))).exists());V.mkdir(parents=True)
runtime=V/'runtime';(runtime/'bin').mkdir(parents=True);rr=V/'run';rr.mkdir();out=V/'NXDEMO51';(out/'logs').mkdir(parents=True)
ops=runtime/'bin/nxl-fileops';ops.write_text('#!/bin/sh\ncase "$1" in sha256) sha256sum "$2" | cut -d " " -f 1;;same) cmp -s "$2" "$3";;*) exit 2;;esac\n','ascii',newline='\n')
for n in ['sync']:(runtime/'bin'/n).write_text('#!/bin/sh\nexit 0\n','ascii',newline='\n')
# Narrow compaction to wrapper text only. Native allocation/retirement logs
# may be consumed by the existing proof verifier and are never compacted.
p=A/'srw-log-policy.sh';s=p.read_text('utf8').replace('-name probe.log -o -name runner.log -o -name reads.log -o -name collector.log -o -name install-phases.log -o -name native-commands.log -o -name stdout.log -o -name stderr.log','-name runner.log -o -name collector.log -o -name stdout.log -o -name stderr.log').replace('probe.log|runner.log|reads.log|collector.log|install-phases.log|native-commands.log|stdout.log|stderr.log','runner.log|collector.log|stdout.log|stderr.log')
p.write_text(s,'utf8',newline='\n')
for n in ['finish-srw.sh','srw-log-policy.sh']:
 s=(A/n).read_text('utf8').replace('/mnt/mmc/NXDEMO51',posix(out)).replace('/tmp/nx-lut-lite',posix(rr))
 s=re.sub('^OPS_SHA=[0-9a-f]{64}$','OPS_SHA='+sha(ops),s,flags=re.M);(runtime/n).write_text(s,'utf8',newline='\n')
runner=runtime/'memory-test.sh'
runner.write_text(r'''#!/bin/sh
set -eu
. "$FX_CONTROL"
if [ "$1" = --rearm ];then
 printf '%s\n' "$*" >> "$FX_CALLS"
 [ "${FAIL_REARM:-0}" = 0 ] || exit 1
 : > "$FX_AFTER"
 exit 0
fi
[ "$1" = --status ] || exit 2
if [ -f "$FX_AFTER" ];then state=${AFTER_STATE:-NEED_STILL_INIT};job=${AFTER_JOB};reset=${AFTER_RESET:-0};fa=${AFTER_FA:-0};ba=${AFTER_BA:-0}
else state=${BEFORE_STATE:-COMPLETE};job=${BEFORE_JOB};reset=1;fa=1;ba=1;fi
printf 'NX51STATUS1\nstate=%s\nfront_verified=%s\nrequires_reset=%s\nfront_attempt=%s\nbackend_attempt=%s\njob_dir=%s\n' "$state" "${VERIFIED:-1}" "$reset" "$fa" "$ba" "$job"
[ "${DUPLICATE:-0}" = 0 ] || printf 'state=RESTART_REQUIRED\n'
printf 'end=NX51END1\n'
''','ascii',newline='\n')
p=runtime/'finish-srw.sh';s=p.read_text('utf8');s=re.sub('^RUNNER_SHA=[0-9a-f]{64}$','RUNNER_SHA='+sha(runner),s,flags=re.M);s=re.sub('^SRW_LOG_POLICY_SHA=[0-9a-f]{64}$','SRW_LOG_POLICY_SHA='+sha(runtime/'srw-log-policy.sh'),s,flags=re.M);p.write_text(s,'utf8',newline='\n')
env=dict(ENV,PATH=str(runtime/'bin')+';'+r'C:\Program Files\Git\usr\bin;'+os.environ.get('PATH',''),FX_CONTROL=posix(V/'control.sh'),FX_CALLS=posix(V/'calls.log'),FX_AFTER=posix(V/'after'),NX51_PROGRESS_TOKEN='0123456789abcdef0123456789abcdef',MSYS2_ARG_CONV_EXCL='*')
records=[]
def control(**kw):
 vals=dict(BEFORE_JOB=posix(out/'JOB_OLD'),AFTER_JOB=posix(out/'JOB_NEW'));vals.update(kw)
 (V/'control.sh').write_text(''.join(k+"='"+str(v)+"'\n"for k,v in vals.items()),'utf8',newline='\n')
 for f in [V/'after',V/'calls.log',rr/'rc1-next.job']:
  if f.exists():f.unlink()
def call(n,*args,ok=True,contains=None):
 p=subprocess.run([r'C:\Program Files\Git\usr\bin\sh.exe',posix(runtime/n),*args],env=env,capture_output=True,text=True,encoding='utf8',timeout=30)
 row=dict(script=n,args=args,exit=p.returncode,output=p.stdout+p.stderr);records.append(row);assert(p.returncode==0)==ok,row
 if contains:assert contains in row['output'],row
 return row
for kw in [dict(BEFORE_STATE='RETRYABLE'),dict(BEFORE_STATE='RESTART_REQUIRED'),dict(BEFORE_STATE='FRONT_RUNNING'),dict(VERIFIED=0),dict(DUPLICATE=1),dict(BEFORE_JOB='/foreign/JOB_OLD')]:
 control(**kw);call('finish-srw.sh',ok=False);assert not(V/'calls.log').exists()and not(rr/'rc1-next.job').exists()
for kw in [dict(FAIL_REARM=1),dict(AFTER_STATE='RESTART_REQUIRED'),dict(AFTER_RESET=1),dict(AFTER_FA=1),dict(AFTER_BA=1),dict(AFTER_JOB=posix(out/'JOB_OLD'))]:
 control(**kw);call('finish-srw.sh',ok=False);assert not(rr/'rc1-next.job').exists()
control();call('finish-srw.sh',contains='ready=1');assert(rr/'rc1-next.job').read_text().strip()==posix(out/'JOB_NEW');assert(V/'calls.log').read_text().strip()=='--rearm'
token=env['NX51_PROGRESS_TOKEN'];job=out/'JOB_NEW_ROUND';init=out/('INIT_AUTO_'+token)
for root in [job,init]:
 root.mkdir();(root/'nested').mkdir()
 for name in ['NATIVE.JPG','proof.bin','result.json','events.log','p39_commands.log','probe.log','reads.log','native-commands.log']:(root/name).write_bytes(b'KEEP_PROOF_'+name.encode())
 (root/'nested/collector.log').write_bytes(b'OPTIONAL'*5000)
 (root/'runner.log').write_bytes(b'WRAPPER')
snapshot={str(p):sha(p)for root in [job,init]for p in root.rglob('*')if p.is_file()}
call('srw-log-policy.sh','complete',posix(job),posix(init),token,contains='RETAINED_HISTORICAL');assert all(sha(k)==v for k,v in snapshot.items())
for root in [job,init]:call('srw-log-policy.sh','mark',posix(root),token)
call('srw-log-policy.sh','complete',posix(job),posix(init),token,contains='compacted=4')
assert(out/'logs/SRW_RC1_latest.log').stat().st_size<40000
for name,h in snapshot.items():
 p=Path(name)
 if p.name in ['collector.log','runner.log']:assert not p.exists()
 else:assert sha(p)==h
# Same fixed latest log is reused. No new diagnostic history is generated.
(job/'collector.log').write_text('SECOND_ROUND');call('srw-log-policy.sh','complete',posix(job),posix(init),token)
assert'SECOND_ROUND'in(out/'logs/SRW_RC1_latest.log').read_text()and len(list((out/'logs').iterdir()))==1
# A foreign fixed-leaf file blocks compaction and preserves optional source.
(out/'logs/SRW_RC1_latest.log').write_text('USER_DATA');(job/'runner.log').write_text('PRESERVE')
call('srw-log-policy.sh','complete',posix(job),posix(init),token,ok=False,contains='NOT_OWNED');assert(job/'runner.log').read_text()=='PRESERVE'
call('srw-log-policy.sh','mark',posix(V),token,ok=False)
put(W/'SRW-completion-shell-validation.json',dict(passed=True,cases=len(records),commands=records,
 real_production_shell_control_flow=True,modeled_endpoints=['existing session --status/--rearm and resource proof','filesystem sync durability'],
 new_session_only_after_COMPLETE_and_rearm=True,no_automatic_render=True,duplicate_status_rejected=True,
 old_history_and_native_resource_proofs_preserved=True,optional_wrapper_logs_latest_only=True,hardware_tested=False))
print('PASS',len(records),'SRW completion and bounded optional-log cases',flush=True)
