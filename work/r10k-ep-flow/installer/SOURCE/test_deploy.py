#!/usr/bin/env python3
"""Actual deployment shell + host fileops in labeled isolated models, no camera."""
from pathlib import Path
import hashlib,json,os,shutil,stat,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def executable(p,s):p.write_text(s);p.chmod(0o755)
class Deploy(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  (ROOT/'results').mkdir(exist_ok=True)
  cls.build=tempfile.TemporaryDirectory();cls.ops=Path(cls.build.name)/'fileops';subprocess.run(['gcc','-O2',str(ROOT/'SOURCE/fileops.c'),'-o',str(cls.ops)],check=True)
 @classmethod
 def tearDownClass(cls):cls.build.cleanup()
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='demo49-deploy-model-');self.w=Path(self.tmp.name);self.sd=self.w/'mnt/mmc';self.pkg=self.sd/'nx-lut-demo';self.src=self.pkg/'nx-lut-lite';self.mod=self.w/'opt/usr/nx-lut-lite';self.bt=self.w/'usr/sbin/bluetoothd';self.back=self.w/'usr/sbin/bluetoothd.nxl-original';self.mock=self.w/'mockbin';self.run=self.w/'tmp/nx-lut-lite';self.meta=self.w/'opt/usr/nx-lut-demo49-backup';self.old=self.w/'opt/usr/nx-lut-lite.before-demo49';self.events=self.w/'actions.jsonl'
  for p in [self.src/'bin',self.src/'looks',self.mod/'state',self.bt.parent,self.mock,self.run,self.w/'proc',self.w/'etc/systemd/system',self.w/'lib',self.pkg/'assets']:p.mkdir(parents=True,exist_ok=True)
  (self.w/'etc/version.info').write_text('1.12\nNX500\n');(self.w/'lib/ld-linux.so.3').write_text('HOST_LOADER_SENTINEL');self.mode('rw')
  self.stock=b'\x7fELF\x01\x01\x01'+b'\0'*11+b'\x28\0'+b'STOCK_DAEMON_HOST_MODEL';self.bt.write_bytes(self.stock);self.bt.chmod(0o755)
  self.old_state=b'KEEP_USER_SELECTION';(self.mod/'state/selected').write_bytes(self.old_state);(self.mod/'README_zh.md').write_text('NX LUT Lite old');(self.mod/'lut.sh').write_text('# NX_LUT_DYNAMIC24_V1\n');(self.mod/'keep-old').write_text('OLD_MODULE')
  files={'README_zh.md':'NX LUT Lite demo','lut.sh':'# NX_LUT_DYNAMIC24_V1\n','install.sh':'#!/bin/sh\nexit 99\n','uninstall.sh':'#!/bin/sh\nexit 99\n','process.sh':'nxl_process_running(){ return 1; }\nnxl_require_no_keyscan(){ return 0; }\nnxl_stop_listener(){ printf STOP_OWN_LISTENER >> "'+str(self.w/'listener-actions')+'"; }\n','bluetoothd-wrapper.sh':'#!/bin/sh\n# NX_LUT_LITE_BT_HOOK_V1\n# NX_LUT_LITE_DEMO49_BT_HOOK_V1\nexec /usr/sbin/bluetoothd.nxl-original "$@"\n','start.sh':'#!/bin/sh\nexit 98\n','memory-test.sh':'#!/bin/sh\nexit 97\n'}
  for n,s in files.items():executable(self.src/n,s)
  (self.pkg/'assets/SAM_0183.SRW').write_text('HOST_SOURCE_MODEL_NOT_RAW');shutil.copy2(self.ops,self.pkg/'patch-fileops');self.pin()
  for tool in ['cp','mv','rm','chmod','mount','sync','systemctl','prefman']:
   executable(self.mock/tool,'''#!/usr/bin/env python3
import os,sys,json,subprocess
from pathlib import Path
W=Path('''+repr(str(self.w))+''');tool=Path(sys.argv[0]).name;args=sys.argv[1:]
with (W/'actions.jsonl').open('a') as f:f.write(json.dumps(dict(tool=tool,args=args))+'\\n')
if tool=='systemctl':
 if args[0]=='is-enabled':v=os.environ.get('OLD_UNIT_ENABLED','disabled');print(v);sys.exit(0 if v=='enabled' else 1)
 if args[0]=='is-active':print('inactive');sys.exit(3)
 sys.exit(0)
if tool in ('sync','prefman'):sys.exit(0)
if tool=='mount':
 ro='remount,ro' in args
 if ro and os.environ.get('FAIL_RO_ONCE') and not (W/'ro-failed').exists():(W/'ro-failed').touch();sys.exit(27)
 (W/'proc/mounts').write_text('/dev/host '+str(W)+' ext4 '+('ro' if ro else 'rw')+' 0 0\\n');sys.exit(0)
if tool=='cp' and os.environ.get('FAIL_STAGE_COPY') and '/.nx-lut-demo-stage.' in args[-1]:sys.exit(31)
if tool=='mv' and os.environ.get('FAIL_PROMOTE') and '/.nx-lut-demo-stage.' in args[0] and args[-1].endswith('/nx-lut-lite'):sys.exit(32)
if tool=='mv' and os.environ.get('FAIL_HOOK') and '.demo49-new.' in args[0]:sys.exit(33)
mode=(W/'proc/mounts').read_text().split()[3]
targets=args if tool=='rm' else args[-1:]
if mode=='ro' and any(t.startswith(str(W/'opt')) or t.startswith(str(W/'usr')) or t.startswith(str(W/'etc/systemd')) for t in targets):
 if tool!='rm' or any(Path(t).exists() for t in targets if not t.startswith('-')):sys.exit(29)
sys.exit(subprocess.run(['/bin/'+tool,*args]).returncode)
''')
  self.env=dict(os.environ,PATH=str(self.mock)+os.pathsep+os.environ['PATH']);self.keep=False
 def tearDown(self):
  result=getattr(getattr(self,'_outcome',None),'result',None)
  failed=bool(result and any(t is self for t,_ in list(result.failures)+list(result.errors)))
  if failed:
   self.tmp._finalizer.detach();(ROOT/'results'/('failed-'+self._testMethodName+'.json')).write_text(json.dumps({'fixture':str(self.w),'log':self.log(),'events':self.events.read_text() if self.events.exists() else ''},indent=2))
  else:self.tmp.cleanup()
 def mode(self,m):(self.w/'proc/mounts').write_text('/dev/host '+str(self.w)+' ext4 '+m+' 0 0\n')
 def pin(self):
  manifest=''.join(sha(p)+' '+str(p.relative_to(self.src))+'\n' for p in sorted(self.src.rglob('*')) if p.is_file());(self.pkg/'module.manifest').write_text(manifest)
  s=(ROOT/'deploy.sh.in').read_text().replace('task_root=\n','task_root='+str(self.w)+'\n').replace('PATH=/usr/local/sbin:','PATH='+str(self.mock)+':/usr/local/sbin:').replace('@DEMO49_MANIFEST_SHA256@',sha(self.pkg/'module.manifest')).replace('OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d','OPS_SHA='+sha(self.pkg/'patch-fileops')).replace('dfd6608e4ee05f0d5b4b402fedd3c928aee509595e6964efa4c1ce87f1d9715c',hashlib.sha256(b'HOST_SOURCE_MODEL_NOT_RAW').hexdigest())
  s=s.replace('RUN=$task_root/tmp/nx-lut-lite','RUN='+str(self.run)).replace('oem_proc=$task_root/proc','oem_proc='+str(getattr(self,'owner_proc',self.w/'proc'))).replace('LOCK=$task_root/tmp/nx-lut-demo-setup.lock','LOCK='+str(getattr(self,'setup_lock',self.w/'tmp/nx-lut-demo-setup.lock')))
  # Host fixture trees are not RAM filesystems. Only this disposable copy models
  # the camera's successful statfs gate; production script has no env bypass.
  s=s.replace('"$OPS" volatile-fs "$RUN" || die',': || die')
  executable(self.pkg/'deploy.sh',s)
 def call(self,action='install',env=None):return subprocess.run(['/bin/sh',str(self.pkg/'deploy.sh'),action],env=self.env|dict(env or {}),capture_output=True,text=True,timeout=15)
 def log(self):p=self.sd/'NXDEMO51/logs/install.log';return p.read_text() if p.exists() else ''
 def passed(self,p):self.assertEqual(p.returncode,0,self.log()+p.stderr);self.assertIn('INSTALL_COMPLETE',self.log())
 def original(self):self.assertEqual(self.bt.read_bytes(),self.stock);self.assertEqual((self.mod/'keep-old').read_text(),'OLD_MODULE');self.assertEqual((self.mod/'state/selected').read_bytes(),self.old_state)
 def locks_gone(self):self.assertFalse(getattr(self,'setup_lock',self.w/'tmp/nx-lut-demo-setup.lock').exists())
 def test_complete_install_preserves_state_and_original_inode(self):
  inode=self.bt.stat().st_ino;self.passed(self.call());self.assertEqual(self.back.stat().st_ino,inode);self.assertEqual(self.back.read_bytes(),self.stock);self.assertTrue((self.old/'keep-old').exists());self.assertEqual((self.mod/'state/selected').read_bytes(),self.old_state);self.assertEqual((self.mod/'state/p49-experiment.state').read_text(),'NXP33INSTALL1\nREADY=0.3.6-demo50\n');self.assertTrue((self.run/'demo49-install-session').exists());self.assertIn('DEMO49_BT_HOOK',self.bt.read_text());self.locks_gone()
 def test_fresh_stock_install_then_complete_uninstall(self):
  # The factory firmware's /mnt/mmc is a directory alias, not a plain directory.
  physical=self.w/'opt/storage/sdcard';physical.parent.mkdir(parents=True)
  self.sd.rename(physical);self.sd.symlink_to(physical,target_is_directory=True)
  shutil.copy2(ROOT.parent/'base/nx-lut-lite/progress.sh',self.src/'progress.sh');self.pin()
  self.env['NXL_PROGRESS_SD']=str(self.sd)
  shutil.rmtree(self.mod);self.passed(self.call());self.assertEqual((self.meta/'had-module').read_text(),'0\n')
  self.assertTrue(self.sd.is_symlink());self.assertIn('install_revision=recovery-r7',self.log())
  self.assertIn('phase=INSTALL_COMPLETE\nstate=DONE',(physical/'NXDEMO51/logs/status.txt').read_text())
  self.assertTrue((physical/'NXDEMO51/logs/progress.log').is_file())
  self.passed(self.call('uninstall'));self.assertFalse(self.mod.exists());self.assertEqual(self.bt.read_bytes(),self.stock);self.assertFalse(self.back.exists());self.assertTrue(list(self.w.glob('opt/usr/.nx-lut-demo-current.*')));self.locks_gone()

 def test_install_then_rollback_restores_module_and_stock_hook(self):
  self.passed(self.call());self.passed(self.call('rollback'));self.original();self.assertFalse(self.back.exists());self.locks_gone()
 def test_reinstall_reissues_single_grant_and_keeps_first_backup(self):
  self.passed(self.call());p=self.mod/'state/p49-experiment.state';p.write_text('NXP33INSTALL1\nSTARTED=0.3.6-demo50\n');self.passed(self.call());self.assertIn('READY=',p.read_text());self.assertTrue((self.old/'keep-old').exists());self.assertEqual((self.meta/'had-hook').read_text(),'stock\n');self.locks_gone()
 def test_existing_owned_hook_retained_on_rollback(self):
  self.bt.rename(self.back);old=b'#!/bin/sh\n# NXISP_FULL_HOOK_BEGIN_1_2_2\n# old trace hook\n# NXISP_FULL_HOOK_END_1_2_2\n# NX_LUT_LITE_BT_HOOK_V1\nexec /usr/sbin/bluetoothd.nxl-original "$@"\n';self.bt.write_bytes(old);self.bt.chmod(0o755)
  self.passed(self.call());self.assertNotIn('NXISP_FULL_HOOK',self.bt.read_text());self.passed(self.call('rollback'));self.assertEqual(self.bt.read_bytes(),old);self.assertEqual(self.back.read_bytes(),self.stock)
 def test_unknown_hook_refused_without_changes(self):
  self.bt.write_text('#!/bin/sh\n# OTHER_MOD\n');self.assertNotEqual(self.call().returncode,0);self.assertIn('Unknown Bluetooth hook',self.log());self.assertTrue((self.mod/'keep-old').exists());self.locks_gone()
 def test_unowned_existing_module_refused(self):
  (self.mod/'README_zh.md').write_text('OTHER_MOD');self.assertNotEqual(self.call().returncode,0);self.original();self.locks_gone()
 def published_readme(self,version='r6'):
  directory='demo-p52-timing-r6' if version=='r6' else 'demo-p52-restore-r5'
  source=ROOT.parent.parent/directory/'installer/README_zh.md'
  expected='a567c94527b82954af6c36eaf87321c2457feb9b10a49e4be36a4eb45dcad01d' if version=='r6' else 'ca29b3e48ffe458ed57c356b791989fe5f421fdeb7be4fedd9d77556b3fe4485'
  self.assertEqual(sha(source),expected,'Immutable published README changed');self.assertNotIn('NX LUT Lite',source.read_text())
  shutil.copy2(source,self.mod/'README_zh.md');(self.mod/'state/demo51-installed').write_text('NXDEMO51_INSTALLED_V1\nversion=0.4.0-demo51-422\ninstall_revision='+('timing-r6' if version=='r6' else 'recovery-r5')+'\n')
  return source
 def test_actual_published_r6_readme_upgrade_accepted(self):
  source=self.published_readme();old=source.read_bytes();self.passed(self.call());self.assertEqual((self.old/'README_zh.md').read_bytes(),old);self.assertEqual((self.mod/'state/selected').read_bytes(),self.old_state);self.locks_gone()
 def test_actual_published_r5_readme_upgrade_accepted(self):
  source=self.published_readme('r5');old=source.read_bytes();self.passed(self.call());self.assertEqual((self.old/'README_zh.md').read_bytes(),old);self.locks_gone()
 def test_published_readme_changed_single_byte_refused(self):
  self.published_readme();p=self.mod/'README_zh.md';text=p.read_bytes();p.write_bytes(b'!'+text[1:]);self.assertNotEqual(self.call().returncode,0);self.assertIn('Unknown existing module',self.log());self.original();self.locks_gone()
 def test_published_readme_without_installed_marker_refused(self):
  self.published_readme();(self.mod/'state/demo51-installed').unlink();self.assertNotEqual(self.call().returncode,0);self.original();self.locks_gone()
 def test_published_readme_wrong_marker_refused(self):
  self.published_readme();(self.mod/'state/demo51-installed').write_text('NXDEMO51_INSTALLED_V2\n');self.assertNotEqual(self.call().returncode,0);self.original();self.locks_gone()
 def test_published_readme_linked_marker_refused(self):
  self.published_readme();p=self.mod/'state/demo51-installed';saved=self.w/'marker-copy';p.rename(saved);p.symlink_to(saved);self.assertNotEqual(self.call().returncode,0);self.assertTrue(p.is_symlink());self.original();self.locks_gone()
 def test_published_readme_linked_state_refused(self):
  self.published_readme();p=self.mod/'state';saved=self.w/'state-copy';p.rename(saved);p.symlink_to(saved,target_is_directory=True);self.assertNotEqual(self.call().returncode,0);self.assertTrue(p.is_symlink());self.original();self.locks_gone()
 def test_published_readme_still_requires_own_lut_header(self):
  self.published_readme();(self.mod/'lut.sh').write_text('# FOREIGN_LUT_MODULE\n');self.assertNotEqual(self.call().returncode,0);self.assertIn('Unknown existing module',self.log());self.original();self.locks_gone()
 def test_existing_source_asset_hash_verified_without_replacement(self):
  asset=self.pkg/'assets/SAM_0183.SRW';before=asset.read_bytes();self.passed(self.call());self.assertEqual(asset.read_bytes(),before)
  asset.write_bytes(b'CHANGED_SOURCE');self.assertNotEqual(self.call().returncode,0);self.assertIn('Existing SAM_0183 source hash mismatch',self.log());self.assertEqual(asset.read_bytes(),b'CHANGED_SOURCE');self.locks_gone()
 def test_missing_source_refused(self):
  (self.pkg/'assets/SAM_0183.SRW').unlink();self.assertNotEqual(self.call().returncode,0);self.original();self.locks_gone()
 def test_bad_manifest_before_source_helper_execution(self):
  (self.pkg/'module.manifest').write_text('bad');self.assertNotEqual(self.call().returncode,0);self.assertFalse((self.w/'listener-actions').exists());self.original();self.locks_gone()
 def test_source_mismatch_refused_before_dot_process(self):
  (self.src/'process.sh').write_text('touch /SHOULD_NEVER_RUN');self.assertNotEqual(self.call().returncode,0);self.assertFalse((self.w/'listener-actions').exists());self.original();self.locks_gone()
 def test_symlink_source_refused(self):
  p=self.src/'start.sh';p.unlink();p.symlink_to(self.src/'install.sh');self.assertNotEqual(self.call().returncode,0);self.original();self.locks_gone()
 def test_preserved_state_symlink_never_truncates_sd_photo(self):
  photo=self.sd/'USER.JPG';photo.write_bytes(b'USER_PHOTO_SENTINEL');(self.mod/'state/startup.status').symlink_to(photo);self.assertNotEqual(self.call().returncode,0);self.assertEqual(photo.read_bytes(),b'USER_PHOTO_SENTINEL');self.original();self.locks_gone()
 def test_active_restore_helper_comm_refuses_install_without_changes(self):
  p=self.src/'process.sh';p.write_text(p.read_text().replace('nxl_process_running(){ return 1; }','nxl_process_running(){ for name in "$@";do [ "$name" != nxl-oem-restore ] || return 0;done;return 1; }'));self.pin();self.assertNotEqual(self.call().returncode,0);self.assertIn('Close menu and wait for workers',self.log());self.original();self.locks_gone()
 def test_active_engine_lock_preserved(self):
  (self.run/'engine.lock').mkdir();self.assertNotEqual(self.call().returncode,0);self.assertTrue((self.run/'engine.lock').is_dir());self.original();self.locks_gone()
 def make_known_front_path_holds(self):
  store=self.sd/'NXDEMO51'
  if store.exists():shutil.rmtree(store)
  job=store/'JOB_falsepath';job.mkdir(parents=True);(job/'CAP_HOLD.bin').write_bytes(b'PRESERVE_OWNED_CAP_AND_REPORTS')
  for name in ['engine.lock','trace.lock']:
   d=self.run/name
   if d.is_symlink():d.unlink()
   elif d.exists():shutil.rmtree(d)
   d.mkdir();(d/'NX51_OWNER').write_text('NX51LOCK1\njob='+str(job)+'\nboot=unchanged-kernel\n')
   (d/'RESTART_REQUIRED').write_text('NX51HOLD1\njob='+str(job)+'\nboot=unchanged-kernel\nreason=FRONT_RESTART_OR_PATH_INVALID\n')
  return job
 def hold_bytes(self):
  return {str(p.relative_to(self.w)):p.read_bytes() for name in ['engine.lock','trace.lock'] for p in (self.run/name).iterdir() if p.is_file() and not p.is_symlink()}
 def make_oem_restore_holds(self,pid='4242',forms=('pid','pid')):
  # Unique HOST scenario paths avoid cross-case directory-cache/inode reuse;
  # this is fixture isolation only, never a camera boot/run counter.
  self.oem_case_index=getattr(self,'oem_case_index',0)+1
  self.run=self.w/'tmp'/('nx-lut-oem-case-'+str(self.oem_case_index));self.run.mkdir()
  self.owner_proc=self.w/('proc-oem-case-'+str(self.oem_case_index));self.owner_proc.mkdir()
  self.setup_lock=self.w/'tmp'/('nx-lut-setup-oem-case-'+str(self.oem_case_index)+'.lock')
  # This sandbox exposes no host/proc. Model only the fixed kernel stdin fd
  # read interface; all actual byte comparison stays in the compiled C helper.
  # Owner PID liveness is always the isolated synthetic proc tree below.
  wrapper='''#!/usr/bin/env python3
# HOST_STDIN_PROC_FD_MODEL_NOT_CAMERA
import os,sys,tempfile,subprocess
from pathlib import Path
REAL='''+repr(str(self.ops))+'''
if len(sys.argv)==4 and sys.argv[1]=='same' and sys.argv[3]=='/proc/self/fd/0':
 with tempfile.NamedTemporaryFile(dir='''+repr(str(self.w))+''',prefix='HOST-stdin-',delete=False) as f:
  name=f.name;f.write(sys.stdin.buffer.read())
 try:r=subprocess.run([REAL,'same',sys.argv[2],name]).returncode
 finally:os.unlink(name)
 sys.exit(r)
os.execv(REAL,[REAL,*sys.argv[1:]])
'''
  executable(self.pkg/'patch-fileops',wrapper);self.pin()
  proc=self.owner_proc;(proc/'self').mkdir(exist_ok=True)
  (proc/'self/status').write_text('Name:\tHOST_PROCESS_VIEW\nState:\tR\n')
  for name in ['4242','4243','2','2147483647']:
   q=proc/name
   if q.is_symlink() or q.is_file():q.unlink()
   elif q.exists():shutil.rmtree(q)
  for name,form in zip(['engine.lock','trace.lock'],forms):
   q=self.run/name
   if q.is_symlink() or q.is_file():q.unlink()
   elif q.exists():shutil.rmtree(q)
   q.mkdir();(q/'NXOEM_OWNER').write_text('NXOEMLOCK1\npid='+pid+'\n')
   if form=='pid':(q/'pid').write_text(pid+'\n')
   else:(q/'RESTART_REQUIRED').write_text('NXOEMHOLD1\nreason=OEM_RESTORE_NOT_VERIFIED\n')
  return proc
 def oem_hold_snapshot(self):
  nodes={}
  for name in ['engine.lock','trace.lock']:
   root=self.run/name
   paths=[root]+list(root.rglob('*')) if root.exists() and not root.is_symlink() else [root]
   for q in paths:
    if not q.exists() and not q.is_symlink():continue
    st=q.lstat();key=str(q.relative_to(self.run));item={'mode':stat.S_IFMT(st.st_mode),'inode':st.st_ino}
    if stat.S_ISREG(st.st_mode):item['bytes']=q.read_bytes().hex()
    if stat.S_ISLNK(st.st_mode):item['link']=os.readlink(q)
    nodes[key]=item
  return nodes
 def test_stale_exact_oem_pid_pair_allows_only_controlplane_update(self):
  self.make_oem_restore_holds();job=self.sd/'NXDEMO51/JOB_existing_oem';job.mkdir(parents=True);sentinels={self.sd/'NXDEMO51/CURRENT.journal':b'KEEP_CURRENT_JOURNAL',job/'STATE.journal':b'KEEP_UNKNOWN_TASK_JOURNAL',job/'SESSION.state':b'KEEP_SESSION_BYTES',job/'CAP_HOLD.bin':b'KEEP_OWNERSHIP_EVIDENCE'}
  for q,data in sentinels.items():q.write_bytes(data)
  before=self.oem_hold_snapshot();self.passed(self.call());self.assertEqual(self.oem_hold_snapshot(),before)
  for q,data in sentinels.items():self.assertEqual(q.read_bytes(),data)
  self.assertIn('KNOWN_OEM_RESTORE_HOLDS_PRESERVED owner_pid=4242',self.log());self.assertIn('INSTALL_CONTROL_PLANE_ONLY; NO_HARDWARE_IDLE_CLAIM; NO_RECOVERY_OR_NATIVE_START; NO_HOLD_CHANGED',self.log());self.assertFalse(any(x in self.log() for x in ['CAMERA_RESTORED','HARDWARE_READY']));self.locks_gone()
 def test_stale_oem_unknown_hold_and_interrupted_pair_shapes_preserved(self):
  for forms in [('hold','hold'),('hold','pid'),('pid','hold')]:
   with self.subTest(forms=forms):
    self.make_oem_restore_holds(forms=forms);before=self.oem_hold_snapshot();self.passed(self.call());self.assertEqual(self.oem_hold_snapshot(),before);self.locks_gone()
 def test_stale_oem_pid_boundaries_use_isolated_proc_view(self):
  for pid in ['2','2147483647']:
   with self.subTest(pid=pid):
    self.make_oem_restore_holds(pid=pid);before=self.oem_hold_snapshot();self.passed(self.call());self.assertEqual(self.oem_hold_snapshot(),before);self.locks_gone()
 def test_malformed_oem_shapes_never_change_original_or_holds(self):
  def change(path,old,new):path.write_text(path.read_text().replace(old,new))
  variants=[
   ('owner_header',lambda:change(self.run/'engine.lock/NXOEM_OWNER','NXOEMLOCK1','FOREIGN')),
   ('owner_pid_key',lambda:change(self.run/'engine.lock/NXOEM_OWNER','pid=','owner=')),
   ('owner_extra',lambda:(self.run/'engine.lock/NXOEM_OWNER').write_text('NXOEMLOCK1\npid=4242\nextra\n')),
   ('owner_NUL',lambda:(self.run/'engine.lock/NXOEM_OWNER').write_bytes(b'NXOEMLOCK1\npid=42\x0042\n')),
   ('owner_unterminated',lambda:(self.run/'engine.lock/NXOEM_OWNER').write_text('NXOEMLOCK1\npid=4242')),
   ('cross_pid_mismatch',lambda:((self.run/'trace.lock/NXOEM_OWNER').write_text('NXOEMLOCK1\npid=4243\n'),(self.run/'trace.lock/pid').write_text('4243\n'))),
   ('leaf_pid_mismatch',lambda:(self.run/'engine.lock/pid').write_text('4243\n')),
   ('leaf_pid_extra',lambda:(self.run/'engine.lock/pid').write_text('4242\nextra\n')),
   ('leaf_pid_NUL',lambda:(self.run/'engine.lock/pid').write_bytes(b'42\x0042\n')),
   ('hold_dangling_symlink',lambda:((self.run/'engine.lock/pid').unlink(),(self.run/'engine.lock/RESTART_REQUIRED').symlink_to(self.w/'absent'))),
   ('extra_FIFO',lambda:os.mkfifo(self.run/'trace.lock/EXTRA')),
   ('leaf_pid_unterminated',lambda:(self.run/'engine.lock/pid').write_text('4242')),
   ('pid_and_hold',lambda:(self.run/'engine.lock/RESTART_REQUIRED').write_text('NXOEMHOLD1\nreason=OEM_RESTORE_NOT_VERIFIED\n')),
   ('missing_pid_and_hold',lambda:(self.run/'trace.lock/pid').unlink()),
   ('foreign_file',lambda:(self.run/'engine.lock/FOREIGN').write_text('keep')),
   ('hidden_file',lambda:(self.run/'trace.lock/.FOREIGN').write_text('keep')),
   ('dangling_extra',lambda:(self.run/'engine.lock/EXTRA').symlink_to(self.w/'absent')),
   ('owner_symlink',lambda:((self.run/'engine.lock/NXOEM_OWNER').unlink(),(self.run/'engine.lock/NXOEM_OWNER').symlink_to(self.run/'trace.lock/NXOEM_OWNER'))),
   ('pid_symlink',lambda:((self.run/'engine.lock/pid').unlink(),(self.run/'engine.lock/pid').symlink_to(self.run/'trace.lock/pid'))),
   ('pid_dangling_symlink',lambda:((self.run/'engine.lock/pid').unlink(),(self.run/'engine.lock/pid').symlink_to(self.w/'absent'))),
   ('one_directory_missing',lambda:shutil.rmtree(self.run/'trace.lock')),
   ('directory_symlink',lambda:((self.run/'trace.lock').rename(self.run/'kept-oem'),(self.run/'trace.lock').symlink_to(self.run/'kept-oem',target_is_directory=True)))
  ]
  for label,mutate in variants:
   with self.subTest(label=label):
    kept=self.run/'kept-oem'
    if kept.exists():shutil.rmtree(kept)
    self.make_oem_restore_holds();mutate();before=self.oem_hold_snapshot();self.assertNotEqual(self.call().returncode,0);self.assertEqual(self.oem_hold_snapshot(),before);self.original();self.locks_gone()
 def test_invalid_oem_owner_pid_and_hold_records_refused(self):
  for pid in ['','0','1','-2','+2','04242','2x','2147483648','9999999999','12345678901','4242 ']:
   with self.subTest(pid=pid):
    self.make_oem_restore_holds(pid=pid);before=self.oem_hold_snapshot();self.assertNotEqual(self.call().returncode,0);self.assertEqual(self.oem_hold_snapshot(),before);self.original();self.locks_gone()
  for text in ['NXOEMHOLD1\nreason=OEM_RESTORE_NOT_VERI\x00FIED\n','NXOEMHOLD1\nreason=OTHER_UNKNOWN\n','NXOEMHOLD1\nreason=OEM_RESTORE_NOT_VERIFIED','NXOEMHOLD1\nreason=OEM_RESTORE_NOT_VERIFIED\nextra\n','FOREIGN\nreason=OEM_RESTORE_NOT_VERIFIED\n']:
   with self.subTest(hold=text):
    self.make_oem_restore_holds(forms=('hold','hold'));(self.run/'engine.lock/RESTART_REQUIRED').write_text(text);before=self.oem_hold_snapshot();self.assertNotEqual(self.call().returncode,0);self.assertEqual(self.oem_hold_snapshot(),before);self.original();self.locks_gone()
 def test_live_or_reused_owner_pid_any_proc_node_refused(self):
  for kind in ['directory','regular','dangling_symlink']:
   with self.subTest(kind=kind):
    proc=self.make_oem_restore_holds();q=proc/'4242'
    if kind=='directory':q.mkdir();(q/'status').write_text('Name:\tUNRELATED_REUSED_PID\n')
    elif kind=='regular':q.write_text('PID_PATH_NOT_TRUSTWORTHY')
    else:q.symlink_to(proc/'absent')
    before=self.oem_hold_snapshot();self.assertNotEqual(self.call().returncode,0);self.assertEqual(self.oem_hold_snapshot(),before);self.original();self.locks_gone()
 def test_missing_or_bad_proc_view_is_not_dead_owner_proof(self):
  for text in [None,'NOT_A_KERNEL_PROCESS_VIEW\n']:
   with self.subTest(text=text):
    proc=self.make_oem_restore_holds()
    if text is None:(proc/'self/status').unlink()
    else:(proc/'self/status').write_text(text)
    before=self.oem_hold_snapshot();self.assertNotEqual(self.call().returncode,0);self.assertEqual(self.oem_hold_snapshot(),before);self.original();self.locks_gone()
 def test_owner_reappears_before_final_component_switch_is_refused(self):
  proc=self.make_oem_restore_holds();p=self.src/'process.sh';p.write_text(p.read_text().replace('nxl_stop_listener(){','nxl_stop_listener(){ mkdir '+str(proc/'4242')+';'));self.pin();before=self.oem_hold_snapshot();self.assertNotEqual(self.call().returncode,0);self.assertEqual(self.oem_hold_snapshot(),before);self.original();self.assertTrue((proc/'4242').is_dir());self.locks_gone()
 def test_oem_exception_never_applies_to_uninstall_or_rollback_or_mixed_family(self):
  for action in ['uninstall','rollback']:
   with self.subTest(action=action):
    self.make_oem_restore_holds();before=self.oem_hold_snapshot();self.assertNotEqual(self.call(action).returncode,0);self.assertEqual(self.oem_hold_snapshot(),before);self.original();self.locks_gone()
  self.make_known_front_path_holds();d=self.run/'engine.lock';shutil.rmtree(d);d.mkdir();(d/'NXOEM_OWNER').write_text('NXOEMLOCK1\npid=4242\n');(d/'pid').write_text('4242\n');proc=self.owner_proc;(proc/'self').mkdir(exist_ok=True);(proc/'self/status').write_text('Name:\tHOST_PROCESS_VIEW\n');before=self.oem_hold_snapshot();self.assertNotEqual(self.call().returncode,0);self.assertEqual(self.oem_hold_snapshot(),before);self.original();self.locks_gone()
 def test_exact_false_path_holds_allow_controlplane_update_without_releasing_them(self):
  job=self.make_known_front_path_holds();before=self.hold_bytes();cap=(job/'CAP_HOLD.bin').read_bytes()
  self.passed(self.call());self.assertEqual(self.hold_bytes(),before);self.assertEqual((job/'CAP_HOLD.bin').read_bytes(),cap)
  self.assertIn('KNOWN_FRONT_PATH_HOLD_PRESERVED',self.log());self.assertIn('NO_RECOVERY_OR_NATIVE_START',self.log())
  self.assertTrue((self.mod/'state/p49-experiment.state').read_text().endswith('READY=0.3.6-demo50\n'))
  self.assertFalse((job/'P52_POST_COMPLETE').exists());self.locks_gone()
 def test_false_path_hold_mismatches_and_foreign_holds_still_refused(self):
  original_process=(self.src/'process.sh').read_text()
  def change(path,old,new):path.write_text(path.read_text().replace(old,new))
  variants=[
   ('wrong_reason',lambda j:change(self.run/'engine.lock/RESTART_REQUIRED','FRONT_RESTART_OR_PATH_INVALID','NATIVE_CLEANUP_UNKNOWN')),
   ('wrong_owner_header',lambda j:change(self.run/'engine.lock/NX51_OWNER','NX51LOCK1','FOREIGN')),
   ('wrong_hold_header',lambda j:change(self.run/'engine.lock/RESTART_REQUIRED','NX51HOLD1','FOREIGN')),
   ('hold_owner_job_mismatch',lambda j:change(self.run/'engine.lock/RESTART_REQUIRED',str(j),str(j)+'_other')),
   ('hold_owner_boot_mismatch',lambda j:change(self.run/'engine.lock/RESTART_REQUIRED','unchanged-kernel','other-kernel')),
   ('cross_lock_job_mismatch',lambda j:(j.with_name(j.name+'_other').mkdir(),[change(self.run/('trace.lock/'+n),str(j),str(j)+'_other') for n in ['NX51_OWNER','RESTART_REQUIRED']])),
   ('extra_file',lambda j:(self.run/'engine.lock/FOREIGN').write_text('foreign data')),
   ('hidden_extra_file',lambda j:(self.run/'trace.lock/.FOREIGN').touch()),
   ('pid_even_if_stale',lambda j:(self.run/'engine.lock/pid').write_text('999999\n')),
   ('pid_broken_symlink',lambda j:(self.run/'engine.lock/pid').symlink_to(self.w/'absent')),
   ('owner_symlink',lambda j:((self.run/'engine.lock/NX51_OWNER').unlink(),(self.run/'engine.lock/NX51_OWNER').symlink_to(self.run/'trace.lock/NX51_OWNER'))),
   ('owner_extra_line',lambda j:(self.run/'engine.lock/NX51_OWNER').write_text((self.run/'engine.lock/NX51_OWNER').read_text()+'extra\n')),
   ('hold_no_final_newline',lambda j:(self.run/'trace.lock/RESTART_REQUIRED').write_text((self.run/'trace.lock/RESTART_REQUIRED').read_text().rstrip('\n'))),
   ('job_directory_missing',lambda j:shutil.rmtree(j)),
   ('other_DMA_hold',lambda j:(self.run/'memory-test.lock').mkdir()),
   ('active_native_worker',lambda j:(self.src/'process.sh').write_text(original_process.replace('nxl_process_running(){ return 1; }','nxl_process_running(){ return 0; }')))
  ]
  for label,mutate in variants:
   with self.subTest(label=label):
    other=self.run/'memory-test.lock'
    if other.exists():other.rmdir()
    (self.src/'process.sh').write_text(original_process);job=self.make_known_front_path_holds();mutate(job);self.pin()
    before=self.hold_bytes();p=self.call();self.assertNotEqual(p.returncode,0,label);self.assertEqual(self.hold_bytes(),before,label);self.original();self.locks_gone()
  (self.src/'process.sh').write_text(original_process);self.pin()
  for action in ['uninstall','rollback']:
   with self.subTest(action=action):
    other=self.run/'memory-test.lock'
    if other.exists():other.rmdir()
    self.make_known_front_path_holds();before=self.hold_bytes();self.assertNotEqual(self.call(action).returncode,0);self.assertEqual(self.hold_bytes(),before);self.original();self.locks_gone()
 def test_stale_regular_ui_flock_files_do_not_block_install(self):
  for name in ['menu.lock','memory-ui.lock','trace-ui.lock']:(self.run/name).write_bytes(b'')
  self.passed(self.call());self.assertTrue((self.mod/'state/demo51-installed').is_file());self.assertIn('version=0.4.0-demo51-422',self.log())
 def test_unknown_old_isp_trace_lock_not_cleared(self):
  p=self.w/'tmp/nx-isp-full-capture.lock';p.mkdir();(p/'owner').write_text('OTHER')
  self.assertNotEqual(self.call().returncode,0);self.assertEqual((p/'owner').read_text(),'OTHER');self.original();self.locks_gone()
 def test_old_isp_arm_not_cleared(self):
  p=self.sd/'NXISP_FULL_ARM';p.write_text('ARMED')
  self.assertNotEqual(self.call().returncode,0);self.assertEqual(p.read_text(),'ARMED');self.original();self.locks_gone()
 def test_video_mode_refused(self):
  (self.run/'session').mkdir();(self.run/'session/video_enabled').touch();self.assertNotEqual(self.call().returncode,0);self.original();self.locks_gone()
 def test_module_path_symlink_refused(self):
  kept=self.mod.with_name('kept');self.mod.rename(kept);self.mod.symlink_to(kept,target_is_directory=True);self.assertNotEqual(self.call().returncode,0);self.assertTrue(self.mod.is_symlink());self.locks_gone()
 def test_stage_copy_failure_keeps_current_module(self):
  self.assertNotEqual(self.call(env={'FAIL_STAGE_COPY':'1'}).returncode,0);self.original();self.locks_gone()
 def test_promotion_failure_restores_current_module(self):
  self.assertNotEqual(self.call(env={'FAIL_PROMOTE':'1'}).returncode,0);self.original();self.assertFalse(self.old.exists());self.locks_gone()
 def test_hook_failure_restores_complete_prior_group(self):
  self.assertNotEqual(self.call(env={'FAIL_HOOK':'1'}).returncode,0);self.original();self.assertFalse(self.old.exists());self.assertFalse(self.back.exists());self.locks_gone()
 def test_original_ro_mount_restored(self):
  self.mode('ro');self.passed(self.call());self.assertIn(' ro ',(self.w/'proc/mounts').read_text());self.locks_gone()
 def test_failed_first_ro_restore_rolls_back_group(self):
  self.mode('ro');self.assertNotEqual(self.call(env={'FAIL_RO_ONCE':'1'}).returncode,0);self.original();self.assertIn(' ro ',(self.w/'proc/mounts').read_text());self.assertIn('restored=1',self.log());self.locks_gone()
 def test_uninstall_refuses_unknown_hook(self):
  self.passed(self.call());self.bt.write_text('#!/bin/sh\nOTHER\n');self.assertNotEqual(self.call('uninstall').returncode,0);self.assertTrue(self.mod.exists());self.assertEqual(self.back.read_bytes(),self.stock)
 def test_old_disabled_service_enablement_preserved_on_rollback(self):
  unit=self.w/'etc/systemd/system/nx-lut-lite.service';unit.write_text('[Unit]\nDescription=NX LUT Lite old\n');self.passed(self.call());self.assertFalse(unit.exists());self.passed(self.call('rollback'));self.assertTrue(unit.exists());events=[json.loads(x) for x in self.events.read_text().splitlines()];self.assertFalse(any(e['tool']=='systemctl' and e['args'][0]=='enable' for e in events));self.assertEqual((self.meta/'unit-enabled.previous').read_text(),'disabled\n')
 def test_old_enabled_service_enablement_restored_on_failure(self):
  unit=self.w/'etc/systemd/system/nx-lut-lite.service';unit.write_text('[Unit]\nDescription=NX LUT Lite old\n');self.assertNotEqual(self.call(env={'FAIL_HOOK':'1','OLD_UNIT_ENABLED':'enabled'}).returncode,0);self.original();self.assertTrue(unit.exists());events=[json.loads(x) for x in self.events.read_text().splitlines()];self.assertTrue(any(e['tool']=='systemctl' and e['args'][0]=='enable' for e in events));self.locks_gone()
 def test_unknown_service_refused(self):
  unit=self.w/'etc/systemd/system/nx-lut-lite.service';unit.write_text('Description=OTHER MOD\n');self.assertNotEqual(self.call().returncode,0);self.original();self.assertEqual(unit.read_text(),'Description=OTHER MOD\n');self.locks_gone()
 def test_existing_btnew_recovery_not_removed_on_guard_refusal(self):
  wrapper=self.w/'samepid.sh';executable(wrapper,'#!/bin/sh\np='+str(self.bt)+'.demo49-new.$$\nprintf RETAINED > "$p"\nexec /bin/sh '+str(self.pkg/'deploy.sh')+' install\n');p=subprocess.run(['/bin/sh',str(wrapper)],env=self.env,capture_output=True,text=True,timeout=15);self.assertNotEqual(p.returncode,0);files=list(self.bt.parent.glob('bluetoothd.demo49-new.*'));self.assertEqual(len(files),1);self.assertEqual(files[0].read_text(),'RETAINED');self.original();self.locks_gone()
 def test_visible_reacquired_setup_lock_is_preserved_and_not_complete(self):
  ops=self.pkg/'patch-fileops';real=self.pkg/'fileops.real';ops.rename(real);executable(ops,'#!/bin/sh\nif [ "$1" = unlock ];then\n '+str(real)+' "$@" || exit $?\n mkdir "$2"\n printf FOREIGN > "$2/owner"\n exit 0\nfi\nexec '+str(real)+' "$@"\n');self.pin();p=self.call();self.assertNotEqual(p.returncode,0);self.assertNotIn('INSTALL_COMPLETE',self.log());self.assertEqual((self.w/'tmp/nx-lut-demo-setup.lock/owner').read_text(),'FOREIGN')
 def test_failed_sd_entries_do_not_execute_payload_process_helper(self):
  executable(self.pkg/'deploy.sh','#!/bin/sh\nexit 7\n');executable(self.src/'process.sh','#!/bin/sh\nprintf PAYLOAD_EXECUTED > '+str(self.w/'bad-executed')+'\nnxl_finish_factory_install(){ :; }\n')
  for folder in ['', 'UNINSTALL/', 'ROLLBACK/']:
   with self.subTest(folder=folder):
    source=(ROOT/folder/'install.sh').read_text().replace('task_root=\n','task_root='+str(self.w)+'\n');entry=self.sd/'install.sh';executable(entry,source);p=subprocess.run(['/bin/sh',str(entry)],env=self.env,capture_output=True,text=True,timeout=10);self.assertNotEqual(p.returncode,0);self.assertFalse((self.w/'bad-executed').exists())
 def test_syntax_no_missing_camera_utilities_or_autorun(self):
  s=(ROOT/'deploy.sh.in').read_text();self.assertNotIn('pidof',s);self.assertNotIn('cmp ',s);self.assertNotIn('sha256sum',s);self.assertNotIn('NXL_PATCH_TEST_ROOT',s);self.assertNotIn('nxl-demo-front.arm',s);subprocess.run(['/bin/sh','-n',str(ROOT/'deploy.sh.in')],check=True)
if __name__=='__main__':unittest.main(verbosity=2)
