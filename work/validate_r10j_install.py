from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,tempfile
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';P=W/'r10j-frame-flow-full-v1';V=R/'validation/current/install-fixtures-v2';M='nx-lut-demo/nx-lut-lite/';SH='C:/Program Files/Git/usr/bin/sh.exe'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def posix(p):s=Path(p).resolve().as_posix();return '/'+s[0].lower()+s[2:]
def put(p,s):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf8',newline='\n');p.chmod(0o755)
def snapshot(p):return {q.relative_to(p).as_posix():sha(q) for q in p.rglob('*') if q.is_file()}
records=[];env={**os.environ,'PATH':'/usr/bin:/bin'}
# Audit the exact package bytes before any endpoint modeling or fixture edits.
from audit_camera_manifest import audit_package
V.mkdir(parents=True,exist_ok=True)
raw_audit=audit_package(P,include_sums=False)
raw=(P/'nx-lut-demo/module.manifest').read_bytes()
assert len(raw.splitlines())==64 and b'\r' not in raw
actual=(P/'nx-lut-demo/deploy.sh').read_text('utf8')
start=actual.index("while IFS=' ' read -r want file extra;do")
end=actual.index('done < "$PKG/module.manifest"',start)
loop=actual[start:end]+'done\nprintf "MODULE_PAYLOAD_VERIFIED_RAW_INPUT\\n"\n'
script=V/'exact-r9s-payload-loop.sh'
put(script,'set -eu\nSRC="'+posix(P/'nx-lut-demo/nx-lut-lite')+'"\ndie(){ printf "INSTALL_ERROR: %s\\n" "$1";exit 1; }\nhash(){ /usr/bin/sha256sum "$1" | { read digest rest;printf "%s\\n" "$digest"; }; }\n'+loop)
r=subprocess.run([SH,script.resolve().as_posix()],input=raw,capture_output=True,env=env,timeout=60)
assert r.returncode==0 and b'MODULE_PAYLOAD_VERIFIED_RAW_INPUT' in r.stdout,(r.returncode,r.stdout,r.stderr)
records.append(dict(name='exact_r9u_package_raw_manifest',exit=0,manifest_sha256=hashlib.sha256(raw).hexdigest(),raw_binary_pipe=True,all_module_hashes_verified=True))
print('PASS exact R10j package raw manifest (64 files)',flush=True)
own=(P/'nx-lut-demo/deploy.sh').read_text(encoding='utf8');own=own[own.index('verify_owned_manifest()'):own.index('demo_module(){')]
old=(W/'r7o-full-v1/nx-lut-demo/deploy.sh').read_text(encoding='utf8');old=old[old.index('owned_module(){'):old.index('demo_module(){')]
def driver(f,code,root,expected,label):
 s=f/'test-owner.sh';put(s,'set -eu\nOPS="'+posix(f/'ops')+'"\nPKG="'+posix(P/'nx-lut-demo')+'"\nSRC="$PKG/nx-lut-lite"\nOWNERSHIP_INDEX="$PKG/ownership/index.sha256"\nOWNERSHIP_INDEX_SHA='+sha(P/'nx-lut-demo/ownership/index.sha256')+'\nhash(){ "$OPS" sha256 "$1"; }\n'+code+'\nowned_module "'+posix(root)+'"\n')
 before=snapshot(root);r=subprocess.run([SH,s.as_posix()],env=env,capture_output=True,text=True,timeout=50);assert r.returncode==expected,(label,r.returncode,r.stdout,r.stderr);assert snapshot(root)==before;records.append(dict(name=label,exit=r.returncode,readonly_candidate=True));print('PASS installer',label,flush=True)
ops='''#!/bin/sh
case "$1" in
sha256) /usr/bin/sha256sum "$2" | { read digest ignored;printf '%s\\n' "$digest"; };;
same) /usr/bin/cmp -s "$2" "$3";;
*) exit 9;;
esac
'''

def end_to_end(mode,OLD):
 baseline=OLD.name if OLD else 'stock-firmware'
 f=V/('install-'+baseline+'-'+mode)
 if f.exists():assert f.resolve().is_relative_to(V.resolve());shutil.rmtree(f)
 sd=f/'mnt/mmc';pkg=sd/'nx-lut-demo';src=pkg/'nx-lut-lite';mod=f/'opt/usr/nx-lut-lite';run=f/'tmp/nx-lut-lite';mock=f/'mock'
 for p in [run,mock,f/'usr/sbin',f/'opt/usr',f/'lib',f/'etc/systemd/system',f/'proc']:
  p.mkdir(parents=True,exist_ok=True)
 shutil.copytree(P/M,src);shutil.copytree(P/'nx-lut-demo/ownership',pkg/'ownership');(pkg/'bin').mkdir();shutil.copy2(P/'nx-lut-demo/bin/nxl-session.arm',pkg/'bin/nxl-session.arm')
 grant='NXP33INSTALL1\nSTARTED=0.3.6-demo50\npid=0\nreport=KEEP_UNVERIFIED_RESOURCE_EVIDENCE\n' if mode in ['held_pair','held_pair_init'] else 'NXP33INSTALL1\nREADY=0.3.6-demo50\n'
 if OLD:
  shutil.copytree(OLD/M,mod);(mod/'state').mkdir();shutil.copy2(OLD/'nx-lut-demo/module.manifest',mod/'state/module.manifest');put(mod/'state/demo49-installed','NXDEMO49_INSTALLED_V1\n');put(mod/'state/demo51-installed','NXDEMO51_INSTALLED_V1\nversion=0.4.0-demo51-422\ninstall_revision=recovery-r9t-usb-key-guard\n');put(mod/'state/user-choice','KEEP_USER_CHOICE\n');put(mod/'state/p49-experiment.state',grant);put(mod/'state/usb-config/last-good.bin','KEEP_VALID_USB_BASELINE_ACROSS_UPGRADE\n')
  meta=f/'opt/usr/nx-lut-demo49-backup';put(meta/'identity','NXDEMO49_BACKUP_V1\n');put(meta/'had-hook','stock\n')
 put(f/'etc/version.info','1.12\nNX500\n');put(f/'lib/ld-linux.so.3','MODELED_ARM_LOADER\n')
 bt=f/'usr/sbin/bluetoothd';stock=f/'usr/sbin/bluetoothd.nxl-original' if OLD else bt;stock.write_bytes(b'\x7fELF'+b'ORIGINAL_DAEMON_MODEL');stock.chmod(0o755)
 if OLD:shutil.copy2(W/'r7n-full-v1'/M/'bluetoothd-wrapper.sh',bt);bt.chmod(0o755)
 put(src/'process.sh',('nxl_process_running(){ return 0; }\n' if mode in ['active_worker','active_worker_init'] else 'nxl_process_running(){ return 1; }\n')+'nxl_require_no_keyscan(){ return 0; }\nnxl_stop_listener(){ :; }\n')
 if mode=='active_probe':put(src/'process.sh','nxl_process_running(){ for name in \"$@\";do [ \"$name\" != nxl-video-probe ] || return 0;done;return 1; }\n'+'nxl_require_no_keyscan(){ return 0; }\nnxl_stop_listener(){ :; }\n')
 if mode=='active_usbguard':put(src/'process.sh','nxl_process_running(){ for name in \"$@\";do [ \"$name\" != nxl-usb-guard.a ] || return 0;done;return 1; }\n'+'nxl_require_no_keyscan(){ return 0; }\nnxl_stop_listener(){ :; }\n')
 if mode=='active_fullwidth':put(src/'process.sh','nxl_process_running(){ for name in "$@";do [ "$name" != nxl-video-fullw ] || return 0;done;return 1; }\nnxl_require_no_keyscan(){ return 0; }\nnxl_stop_listener(){ :; }\n')
 put(src/'progress.sh' ,'nxl_progress(){ printf "PROGRESS phase=%s state=%s detail=%s\\n" "$1" "$2" "$3"; }\n')
 put(pkg/'bin/nxl-holdcheck.arm','#!/bin/sh\ncase "$1" in --inventory|--check-nx51-pair) exit 0;;*) exit 20;;esac\n')
 put(pkg/'assets/SAM_0183.SRW','MODELED_RAW_SOURCE\n')
 put(pkg/'patch-fileops',ops.replace('*) exit 9;;','''version) [ "$(head -n 1 "$2")" = 1.12 ];;
volatile-fs) exit 0;;
mount-info) printf 'MODELROOT rw\\n';;
kind) printf 'elf\\n';;
unlock) /usr/bin/rmdir "$2";;
*) exit 9;;''').replace('MODELROOT',posix(f)))
 put(mock/'prefman','#!/bin/sh\nexit 0\n');put(mock/'mount','#!/bin/sh\nexit 0\n')
 put(mock/'mkdir','#!/bin/sh\nif [ "$1" = -p ];then shift;for path in "$@";do [ -d "$path" ] || /usr/bin/mkdir "$path" || exit $?;done;else exec /usr/bin/mkdir "$@";fi\n')
 put(mock/'rm','#!/bin/sh\nfor path in "$@";do case "$path" in -*) :;;"'+posix(f)+'"/*) :;;*) echo OUTSIDE_FIXTURE >&2;exit 97;;esac;done\nexec /usr/bin/rm "$@"\n')
 put(mock/'cp','''#!/bin/sh
last=
for value in "$@";do last=$value;done
if [ "${MODEL_MODE:-}" = stage_copy_failure_init ];then case "$last" in */.nx-lut-demo-stage.*) exit 31;;esac;fi
exec /usr/bin/cp "$@"
''')
 put(mock/'mv','''#!/bin/sh
case "${MODEL_MODE:-}" in
hook_switch_failure_init) case "$1" in *.demo49-new.*) exit 33;;esac;;
end_readback_failure_init) case "$1" in */.nx-lut-demo-stage.*) /usr/bin/mv "$@" || exit $?;printf x >> "$2/bin/nxl-demo-front.arm";exit 0;;esac;;
esac
exec /usr/bin/mv "$@"
''')
 store=sd/'NXDEMO51';put(store/'CURRENT.journal','KEEP_JOURNAL\n');put(store/'JOB_prior/STATE.journal','KEEP_TASK_EVIDENCE\n');(store/'logs').mkdir()
 if mode in ['held_pair','held_pair_init']:
  for name in ['engine.lock','trace.lock']:
   put(run/name/'NX51_OWNER','NX51LOCK1\njob=/mnt/mmc/NXDEMO51/JOB_prior\nboot=OBSERVED_ONLY\n');put(run/name/'RESTART_REQUIRED','NX51HOLD1\njob=/mnt/mmc/NXDEMO51/JOB_prior\nboot=OBSERVED_ONLY\nreason=PIPELINE_NATIVE_STATE_UNVERIFIED\n');put(run/name/'NX51_ROUND','NX51ROUND1\ntoken=0123456789abcdef0123456789abcdef\npid=470\n')
 if mode.endswith('_init'):
  put(run/'auto-init-held','NXEPOWNED1\nreport=/mnt/mmc/NXDEMO51/INIT_AUTO_FIELD\njob=/mnt/mmc/NXDEMO51/JOB_prior\ntoken=0123456789abcdef0123456789abcdef\ncamera_pid=247\ncamera_ticks=1224\nudd_base=1050796032\nbaseline=0\nclose_attempted=0\nend=NXEPOWNEDEND1\n')
 if mode=='init_hold_directory':(run/'auto-init-held').mkdir()
 if mode=='init_hold_changed':
  put(run/'auto-init-held','ORIGINAL_UNRESOLVED_REFERENCE\n')
  with (src/'process.sh').open('a',encoding='utf8',newline='\n') as stream:
   stream.write('nxl_stop_listener(){ printf "CONCURRENT_CHANGE\\n" >> "'+posix(run/'auto-init-held')+'"; }\n')
 hold_before=snapshot(run);module_before=snapshot(mod);bt_before=bt.read_bytes();journal_before={p:p.read_bytes() for p in [store/'CURRENT.journal',store/'JOB_prior/STATE.journal']}
 # Model only native/kernel/OS endpoints. The actual installer transaction,
 # ownership predicates, copy/readback, manifest checks and rollback execute.
 manifest=''.join(sha(p)+' '+p.relative_to(src).as_posix()+'\n' for p in sorted(src.rglob('*')) if p.is_file());put(pkg/'module.manifest',manifest)
 s=(P/'nx-lut-demo/deploy.sh').read_text(encoding='utf8').replace('task_root=\n','task_root='+posix(f)+'\n',1).replace('PATH=/usr/local/sbin:', 'PATH='+posix(mock)+':/usr/local/sbin:',1)
 s=s.replace('[ -x "','[ -f "') # NTFS has no ARM ELF executable-bit semantics.
 for key,path in [('MANIFEST_SHA',pkg/'module.manifest'),('OPS_SHA',pkg/'patch-fileops'),('HOLDCHECK_SHA',pkg/'bin/nxl-holdcheck.arm')]:s=re.sub('^'+key+'=[0-9a-f]{64}$',key+'='+sha(path),s,flags=re.M)
 s=s.replace('dfd6608e4ee05f0d5b4b402fedd3c928aee509595e6964efa4c1ce87f1d9715c',sha(pkg/'assets/SAM_0183.SRW'))
 put(pkg/'deploy.sh',s);assert f.resolve().is_relative_to(V.resolve())
 r=subprocess.run([SH,(pkg/'deploy.sh').as_posix(),'install'],env={**env,'MODEL_MODE':mode},capture_output=True,text=True,timeout=100)
 assert (store/'logs/install.log').exists(),(mode,r.returncode,r.stdout,r.stderr)
 log=(store/'logs/install.log').read_text(encoding='utf8');success=mode in ['success','held_pair','success_init','held_pair_init']
 expected=0 if success else 31 if mode in ['stage_copy_failure','stage_copy_failure_init'] else 33 if mode in ['hook_switch_failure','hook_switch_failure_init'] else 1
 assert r.returncode==expected,(mode,r.returncode,r.stdout,r.stderr,log[-3000:])
 for p,b in journal_before.items():assert p.read_bytes()==b
 assert {n:h for n,h in snapshot(run).items() if n!='demo49-install-session'}==({**hold_before,'auto-init-held':hashlib.sha256(b'ORIGINAL_UNRESOLVED_REFERENCE\nCONCURRENT_CHANGE\n').hexdigest()} if mode=='init_hold_changed' else hold_before)
 if mode=='init_hold_directory':assert (run/'auto-init-held').is_dir()
 if mode.endswith('_init') and mode!='active_worker_init':assert 'INIT_HOLD_PRESERVED' in log
 if success and mode.endswith('_init'):assert 'INIT_RUNTIME_STAYS_BLOCKED' in log
 assert not (f/'tmp/nx-lut-demo-setup.lock').exists()
 if OLD:assert (mod/'state/user-choice').read_text()=='KEEP_USER_CHOICE\n' and (mod/'state/p49-experiment.state').read_text()==grant
 if success:
  if OLD:assert (mod/'state/usb-config/last-good.bin').read_text()=='KEEP_VALID_USB_BASELINE_ACROSS_UPGRADE\n'
  assert 'MODULE_OWNER_VERIFIED method=INSTALLED_MANIFEST' in log
  assert 'INSTALL_RUNTIME_COHERENT' in log and 'INSTALL_COMPLETE action=install' in log and 'install_revision=recovery-r10j-frame-flow' in log
  assert (mod/'MODULE.id').read_bytes()==(src/'MODULE.id').read_bytes() and (mod/'state/module.manifest').read_text()==manifest
  for line in manifest.splitlines():h,n=line.split(' ',1);assert sha(mod/n)==h
 else:assert snapshot(mod)==module_before and bt.read_bytes()==bt_before and 'INSTALL_FAILED' in log
 if not OLD and success:
  assert (f/'usr/sbin/bluetoothd.nxl-original').read_bytes()==bt_before
  assert (f/'opt/usr/nx-lut-demo49-backup/had-module').read_text().strip()=='0'
 records.append(dict(name='complete_installer_'+baseline+'_'+mode,exit=r.returncode,all_old_state_and_journals_preserved=True,device_endpoints_modeled=True));print('PASS installer full transaction',baseline,mode,flush=True)
for baseline in ['r10i-hardware-qos-full-v1']:
 end_to_end('success',W/baseline)
end_to_end('active_fullwidth',W/'r10i-hardware-qos-full-v1')
end_to_end('active_usbguard',W/'r10i-hardware-qos-full-v1')
end_to_end('active_probe',W/'r10i-hardware-qos-full-v1')
end_to_end('success',None)
(R/'validation/current/prior-installed-upgrade.json').write_text(json.dumps(dict(passed=True,cases=records,hardware_tested=False,scope='Exact R10j raw byte audit before fixture changes; actual staged installer fresh stock install and predecessor R10i upgrade; active fullwidth guard; OS/native endpoints modeled, no camera writes.',raw_package_audit=raw_audit),indent=2)+'\n');print('PASS R10j installer receipt validation',len(records))
