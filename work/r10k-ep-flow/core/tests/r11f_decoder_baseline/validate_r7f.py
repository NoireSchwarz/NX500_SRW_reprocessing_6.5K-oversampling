"""R7f actual ARM journal verifier + complete installer and lock lifecycle tests."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,tempfile
R=Path(__file__).resolve().parents[1];W=R.parent;OUT=R/'validation/r7f';OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(W/'tooling/python'))
from session_file_model import SessionFileModel
SHELL=Path('C:/Program Files/Git/usr/bin/sh.exe');PY=Path(sys.executable).as_posix()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def text(p,s):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8',newline='\n')
def posix(p):
 s=Path(p).resolve().as_posix();return '/'+s[0].lower()+s[2:] if s[1:3]==':/' else s
def shell_run(p,env):
 return subprocess.run([str(SHELL),str(p.resolve())],env={**os.environ,**env},capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=45)
records=[];binary=R/'session/bin/nxl-session.arm';template=OUT/'fixture'
job='/mnt/mmc/NXDEMO51/JOB_1793879945_417_6f17b4f9';boot='dbb7e6ce-8b5e-4d2a-ba3e-bf5ea16d54bc'
if not template.exists():
 shutil.copytree(R/'session/tests/fixtures/r7f-install',template)
 for journal in template.rglob('STATE.journal'):
  for front in re.findall(r'^front=(/[^\n]+)',journal.read_text(encoding='utf-8'),re.M):(template/front.lstrip('/')).mkdir(parents=True,exist_ok=True)
 (template/'mnt/mmc/NXDEMO51/logs').mkdir(parents=True,exist_ok=True)
argv=['check-install-preflight',job,boot]
def arm_case(name,fixture=template,args=argv,busy=False,expected=76):
 before={str(p.relative_to(fixture)):sha(p) for p in fixture.rglob('*') if p.is_file()}
 model=SessionFileModel(binary,args,fixture,busy=busy);result=model.run()
 assert result['exit']==expected,(name,result)
 after={str(p.relative_to(fixture)):sha(p) for p in fixture.rglob('*') if p.is_file()}
 assert before==after,(name,'fixture evidence was changed')
 assert all(p=='/mnt/mmc/NXDEMO51/logs/session.log' for p in model.writes)
 records.append(dict(name=name,**result))
arm_case('exact field journals and preflight evidence accepted',expected=0)
pin_source=(R/'session/src/preflight_hold.inc').read_text(encoding='utf-8')
for leaf,_,_ in re.findall(r'\{"([^"]+)","([0-9a-f]{64})",(\d+)\}',pin_source):
 p=template/job.lstrip('/')/leaf;old=p.read_bytes();p.write_bytes((bytes([old[0]^1])+old[1:]) if old else b'x')
 try:arm_case('reject changed '+leaf)
 finally:p.write_bytes(old)
for leaf in ['CURRENT.journal','JOB_1793879945_417_6f17b4f9/STATE.journal']:
 p=template/'mnt/mmc/NXDEMO51'/leaf;old=p.read_bytes();p.write_bytes(old[:-1]+b'x')
 try:arm_case('reject torn '+leaf)
 finally:p.write_bytes(old)
arm_case('reject mismatched job',args=['check-install-preflight',job+'_other',boot])
arm_case('reject mismatched saved boot',args=['check-install-preflight',job,boot+'0'])
arm_case('reject active supervisor run lock',busy=True)
p=template/'opt/usr/nx-lut-lite/state/p49-experiment.state';old=p.read_bytes();p.write_bytes(b'NXP33INSTALL1\nSTARTED=0.3.6-demo50\n')
try:arm_case('reject consumed native grant')
finally:p.write_bytes(old)
print('PASS',len(records),'actual production ARM entrypoint cases; all state/evidence bytes unchanged',flush=True)

def wrapper(path,mode):text(path,f"#!/bin/sh\nexec '{PY}' '{(R/'tools/installer_os_model.py').resolve().as_posix()}' {mode} \"$@\"\n")
def function(s,n):
 m=re.search(r'^'+re.escape(n)+r'\(\)\{.*?^\}',s,re.M|re.S);assert m,n;return m.group()+'\n'
deploy=(R/'installer/deploy.sh.in').read_text(encoding='utf-8')
install_records=[]
for case in ['upgrade-preserves-holds','repeat-upgrade','wrong-hold-reason','foreign-leaf','pid-present','evidence-changed','busy-supervisor','promote-failure','worker-appears-before-switch']:
 with tempfile.TemporaryDirectory(dir=OUT,prefix='installer-') as td:
  f=Path(td).resolve();shutil.copytree(template,f,dirs_exist_ok=True);sd=f/'mnt/mmc';pkg=sd/'nx-lut-demo';src=pkg/'nx-lut-lite';dest=f/'opt/usr/nx-lut-lite';run=f/'tmp/nx-lut-lite';mock=f/'mock';mock.mkdir();run.mkdir(parents=True)
  files={'README_zh.md':'NX LUT Lite new\n','lut.sh':'# NX_LUT_DYNAMIC24_V1\n','memory-test.sh':'#!/bin/sh\nexit 97\n','start.sh':'#!/bin/sh\nexit 98\n',
   'bluetoothd-wrapper.sh':'#!/bin/sh\n# NX_LUT_LITE_BT_HOOK_V1\n# NX_LUT_LITE_DEMO51_BT_HOOK_V1\nexec /usr/sbin/bluetoothd.nxl-original "$@"\n',
   'process.sh':'''nxl_process_running(){
 if [ "${NX_TEST_WORKER:-0}" = 1 ] && [ -f "$NX_TEST_FIXTURE/listener-stopped" ];then return 0;fi
 return 1
}
nxl_require_no_keyscan(){ return 0; }
nxl_stop_listener(){ touch "$NX_TEST_FIXTURE/listener-stopped"; }
'''}
  for n,s in files.items():text(src/n,s)
  text(dest/'README_zh.md','NX LUT Lite old\n');text(dest/'lut.sh','# NX_LUT_DYNAMIC24_V1\n');text(dest/'keep-old','old module preserved\n')
  stock=b'\x7fELF\1\1\1'+b'\0'*11+b'\x28\0'+b'STOCK_DAEMON_MODEL'
  bt=f/'usr/sbin/bluetoothd';bt.parent.mkdir(parents=True);bt.write_bytes(stock)
  text(f/'etc/version.info','1.12\nNX500\n');text(f/'lib/ld-linux.so.3','MODEL ARM LOADER')
  source=b'MODEL_SOURCE_NOT_RAW';(pkg/'assets').mkdir(parents=True);(pkg/'assets/SAM_0183.SRW').write_bytes(source)
  wrapper(pkg/'patch-fileops','fileops');wrapper(pkg/'bin/nxl-holdcheck.arm','holdcheck');wrapper(pkg/'bin/nxl-session.arm','session');wrapper(mock/'mv','mv')
  for name in ['prefman','sync','systemctl']:text(mock/name,'#!/bin/sh\nexit 0\n')
  sd_posix=posix(sd);job_posix=sd_posix+'/NXDEMO51/'+job.split('/')[-1]
  for name in ['engine','trace']:
   lock=run/(name+'.lock');lock.mkdir()
   text(lock/'NX51_OWNER',f'NX51LOCK1\njob={job_posix}\nboot={boot}\n')
   text(lock/'RESTART_REQUIRED',f'NX51HOLD1\njob={job_posix}\nboot={boot}\nreason=FRONT_FAILED_NATIVE_STATE_UNVERIFIED\n')
  if case=='wrong-hold-reason':text(run/'engine.lock/RESTART_REQUIRED',(run/'engine.lock/RESTART_REQUIRED').read_text().replace('FRONT_FAILED_NATIVE_STATE_UNVERIFIED','NATIVE_UNKNOWN'))
  if case=='foreign-leaf':text(run/'trace.lock/FOREIGN','keep')
  if case=='pid-present':text(run/'engine.lock/pid','99999\n')
  if case=='evidence-changed':
   p=f/job.lstrip('/')/'front_1/front/case_00/result.json';p.write_bytes(p.read_bytes()+b' ')
  held_before={str(p.relative_to(run)):sha(p) for p in run.rglob('*') if p.is_file()}
  journal_before=sha(f/job.lstrip('/')/'STATE.journal')
  manifest=''.join(sha(p)+' '+p.relative_to(src).as_posix()+'\n' for p in sorted(src.rglob('*')) if p.is_file());text(pkg/'module.manifest',manifest)
  script=deploy.replace('task_root=\n','task_root='+posix(f)+'\n').replace('PATH=/usr/local/sbin:','PATH='+posix(mock)+':/usr/bin:/usr/local/sbin:')
  # NTFS/Git-sh cannot represent the Linux executable bit of an ARM ELF.
  # Its bytes still pass the file utility's ELF identity check below.
  script=script.replace('[ -x "$BTBACK" ]','[ -f "$BTBACK" ]')
  for key,target in [('MANIFEST_SHA',pkg/'module.manifest'),('OPS_SHA',pkg/'patch-fileops'),('HOLDCHECK_SHA',pkg/'bin/nxl-holdcheck.arm')]:script=re.sub(r'^'+key+r'=[^\n]+',key+'='+sha(target),script,flags=re.M)
  script=script.replace('@SESSION_CHECK_SHA256@',sha(pkg/'bin/nxl-session.arm')).replace('dfd6608e4ee05f0d5b4b402fedd3c928aee509595e6964efa4c1ce87f1d9715c',hashlib.sha256(source).hexdigest())
  text(pkg/'deploy.sh',script)
  env=dict(NX_TEST_FIXTURE=f.as_posix(),NX_TEST_DEPS=(W/'tooling/python').resolve().as_posix(),NX_TEST_ARM=binary.resolve().as_posix(),
   NX_TEST_BUSY='1' if case=='busy-supervisor' else '0',NX_TEST_FAIL_PROMOTE='1' if case=='promote-failure' else '0',NX_TEST_WORKER='1' if case=='worker-appears-before-switch' else '0')
  result=shell_run(pkg/'deploy.sh',env);log_path=sd/'NXDEMO51/logs/install.log';log=log_path.read_text(encoding='utf-8',errors='replace') if log_path.exists() else result.stdout
  passed=case in ['upgrade-preserves-holds','repeat-upgrade']
  assert (result.returncode==0)==passed,(case,result.returncode,result.stderr,log)
  held_after={str(p.relative_to(run)):sha(p) for p in run.rglob('*') if p.is_file() and p.parent.name.endswith('.lock')}
  assert held_before==held_after,(case,'installer changed workflow holds')
  assert sha(f/job.lstrip('/')/'STATE.journal')==journal_before,(case,'journal changed')
  assert not (f/'tmp/nx-lut-demo-setup.lock').exists()
  if passed:
   assert log.count('KNOWN_NO_SUBMIT_HOLDS_PRESERVED')==2 and 'INSTALL_COMPLETE' in log
   assert (dest/'state/p49-experiment.state').read_bytes()==b'NXP33INSTALL1\nREADY=0.3.6-demo50\n'
   assert (f/'usr/sbin/bluetoothd.nxl-original').read_bytes()==stock
   if case=='repeat-upgrade':
    result=shell_run(pkg/'deploy.sh',env);log=(sd/'NXDEMO51/logs/install.log').read_text(encoding='utf-8',errors='replace');assert result.returncode==0,(case,result.stderr,log)
    assert log.count('KNOWN_NO_SUBMIT_HOLDS_PRESERVED')==4
  else:assert bt.read_bytes()==stock and (dest/'keep-old').read_text()=='old module preserved\n',(case,'original installation not restored')
  install_records.append(dict(case=case,exit=result.returncode,passed=True,holds_unchanged=True,journal_unchanged=True))
  print('PASS complete installer:',case,flush=True)

# Execute real lock creation/ownership/release functions across two fresh leases.
workflow=(R/'base/nx-lut-lite/memory-test.sh').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(dir=OUT,prefix='rounds-') as td:
 f=Path(td).resolve();wrapper(f/'ops','fileops')
 lock_code='\n'.join(function(workflow,n) for n in ['lock_owned','lock_take'])
 release=re.search(r'^lock_release\(\).*$',workflow,re.M).group()
 script='''#!/bin/sh
set -eu
PATH=/usr/bin:/bin:$PATH;export PATH
job=/model/JOB_test;lock_boot=test-boot;OPS=$NX_TEST_FIXTURE/ops;lock=$NX_TEST_FIXTURE/engine.lock
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
'''+lock_code+release+'''
NX51_SESSION_TOKEN=11111111111111111111111111111111
lock_take "$lock";cp "$lock/NX51_ROUND" "$NX_TEST_FIXTURE/round1"
lock_owned "$lock";lock_release "$lock";absent "$lock"
NX51_SESSION_TOKEN=22222222222222222222222222222222
lock_take "$lock";cp "$lock/NX51_ROUND" "$NX_TEST_FIXTURE/round2"
NX51_SESSION_TOKEN=11111111111111111111111111111111
if lock_release "$lock";then exit 91;fi
[ -d "$lock" ]
NX51_SESSION_TOKEN=22222222222222222222222222222222
lock_release "$lock";absent "$lock"
'''
 text(f/'test.sh',script);result=shell_run(f/'test.sh',dict(NX_TEST_FIXTURE=f.as_posix()))
 assert result.returncode==0,(result.stdout,result.stderr)
 assert (f/'round1').read_bytes()!=(f/'round2').read_bytes()
 print('PASS actual lock lifecycle: two independent round tokens; old token cannot release new round',flush=True)
reset_source=(R/'base/nx-lut-lite/rearm-session.sh').read_text(encoding='utf-8')
reset_function=function(reset_source,'reset_round_locks').replace('/tmp/nx-lut-lite/','$NX_TEST_FIXTURE/run/')
reset_cases=[]
for case in ['fresh','engine.lock','trace.lock','memory-test.lock','demo50-recovery.lock','old-receipt','cancelled']:
 with tempfile.TemporaryDirectory(dir=OUT,prefix='reset-round-') as td:
  f=Path(td).resolve();(f/'proof').mkdir();(f/'run').mkdir()
  if case.endswith('.lock'):(f/'run'/case).mkdir()
  if case=='old-receipt':text(f/'proof/LOCKS_RESET','OLD_RECEIPT_MUST_SURVIVE\n')
  code='''#!/bin/sh
set -eu
PATH=/usr/bin:/bin:$PATH;export PATH
job=/model/JOB_test;token=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa;proof=$NX_TEST_FIXTURE/proof;cancelled=0
[ "$NX_TEST_CASE" != cancelled ] || cancelled=1
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
preflight_workers_idle(){ return 0; }
reject(){ printf 'HELD %s\n' "$1";exit 1; }
sync(){ :; }
'''+reset_function+'reset_round_locks\n'
  text(f/'test.sh',code);result=shell_run(f/'test.sh',dict(NX_TEST_FIXTURE=f.as_posix(),NX_TEST_CASE=case))
  assert (result.returncode==0)==(case=='fresh'),(case,result.stdout,result.stderr)
  if case=='fresh':
   assert 'next_round=fresh_session_token' in (f/'proof/LOCKS_RESET').read_text()
   assert not list((f/'run').iterdir())
  elif case=='old-receipt':assert (f/'proof/LOCKS_RESET').read_text()=='OLD_RECEIPT_MUST_SURVIVE\n'
  else:assert not (f/'proof/LOCKS_RESET').exists()
  reset_cases.append(dict(case=case,exit=result.returncode,passed=True))
print('PASS',len(reset_cases),'actual RESET round-initialization shell cases',flush=True)
result=dict(format='NX51_R7F_INSTALL_RESET_VALIDATION1',passed=True,hardware_tested=False,X11_tested=False,
 actual_ARM_entrypoint_cases=records,complete_installer_cases=install_records,fresh_round_lifecycle_passed=True,reset_round_cases=reset_cases,
 components=dict(session=dict(sha256=sha(binary),bytes=binary.stat().st_size)),
 model_scope='Actual shipped ARM journal verifier and actual complete deployment shell; bounded Linux filesystem/lock syscall model and host OS/file-utility endpoints; no camera hardware executed.',
 source_sha256={p.relative_to(R).as_posix():sha(p) for prefix in ['session/src','base/nx-lut-lite','installer'] for p in sorted((R/prefix).rglob('*')) if p.is_file() and not any(x in ['bin','build','results','validation','__pycache__'] for x in p.relative_to(R).parts)})
(OUT/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print('PASS R7f aggregate',flush=True)
