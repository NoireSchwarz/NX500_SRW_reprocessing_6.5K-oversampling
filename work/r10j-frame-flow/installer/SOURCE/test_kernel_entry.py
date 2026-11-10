#!/usr/bin/env python3
"""Regression tests for install-only entry: no kernel ID or reset dependency.

All actions run beneath an isolated fixture. No camera device, real deployment,
kernel reboot, or arbitrary process is touched.
"""
from pathlib import Path
import hashlib,io,json,os,shlex,subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[2]
ENTRY=R/'installer/install.sh'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
class Fixture:
 def __init__(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='nx51-restore-r5-entry-')
  self.w=Path(self.tmp.name);self.sd=self.w/'mnt/mmc';self.pkg=self.sd/'nx-lut-demo'
  self.pkg.mkdir(parents=True);self.entry=self.sd/'install.sh';self.calls=self.w/'calls'
  self.deploy=self.pkg/'deploy.sh';self.ready();self.stub_deploy();self.mock=self.w/'mock';self.mock.mkdir();sync=self.mock/'sync';sync.write_text('#!/bin/sh\nexit 0\n');sync.chmod(0o755);self.env=dict(os.environ,PATH=str(self.mock)+':'+os.environ.get('PATH','/bin:/usr/bin'))
 def ready(self):
  t=ENTRY.read_text().replace('task_root=\n','task_root='+shlex.quote(str(self.w))+'\n',1)
  self.entry.write_text(t)
  for n in ['info.tg','nx_cs.adj']:(self.sd/n).write_text('MODEL\n')
 def stub_deploy(self,rc=0):
  self.deploy.write_text('#!/bin/sh\nprintf "%s\\n" "$1" >> '+shlex.quote(str(self.calls))+'\nexit '+str(rc)+'\n')
 def run(self):return subprocess.run(['/bin/sh',str(self.entry)],env=self.env,capture_output=True,text=True,timeout=15)
 def triggers(self):return [(self.sd/n).exists() for n in ['install.sh','info.tg','nx_cs.adj']]
 def text(self,name):return (self.sd/'NXDEMO51/logs'/name).read_text()
 def close(self):self.tmp.cleanup()
class InstallOnly(unittest.TestCase):
 def f(self):f=Fixture();self.addCleanup(f.close);return f
 def assert_installed(self,f):
  p=f.run();self.assertEqual(p.returncode,0,(p.stdout,p.stderr));self.assertEqual(f.triggers(),[False]*3)
  self.assertEqual(f.calls.read_text(),'install\n')
  self.assertIn('phase=INSTALL_COMPLETE',f.text('status.txt'))
  self.assertIn('state=SUCCESS',f.text('status.txt'))
  self.assertIn('install_revision=recovery-r7',f.text('install.log'))
 def test_known_OEM_SD_alias_installs_and_publishes_latest_status(self):
  f=self.f();physical=f.w/'opt/storage/sdcard';physical.parent.mkdir(parents=True);f.sd.rename(physical);f.sd.symlink_to(physical,target_is_directory=True)
  self.assert_installed(f);self.assertIn('phase=INSTALL_COMPLETE',(physical/'NXDEMO51/logs/status.txt').read_text())
 def test_status_replacement_failure_is_logged_and_falls_back(self):
  f=self.f();mv=f.mock/'mv';mv.write_text('#!/bin/sh\nfor last do :;done\n[ ! -e "$last" ] || exit 23\nexec /bin/mv "$@"\n');mv.chmod(0o755)
  self.assert_installed(f)
  self.assertIn('mv_exit=23',f.text('install.log'));self.assertIn('snapshot=INPLACE_VERIFIED',f.text('progress.log'))
 def test_false_successful_mv_cannot_leave_initial_entry_snapshot(self):
  f=self.f();mv=f.mock/'mv';mv.write_text('#!/bin/sh\nexit 0\n');mv.chmod(0o755)
  self.assert_installed(f);self.assertIn('mv_exit=0 readback=MISMATCH',f.text('install.log'))
 def test_embedded_progress_is_the_reviewed_shared_helper(self):
  helper=(R/'base/nx-lut-lite/progress.sh').read_text();entry=ENTRY.read_text()
  body='nxl_progress() {'+helper.split('nxl_progress() {',1)[1]
  self.assertIn(body,entry)
 def test_all_diagnostics_are_in_owned_log_folder(self):
  f=self.f();self.assert_installed(f)
  self.assertEqual({p.name for p in f.sd.iterdir()},{'NXDEMO51','nx-lut-demo'})
  names={p.name for p in (f.sd/'NXDEMO51/logs').iterdir()}
  self.assertTrue({'status.txt','install.log','progress.log'}.issubset(names))
 def test_known_legacy_logs_and_numbered_status_copies_migrate_byte_exact(self):
  f=self.f();names=['NX_LUT_DEMO51_INSTALL.log','NX_LUT_DEMO51_PROGRESS.log','NX_LUT_DEMO51_RUN.log','NX_LUT_DEMO51_SESSION.log','NX_LUT_DEMO51_LAUNCH.log','NX_LUT_DEMO51_STATUS.txt','NX_LUT_DEMO49_STARTUP.log','NX_LUT_DEMO49_STARTUP.status','NX_LUT_RAW422_BOOT.log','NX_LUT_DEMO51_STATUS (1).txt','NX_LUT_DEMO51_STATUS(2).txt']
  originals={name:b'OLDER_ERROR\x00'+name.encode()+bytes(range(64)) for name in names}
  for name,raw in originals.items():(f.sd/name).write_bytes(raw)
  unknown=f.sd/'NX_LUT_DEMO51_STATUS (notes).txt';unknown.write_bytes(b'USER_NOTES')
  job=f.sd/'NXDEMO51/JOB_keep';job.mkdir(parents=True);(job/'proof.bin').write_bytes(b'JOB_KEEP')
  self.assert_installed(f)
  for name,raw in originals.items():
   self.assertFalse((f.sd/name).exists(),name);found=list((f.sd/'NXDEMO51/logs/legacy').rglob(name));self.assertEqual(len(found),1,name);self.assertEqual(found[0].read_bytes(),raw)
  self.assertEqual(unknown.read_bytes(),b'USER_NOTES');self.assertEqual((job/'proof.bin').read_bytes(),b'JOB_KEEP')
 def test_legacy_archive_slot_collision_never_overwrites_old_bytes(self):
  f=self.f();(f.sd/'NX_LUT_DEMO51_INSTALL.log').write_bytes(b'OLD_LOG')
  t=f.entry.read_text().replace('nxl_migrate_old_logs\nif','mkdir -p "$LOGDIR/legacy/item.$$.0"\nprintf KEEP > "$LOGDIR/legacy/item.$$.0/previous"\nnxl_migrate_old_logs\nif')
  f.entry.write_text(t);self.assert_installed(f)
  legacy=f.sd/'NXDEMO51/logs/legacy';self.assertEqual(len(list(legacy.glob('item.*.0/previous'))),1);self.assertEqual(next(legacy.glob('item.*.0/previous')).read_bytes(),b'KEEP');self.assertEqual(next(legacy.rglob('NX_LUT_DEMO51_INSTALL.log')).read_bytes(),b'OLD_LOG')
 def test_legacy_symlink_sources_are_not_followed_or_removed(self):
  f=self.f();outside=f.w/'outside';outside.write_bytes(b'USER_DATA');old=f.sd/'NX_LUT_DEMO51_INSTALL.log';old.symlink_to(outside)
  self.assert_installed(f);self.assertTrue(old.is_symlink());self.assertEqual(outside.read_bytes(),b'USER_DATA')
 def test_false_successful_migration_does_not_delete_unmoved_source(self):
  f=self.f();old=f.sd/'NX_LUT_DEMO51_RUN.log';old.write_bytes(b'OLD_ERROR')
  mv=f.mock/'mv';mv.write_text('#!/bin/sh\nexit 0\n');mv.chmod(0o755)
  self.assert_installed(f);self.assertEqual(old.read_bytes(),b'OLD_ERROR');self.assertIn('LEGACY_MIGRATION_WARNING',f.text('install.log'))
 def test_no_boot_id_no_reset_helper_still_installs(self):
  f=self.f();self.assertFalse((f.w/'proc/sys/kernel/random/boot_id').exists());self.assert_installed(f)
 def test_unchanged_boot_id_does_not_gate_reinstall(self):
  f=self.f();p=f.w/'proc/sys/kernel/random/boot_id';p.parent.mkdir(parents=True);p.write_text('same-kernel\n')
  self.assert_installed(f);f.ready();f.calls.unlink();self.assert_installed(f);self.assertEqual(p.read_text(),'same-kernel\n')
 def test_stale_reset_markers_ignored_and_preserved(self):
  f=self.f();store=f.sd/'NXDEMO51';store.mkdir(exist_ok=True)
  before={}
  for phase in ['preinstall','postinstall','user']:
   p=store/(phase+'-kernel-reset.state');p.write_text('invalid old pending state '+phase+'\n');before[p]=p.read_bytes()
  self.assert_installed(f)
  for p,b in before.items():self.assertEqual(p.read_bytes(),b)
 def test_reset_helper_never_invoked(self):
  f=self.f();p=f.pkg/'bin/nxl-kernel-reset.arm';p.parent.mkdir()
  p.write_text('#!/bin/sh\nprintf CALLED > '+shlex.quote(str(f.w/'reset-called'))+'\nexit 99\n');p.chmod(0o755)
  self.assert_installed(f);self.assertFalse((f.w/'reset-called').exists())
 def test_failure_propagates_and_leaves_visible_status(self):
  f=self.f();f.stub_deploy(9);p=f.run();self.assertEqual(p.returncode,9);self.assertEqual(f.triggers(),[False]*3)
  self.assertIn('phase=INSTALL_FAILED',f.text('status.txt'));self.assertIn('state=ERROR',f.text('status.txt'))
  self.assertNotIn('phase=INSTALL_COMPLETE',f.text('install.log'))
 def test_exact_deployment_failure_phase_is_preserved(self):
  for state in ['ERROR','FAILED']:
   with self.subTest(state=state):
    f=self.f();status='NXDEMO51_PROGRESS1\nphase=VERIFY_MODULE\nstate='+state+'\ndetail=EXACT_DIGEST_MISMATCH\ntime=UNAVAILABLE\nend=NXDEMO51_PROGRESS_END1\n'
    f.deploy.write_text('#!/bin/sh\ncat > '+shlex.quote(str(f.sd/'NXDEMO51/logs/status.txt'))+" <<'STATUS'\n"+status+'STATUS\nexit 13\n')
    self.assertEqual(f.run().returncode,13);self.assertEqual(f.text('status.txt'),status)
 def test_missing_payload_still_reports_entry_and_failure(self):
  f=self.f();f.deploy.unlink();p=f.run();self.assertEqual(p.returncode,1);self.assertFalse(f.calls.exists());self.assertEqual(f.triggers(),[False]*3)
  self.assertIn('phase=INSTALL_ENTRY',f.text('install.log'));self.assertIn('missing or symlinked',f.text('install.log'))
 def test_symlink_deployment_refused(self):
  f=self.f();p=f.w/'other.sh';p.write_text(f.deploy.read_text());f.deploy.unlink();f.deploy.symlink_to(p)
  self.assertEqual(f.run().returncode,1);self.assertFalse(f.calls.exists())
 def test_existing_unknown_hardware_state_never_cleared(self):
  f=self.f();lock=f.w/'tmp/nx-lut-lite/memory-test.lock';lock.mkdir(parents=True);(lock/'UNKNOWN').write_text('unknown DMA\n')
  (f.sd/'NXDEMO51').mkdir(exist_ok=True);p=f.sd/'NXDEMO51/current.job';p.write_text('DO NOT REWRITE\n')
  f.stub_deploy(1);self.assertEqual(f.run().returncode,1)
  self.assertEqual((lock/'UNKNOWN').read_text(),'unknown DMA\n');self.assertEqual(p.read_text(),'DO NOT REWRITE\n')
 def test_installer_does_not_start_engine_or_listener(self):
  text=ENTRY.read_text();self.assertNotIn('--request',text);self.assertNotIn('--check',text);self.assertNotIn('boot_id',text)
  self.assertNotIn('memory-test.sh',text);self.assertNotIn('start.sh',text);self.assertNotIn('nxl-session.arm',text)
 def test_entry_progress_precedes_deploy_invocation(self):
  f=self.f();f.deploy.write_text('#!/bin/sh\ngrep -q "phase=INSTALL_ENTRY" '+shlex.quote(str(f.sd/'NXDEMO51/logs/install.log'))+' || exit 8\nprintf "%s\\n" "$1" >> '+shlex.quote(str(f.calls))+'\n')
  self.assert_installed(f)
if __name__=='__main__':
 stream=io.StringIO();suite=unittest.defaultTestLoader.loadTestsFromTestCase(InstallOnly)
 result=unittest.TextTestRunner(stream=stream,verbosity=2).run(suite);v=R/'validation';v.mkdir(exist_ok=True)
 (v/'restore-r5-entry-tests.txt').write_text(stream.getvalue())
 (v/'restore-r5-entry-tests.json').write_text(json.dumps({'tests_run':result.testsRun,'passed':result.wasSuccessful(),'failures':len(result.failures),'errors':len(result.errors),'hardware_tested':False,'reboot_called':False,'entry_sha256':sha(ENTRY),'test_sha256':sha(Path(__file__))},indent=2)+'\n')
 print(stream.getvalue(),end='');raise SystemExit(not result.wasSuccessful())
