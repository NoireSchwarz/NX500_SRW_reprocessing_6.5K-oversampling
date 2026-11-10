#!/usr/bin/env python3
"""Execute real Bluetooth/start shell logic in an isolated process/filesystem model."""
from pathlib import Path
import hashlib,io,json,os,signal,subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[2];BASE=R/'base/nx-lut-lite'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
class Fixture:
 def __init__(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='nx51-bt-r1-');self.w=Path(self.tmp.name)
  self.sd=self.w/'mnt/mmc';self.sd.mkdir(parents=True);(self.sd/'nx-lut-demo').mkdir()
  self.run=self.w/'tmp/nx-lut-lite';self.run.mkdir(parents=True)
  self.module=self.w/'module';(self.module/'bin').mkdir(parents=True);(self.module/'state').mkdir()
  for n in ['0','1']:
   p=self.w/('dev/input/event'+n);p.parent.mkdir(exist_ok=True,parents=True);p.touch()
  (self.w/'proc').mkdir();self.normal=1;self.keyscan=0
  (self.module/'process.sh').write_text('nxl_require_normal_mode(){ [ "$NXBT_NORMAL" = 1 ]; }\nnxl_require_no_keyscan(){ [ "$NXBT_KEYSCAN" = 0 ]; }\n')
  (self.module/'lut.sh').write_text('#!/bin/sh\nprintf "%s\\n" "$1" >> "'+str(self.w/'lut-calls')+'"\n')
  (self.module/'bin/nxl-keys').write_text('''#!/bin/sh
mkdir -p "ROOT/proc/$$"
printf 'Name:\tnxl-keys\n' > "ROOT/proc/$$/status"
printf '%s\n' "$$" > "ROOT/tmp/nx-lut-lite/listener.ready"
printf LISTENER > "ROOT/listener-started"
exec /bin/sleep 3
'''.replace('ROOT',str(self.w)))
  (self.module/'bin/nxl-keys').chmod(0o755)
  for name in ['bt-start.sh','start.sh','menu.sh']:
   text=(BASE/name).read_text().replace('/mnt/mmc',str(self.sd)).replace('/tmp/nx-lut-lite',str(self.run)).replace('/proc/',str(self.w/'proc')+'/').replace('/dev/input/',str(self.w/'dev/input')+'/').replace('/dev/event',str(self.w/'dev/event'))
   if name=='bt-start.sh':text=text.replace('set -u\n','set -u\nsleep(){ /bin/sleep 0.005; }\n',1)
   (self.module/name).write_text(text)
 def execute(self):
  env=dict(os.environ,NXBT_NORMAL=str(self.normal),NXBT_KEYSCAN=str(self.keyscan))
  p=subprocess.Popen(['/bin/sh',str(self.module/'bt-start.sh')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True,env=env)
  try:o,e=p.communicate(timeout=8);return p.returncode,o,e
  finally:
   # This is the exclusively created fixture process group, never an arbitrary PID.
   try:os.killpg(p.pid,signal.SIGTERM)
   except ProcessLookupError:pass
 def status(self):return (self.module/'state/startup.status').read_text()
 def close(self):self.tmp.cleanup()
class BluetoothR1(unittest.TestCase):
 def f(self):f=Fixture();self.addCleanup(f.close);return f
 def assert_listens(self,f):
  rc,o,e=f.execute();self.assertEqual(rc,0,(o,e,(f.module/'state/startup.log').read_text()))
  self.assertIn('LISTENER_INPUTS_OPEN',f.status());self.assertTrue((f.w/'listener-started').exists())
  self.assertEqual((f.w/'lut-calls').read_text(),'recover-oled\n')
  self.assertIn('SUCCESS',(f.sd/'NXDEMO51/logs/status.txt').read_text())
 def test_startup_diagnostics_do_not_create_card_root_files(self):
  f=self.f();self.assert_listens(f)
  logs=f.sd/'NXDEMO51/logs';self.assertTrue((logs/'startup.log').is_file());self.assertTrue((logs/'startup.status').is_file())
  self.assertIn('LISTENER_INPUTS_OPEN',(logs/'startup.status').read_text());self.assertEqual({p.name for p in f.sd.iterdir()},{'NXDEMO51','nx-lut-demo'})
 def test_missing_kernel_uuid_and_reset_helper_can_start_listener(self):
  f=self.f();self.assertFalse((f.w/'proc/sys/kernel/random/boot_id').exists());self.assert_listens(f)
 def test_stale_install_marker_preserved_but_not_a_gate(self):
  f=self.f();p=f.run/'demo49-install-session';p.write_text('OLD HIBERNATION STATE\n');self.assert_listens(f);self.assertEqual(p.read_text(),'OLD HIBERNATION STATE\n')
 def test_same_kernel_uuid_and_invalid_old_reset_markers_ignored(self):
  f=self.f();p=f.w/'proc/sys/kernel/random/boot_id';p.parent.mkdir(parents=True);p.write_text('unchanged\n')
  d=f.sd/'NXDEMO51';d.mkdir()
  for phase in ['preinstall','postinstall','user']:(d/(phase+'-kernel-reset.state')).write_text('INVALID\n')
  self.assert_listens(f);self.assertEqual(p.read_text(),'unchanged\n')
 def test_existing_reset_tool_is_never_called(self):
  f=self.f();p=f.module/'bin/nxl-kernel-reset.arm';p.write_text('#!/bin/sh\ntouch "'+str(f.w/'RESET_CALLED')+'"\nexit 1\n');p.chmod(0o755)
  self.assert_listens(f);self.assertFalse((f.w/'RESET_CALLED').exists())
 def test_disabled_module_stays_disabled_and_reports(self):
  f=self.f();(f.module/'state/disabled').touch();self.assertEqual(f.execute()[0],1);self.assertEqual(f.status(),'DISABLED\n');self.assertFalse((f.w/'listener-started').exists());self.assertIn('ERROR',(f.sd/'NXDEMO51/logs/status.txt').read_text())
 def test_actual_factory_mode_gate_retained(self):
  f=self.f();f.normal=0;self.assertEqual(f.execute()[0],1);self.assertIn('FAILED_WAITING_NORMAL_MODE',f.status());self.assertFalse((f.w/'listener-started').exists())
 def test_legacy_keyscan_conflict_still_refused(self):
  f=self.f();f.keyscan=1;self.assertEqual(f.execute()[0],1);self.assertIn('FAILED_LEGACY_KEYSCAN_CONFLICT',f.status());self.assertFalse((f.w/'listener-started').exists())
 def test_listener_start_does_not_clear_hardware_unknown_lock(self):
  f=self.f();p=f.run/'memory-test.lock';p.mkdir();(p/'UNKNOWN').write_text('unknown DMA\n')
  self.assert_listens(f);self.assertEqual((p/'UNKNOWN').read_text(),'unknown DMA\n')
 def test_menu_shell_ignores_stale_install_marker_and_preserves_it(self):
  f=self.f();p=f.run/'demo49-install-session';p.write_text('OLD HIBERNATION STATE\n')
  menu=f.module/'bin/nxl-menu';menu.write_text('#!/bin/sh\nprintf "%s\\n" "$1" > "'+str(f.w/'menu-args')+'"\n');menu.chmod(0o755)
  env=dict(os.environ,NXBT_NORMAL='1',NXBT_KEYSCAN='0')
  result=subprocess.run(['/bin/sh',str(f.module/'menu.sh')],env=env,capture_output=True,text=True,timeout=5)
  self.assertEqual(result.returncode,0,(result.stdout,result.stderr));self.assertEqual((f.w/'menu-args').read_text(),'--toggle\n');self.assertEqual(p.read_text(),'OLD HIBERNATION STATE\n')
if __name__=='__main__':
 stream=io.StringIO();result=unittest.TextTestRunner(stream=stream,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(BluetoothR1))
 v=R/'validation';v.mkdir(exist_ok=True);(v/'install-r4-bluetooth-tests.txt').write_text(stream.getvalue())
 (v/'install-r4-bluetooth-tests.json').write_text(json.dumps({'tests_run':result.testsRun,'passed':result.wasSuccessful(),'failures':len(result.failures),'errors':len(result.errors),'hardware_tested':False,'source_sha256':{str(p.relative_to(R)):sha(p) for p in [BASE/'bt-start.sh',BASE/'start.sh',BASE/'menu.sh',Path(__file__)]}},indent=2)+'\n')
 print(stream.getvalue(),end='');raise SystemExit(not result.wasSuccessful())
