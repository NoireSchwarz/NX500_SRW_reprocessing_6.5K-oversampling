# Execute production check_components with real ARM --version output.
from pathlib import Path
import hashlib,json,os,re,shutil,struct,subprocess,sys,time
W=Path(sys.argv[1]).resolve()if len(sys.argv)>1 else Path(__file__).resolve().parent;R=W.parent/'repository/nx500';OLD=W.parent/'r11k-srw-transfer-work/stage';S=W/'stage';M='nx-lut-demo/nx-lut-lite';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sys.path[:0]=[str(R/'work/tooling/python'),str(R/'work/r10k-ep-flow/tools')];from arm_model import ArmModel
versions={};run_root=W/'version-runs'/('run-'+str(time.time_ns()));captures=run_root/'actual-ARM-version-output';captures.mkdir(parents=True,exist_ok=False)
for label,path in [('broken-normal',OLD/M/'bin/nxl-demo-front.arm'),('broken-auto',OLD/M/'bin/nxl-init-pipeline.arm'),('fixed-normal',S/M/'bin/nxl-demo-front.arm'),('fixed-auto',S/M/'bin/nxl-init-pipeline.arm'),('post',S/M/'bin/nxl-demo-post.arm')]:
 model=ArmModel(path,['--version']);model.uc.mem_write(0xffff0fc0,struct.pack('<8I',0xe5923000,0xe1530000,0x1a000002,0xe5821000,0xe3a00000,0xe12fff1e,0xe3a00001,0xe12fff1e));a=model.run();assert a['exit']==0;a.update(binary=str(path),binary_sha256=sha(path));versions[label]=a;(captures/(label+'.txt')).write_bytes(a['stdout'].encode('utf8'))
for label in ['fixed-normal','fixed-auto']:assert versions[label]['stdout'].startswith('NXL_DEMO_FRONT=0.3.6-demo50;')and 'NXL_SRW_PATCH=R11k3c;'in versions[label]['stdout']
for label in ['broken-normal','broken-auto']:assert versions[label]['stdout'].startswith('NXL_SRW_TRANSFER=')
source=(S/M/'memory-test.sh').read_text('utf8');body=re.search(r'^check_components\(\)\{.*?^\}',source,re.M|re.S).group();real_body=body
for component in ['FRONT','POST']:
 old='version=$("$'+component+'" --version)';new='version=$('+component.lower()+'_version_endpoint "$'+component+'" --version)';assert body.count(old)==1;body=body.replace(old,new)
# Only executable boundaries and hash utility are adapted to the host; admission logic is unchanged.
helper='\n'.join(next(line for line in source.splitlines()if line.startswith(n+'(){'))for n in ['regular','is_hash'])
SHELL='C:/Program Files/Git/usr/bin/sh.exe'
def posix(p):
 s=p.resolve().as_posix();return '/'+s[0].lower()+s[2:]
rows=[]
cases=[('broken-normal','broken-normal','FRONT_VERSION_MISMATCH'),('broken-auto','broken-auto','FRONT_VERSION_MISMATCH'),('fixed-normal','fixed-normal',None),('fixed-auto','fixed-auto',None),('foreign-version','fixed-normal','FRONT_VERSION_MISMATCH'),('hash-mismatch','fixed-normal','FRONT_IDENTITY_FAILED'),('post-version-mismatch','fixed-normal','POST_VERSION_MISMATCH'),('version-exec-failure','fixed-normal','FRONT_VERSION_FAILED')]
for case,label,wanted in cases:
 f=run_root/'entry-fixtures'/case;f.mkdir(parents=True,exist_ok=False);front=Path(versions[label]['binary']);assign=['prepare_full=0','menu_ok=0','timing_clock_ready=0']
 for key,path in [('MENU',S/M/'bin/nxl-menu'),('FRONT',front),('POST',S/M/'bin/nxl-demo-post.arm'),('HWCTL',S/M/'bin/nxl-hw-controller.arm'),('HWWORK',S/M/'bin/nxl-hw-worker.arm'),('ENGINE',S/M/'lut.sh'),('RESTORE',S/M/'bin/nxl-oem-restore.arm')]:
  p=f/(key+'.exe');shutil.copyfile(path,p);assign.extend([key+"='"+posix(p)+"'",key+'_SHA='+sha(path)])
 if case=='hash-mismatch':
  p=f/'FRONT.exe';b=bytearray(p.read_bytes());b[-1]^=1;p.write_bytes(b)
 front_text=f/'front-version.txt';post_text=f/'post-version.txt';front_text.write_bytes(b'FOREIGN_FRONT\n'if case=='foreign-version'else versions[label]['stdout'].encode());post_text.write_bytes(b'FOREIGN_POST\n'if case=='post-version-mismatch'else versions['post']['stdout'].encode())
 assign+=['FRONT_VERSION_FILE='+"'"+posix(front_text)+"'",'POST_VERSION_FILE='+"'"+posix(post_text)+"'",'EXEC_FAIL='+('1'if case=='version-exec-failure'else'0')]
 script='''#!/bin/sh
set -u
export PATH=/usr/bin:/bin
hash(){ task_hash_line=$(sha256sum "$1") || return 1; printf %s "${task_hash_line%% *}"; }
fail(){ printf 'REJECTED %s\n' "$1"; exit 1; }
front_version_endpoint(){ [ "$1" = "$FRONT" ] && [ "$2" = --version ] || exit 89; [ "$EXEC_FAIL" = 0 ] || return 31; cat "$FRONT_VERSION_FILE"; }
post_version_endpoint(){ [ "$1" = "$POST" ] && [ "$2" = --version ] || exit 89; cat "$POST_VERSION_FILE"; }
'''+helper+'\n'+'\n'.join(assign)+'\n'+body+'''
check_components
[ "$menu_ok" = 1 ] && [ "$timing_clock_ready" = 1 ] || exit 90
printf 'VERSION_GATE_PASSED\n'
'''
 p=f/'case.sh';p.write_text(script,encoding='utf8',newline='\n');subprocess.run([SHELL,'-c','/usr/bin/chmod +x "$@"','chmod',*[posix(q)for q in f.glob('*.exe')]],check=True,capture_output=True)
 run=subprocess.run([SHELL,str(p)],env=dict(os.environ,PYTHONUTF8='1'),capture_output=True,text=True,encoding='utf8',timeout=30);assert (run.returncode==0)==(wanted is None),(case,run.returncode,run.stdout,run.stderr)
 if wanted:assert 'REJECTED '+wanted in run.stdout,(case,run.stdout)
 else:assert 'VERSION_GATE_PASSED'in run.stdout
 rows.append(dict(case=case,passed=True,expected_rejection=wanted,exit=run.returncode,stdout=run.stdout,stderr=run.stderr));print(case,'PASS',flush=True)
result=dict(passed=True,cases=rows,real_ARM_instructions_executed=True,ARM_versions=versions,production_shell_sha256=sha(S/M/'memory-test.sh'),production_check_components_sha256=hashlib.sha256(real_body.encode()).hexdigest(),raw_admission_pattern_unchanged=True,only_process_execution_and_SHA_utility_OS_boundaries_modeled=True,no_ISP_submitted=True,host_execute_permission_modeled_by_exe_suffix=True,physical_hardware_tested=False)
(W/'version-entry-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print('PASS all eight production version admission cases')
