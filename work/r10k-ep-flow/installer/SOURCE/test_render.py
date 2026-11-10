#!/usr/bin/env python3
"""Renderer tests use labeled minimal ARM headers, never execute the models."""
from pathlib import Path
import hashlib,importlib.util,json,shutil,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT.parent/'base/nx-lut-lite'
spec=importlib.util.spec_from_file_location('demo49_render',ROOT/'render_deploy.py');M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def arm(p,version):
 b=bytearray(64);b[:7]=b'\x7fELF\x01\x01\x01';b[18:20]=b'\x28\0';p.write_bytes(bytes(b)+b'HOST_HEADER_MODEL_NOT_EXECUTABLE\0'+version.encode()+b'\0')
class Render(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='demo49-render-model-');self.w=Path(self.tmp.name);self.module=self.w/'module';(self.module/'bin').mkdir(parents=True)
  for n in ['cube2lut.arm','nx_slotwrite.arm','nx_regtabwrite.arm','nxl-lutobserve.arm','nxl-tablecheck.arm','nxl-keys']:shutil.copy2(BASE/'bin'/n,self.module/'bin'/n)
  for n in ['lut.sh','install.sh','start.sh','menu.sh','process.sh','bt-start.sh','diagnose.sh','trace.sh','bluetoothd-wrapper.sh','progress.sh','restore-camera.sh','rearm-session.sh','LICENSE','NOTICE.md','STRENGTHS_NOTICE.md']:shutil.copy2(BASE/n,self.module/n)
  for n in ['integration','looks','data']:shutil.copytree(BASE/n,self.module/n)
  (self.module/'data/prior-front-retire.sha256').write_text('HOST_MODEL_MANIFEST_NOT_CAMERA_PROOF\n')
  self.front=self.w/'front';self.post=self.w/'post';self.menu=self.w/'menu';arm(self.front,'NXL_DEMO_FRONT=0.3.6-demo50; FNV_WORDLOAD_R6 OBSERVED_PHASE_TIMING_R6');arm(self.post,'NXL_RAW422_POST=0.4.0-demo51-422; FINAL_CHECK_DELEGATION_R6');arm(self.menu,'NX_LUT_MENU_VERSION=0.4.0-demo51-422\0 MENU_STATUS_REVISION=STDOUT_STATUS_ONLY_R3 MENU_FRESH_FULL_TEST_R4 CAMERA_RECOVERY_R5 RESOURCE_REARM_R7');self.session=self.w/'session';arm(self.session,'NX51STATUS1 NXL_SESSION_VERSION=0.4.0-demo51-422; OEM_SD_ALIAS_BOOT_OPTIONAL_R2 STATE_JOURNAL_R4 FRESH_SRW_EVERY_JOB_R4 RESOURCE_REARM_R7');self.restore=self.w/'restore';arm(self.restore,'NXL_OEM_RESTORE=0.4.0-demo51-422; OEM_LIVEVIEW_RESTORE_R5 RO_MMIO_WORD_READ_R7');self.rearm=self.w/'rearm';arm(self.rearm,'RESOURCE_REARM_R7');self.reset=self.w/'reset';arm(self.reset,'NXL_KERNEL_RESET_VERSION=1; LINUX_REBOOT_ONLY');self.out=self.w/'rendered'
 def tearDown(self):self.tmp.cleanup()
 def render(self):return M.render(self.front,sha(self.front),self.post,sha(self.post),self.menu,sha(self.menu),self.out,self.module,session=self.session,session_sha=sha(self.session),restore=self.restore,restore_sha=sha(self.restore),rearm=self.rearm,rearm_sha=sha(self.rearm))
 def test_exact_runtime_manifest_and_twelve_strength_cases(self):
  r=self.render();p=self.out/'nx-lut-demo';runtime=p/'nx-lut-lite';self.assertEqual(sha(p/'module.manifest'),r['module_manifest_sha256']);runner=(runtime/'memory-test.sh').read_text();self.assertNotIn('@DEMO49_',runner);self.assertNotIn('@DEMO51_',runner);self.assertIn('NXDEMO51',runner);self.assertIn('SESSION_SHA='+sha(self.session),runner);self.assertIn('SESSION=$D/bin/nxl-session.arm',runner);self.assertEqual(r['retire_manifest_sha256'],sha(runtime/'data/prior-front-retire.sha256'));self.assertEqual(r['install_revision'],'recovery-r7');self.assertTrue(r['oem_sd_alias_supported'])
  declaration=json.loads((p/'demo-luts.json').read_text());self.assertEqual(set(declaration['looks']),set(M.KNOWN_LOOKS));self.assertEqual(declaration['space'],'CrCbY-BT601-full-v1');self.assertFalse((runtime/'looks/nxl-strengths').exists());self.assertEqual((self.module/'memory-test.sh').read_text(),runner)
  self.assertEqual((runtime/'uninstall.sh').read_text(),(ROOT/'uninstall.sh').read_text());self.assertFalse((runtime/'bin/nxl-allocprobe.arm').exists());self.assertEqual(sha(runtime/'bin/nxl-session.arm'),sha(self.session));self.assertEqual(sha(p/'bin/nxl-session.arm'),sha(self.session));self.assertEqual(r['session_sha256'],sha(self.session));self.assertFalse((runtime/'bin/nxl-kernel-reset.arm').exists());self.assertFalse((p/'bin/nxl-kernel-reset.arm').exists());self.assertFalse(r['kernel_reset_component_deployed']);self.assertIn('DEMO51_BT_HOOK',(runtime/'bluetoothd-wrapper.sh').read_text())
  self.assertEqual(sha(runtime/'bin/nxl-oem-restore.arm'),sha(self.restore));self.assertEqual(sha(p/'bin/nxl-oem-restore.arm'),sha(self.restore));self.assertIn('RESTORE_SHA='+sha(self.restore),runner);self.assertIn('RESTORE_SCRIPT_SHA='+sha(runtime/'restore-camera.sh'),runner);self.assertNotIn('@DEMO51_', (runtime/'restore-camera.sh').read_text());self.assertTrue(r['restore_before_front_ok'])
  for line in (p/'module.manifest').read_text().splitlines():h,n=line.split(' ',1);self.assertEqual(sha(runtime/n),h)
 def test_restore_helper_identity_and_version_are_required(self):
  with self.assertRaises(SystemExit):M.render(self.front,sha(self.front),self.post,sha(self.post),self.menu,sha(self.menu),self.out,self.module,session=self.session,session_sha=sha(self.session),restore=self.restore,restore_sha='0'*64)
  arm(self.restore,'NXL_OEM_RESTORE=OLD')
  with self.assertRaises(SystemExit):self.render()
 def test_missing_camera_recovery_menu_action_refused(self):
  arm(self.menu,'NX_LUT_MENU_VERSION=0.4.0-demo51-422\0 STDOUT_STATUS_ONLY_R3 MENU_FRESH_FULL_TEST_R4')
  with self.assertRaises(SystemExit):self.render()
 def test_missing_reset_component_is_not_an_install_dependency(self):
  self.reset.unlink();r=self.render();self.assertFalse(r['kernel_reset_component_deployed']);self.assertFalse((self.out/'nx-lut-demo/bin/nxl-kernel-reset.arm').exists())
 def test_wrong_session_version_or_missing_alias_fix_refused(self):
  for version in ['OTHER_SESSION_0.1','NX51STATUS1 NXL_SESSION_VERSION=0.4.0-demo51-422; DURABLE_JOB_STATE1','NX51STATUS1 NXL_SESSION_VERSION=0.4.0-demo51-422; OEM_SD_ALIAS_BOOT_OPTIONAL_R2','NX51STATUS1 NXL_SESSION_VERSION=0.4.0-demo51-422; OEM_SD_ALIAS_BOOT_OPTIONAL_R2 STATE_JOURNAL_R4']:
   with self.subTest(version=version):
    arm(self.session,version)
    with self.assertRaises(SystemExit):self.render()
 def test_wrong_session_hash_refused(self):
  with self.assertRaises(SystemExit):M.render(self.front,sha(self.front),self.post,sha(self.post),self.menu,sha(self.menu),self.out,self.module,session=self.session,session_sha='0'*64,reset=self.reset,reset_sha=sha(self.reset),restore=self.restore,restore_sha=sha(self.restore),rearm=self.rearm,rearm_sha=sha(self.rearm))
 def test_changed_source_lut_not_relabelled_native(self):
  (self.module/'looks/kodak_portra400.bin').write_bytes(b'X'*19712)
  with self.assertRaises(SystemExit):self.render()
 def test_unbundled_source_lut_refused(self):
  (self.module/'looks/custom.bin').write_bytes(b'X'*19712)
  with self.assertRaises(SystemExit):self.render()
 def test_wrong_front_hash_refused(self):
  with self.assertRaises(SystemExit):M.render(self.front,'0'*64,self.post,sha(self.post),self.menu,sha(self.menu),self.out,self.module,session=self.session,session_sha=sha(self.session),reset=self.reset,reset_sha=sha(self.reset),restore=self.restore,restore_sha=sha(self.restore),rearm=self.rearm,rearm_sha=sha(self.rearm))
 def test_old_front_version_refused(self):
  arm(self.front,'NXL_ALLOCPROBE=0.3.4-p48.1;')
  with self.assertRaises(SystemExit):self.render()
 def test_same_protocol_without_R6_front_implementation_refused(self):
  arm(self.front,'NXL_DEMO_FRONT=0.3.6-demo50;')
  with self.assertRaises(SystemExit):self.render()
 def test_same_protocol_without_R6_final_check_contract_refused(self):
  arm(self.post,'NXL_RAW422_POST=0.4.0-demo51-422;')
  with self.assertRaises(SystemExit):self.render()
 def test_OEM_helper_without_R7_read_fix_refused(self):
  arm(self.restore,'NXL_OEM_RESTORE=0.4.0-demo51-422; OEM_LIVEVIEW_RESTORE_R5')
  with self.assertRaises(SystemExit):self.render()
 def test_non_arm_refused(self):
  self.post.write_text('HOST_OTHER_FILE')
  with self.assertRaises(SystemExit):self.render()
 def test_source_symlink_refused(self):
  p=self.module/'start.sh';p.unlink();p.symlink_to(self.module/'menu.sh')
  with self.assertRaises(SystemExit):self.render()
 def test_existing_output_not_overwritten(self):
  self.out.mkdir();(self.out/'keep').write_text('KEEP')
  with self.assertRaises(SystemExit):self.render()
  self.assertEqual((self.out/'keep').read_text(),'KEEP')
class MenuStatus(unittest.TestCase):
 def test_strict_raw422_status_and_context_actions(self):
  import subprocess
  header=(BASE/'src/ui_state.h').resolve()
  code=r"""#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "HEADER"
static int checks;
static void status(char *out,const char *state,const char *extra){snprintf(out,2048,"NX51STATUS1\nversion=0.4.0-demo51-422\njob=/mnt/mmc/NXDEMO51/JOB_test\nstate=%s\nfront=-\nfront_verified=0\nresume=none\nreason=TEST\n%s%s",state,extra,"end=NX51END1\n");}
int main(void){char text[2048],bad[2048];struct nxl_raw_status out;
const char *states[]={"NEED_STILL_INIT","FRONT_RUNNING","NEED_JPEG_INIT","BACKEND_RUNNING","COMPLETE","RETRYABLE","RESTART_REQUIRED"};
for(unsigned i=0;i<7;i++){status(text,states[i],"look_id=raw422\nstrength=100\n");assert(nxl_raw_status_parse(text,&out));assert(out.state==(enum nxl_raw_state)(i+1));assert(!strcmp(out.look_id,"raw422")&&out.strength==100);checks++;}
status(text,"NEED_STILL_INIT","look_id=-\nstrength=0\n");assert(nxl_raw_status_parse(text,&out)&&!out.look_id[0]);checks++;
status(text,"NOT_A_STATE","");assert(!nxl_raw_status_parse(text,&out));checks++;
status(text,"NEED_JPEG_INIT","state=COMPLETE\n");assert(!nxl_raw_status_parse(text,&out));checks++;
status(text,"NEED_JPEG_INIT","reason=DUPLICATE\n");assert(!nxl_raw_status_parse(text,&out));checks++;
status(text,"NEED_JPEG_INIT","look_id=kodak_portra400\nstrength=100\n");assert(!nxl_raw_status_parse(text,&out));checks++;
status(text,"NEED_JPEG_INIT","look_id=raw422\nstrength=50bad\n");assert(!nxl_raw_status_parse(text,&out));checks++;
status(text,"NEED_JPEG_INIT","");strcpy(bad,text);char *end=strstr(bad,"end=NX51END1");*end=0;assert(!nxl_raw_status_parse(bad,&out));checks++;
strcpy(bad,text);bad[strlen(bad)-1]=0;assert(!nxl_raw_status_parse(bad,&out));checks++;
strcpy(bad,text);strcat(bad,"state=COMPLETE\n");assert(!nxl_raw_status_parse(bad,&out));checks++;
strcpy(bad,text);char *v=strstr(bad,"0.4.0-demo51-422");v[0]='9';assert(!nxl_raw_status_parse(bad,&out));checks++;
assert(!strcmp(nxl_raw_action(NXL_RAW_NEED_STILL_INIT),"PREPARE RAW"));assert(!strcmp(nxl_raw_action(NXL_RAW_NEED_JPEG_INIT),"ENCODE JPEG"));assert(!strcmp(nxl_raw_action(NXL_RAW_COMPLETE),"VIEW STATUS"));checks++;
memset(&out,0,sizeof out);out.state=NXL_RAW_RETRYABLE;strcpy(out.reason,"FRONT_REINSTALL_REQUIRED");assert(!strcmp(nxl_raw_status_action(&out),"REINSTALL PACKAGE"));checks++;
assert(strstr(nxl_raw_status_hint(&out),"REINSTALL FULL PACKAGE"));checks++;
assert(NXL_DEMO_ROWS==9);checks++;
assert(!strcmp(nxl_raw_action(NXL_RAW_RESTART_REQUIRED),"CHECK REPORT"));checks++;
printf("PASS %d strict RAW422 menu status/action cases\n",checks);return 0;}
""".replace('HEADER',str(header))
  with tempfile.TemporaryDirectory(prefix='raw422-menu-state-') as tmp:
   w=Path(tmp);(w/'test.c').write_text(code)
   subprocess.run(['cc','-O2','-Wno-unused-function',str(w/'test.c'),'-o',str(w/'test')],check=True)
   result=subprocess.run([str(w/'test')],check=True,capture_output=True,text=True)
  self.assertIn('PASS 22 strict',result.stdout)
  validation=ROOT.parent/'validation';validation.mkdir(exist_ok=True)
  (validation/'menu-status-tests.json').write_text(json.dumps(dict(hardware_tested=False,checks=22,result=result.stdout.strip(),header_sha256=sha(header),status_timeout_ms=500,no_LUT_dependency_for_RAW_action=True),indent=2)+'\n')
if __name__=='__main__':unittest.main(verbosity=2)
