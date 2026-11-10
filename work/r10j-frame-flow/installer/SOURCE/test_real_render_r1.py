#!/usr/bin/env python3
"""Render actual ARM files into a temporary tree; execute no camera component."""
from pathlib import Path
import hashlib,importlib.util,json,shutil,tempfile
R=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 spec=importlib.util.spec_from_file_location('r5_render',R/'installer/render_deploy.py')
 m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 names={'front':'core/bin/nxl-demo-front.arm','post':'post/bin/nxl-demo-post.arm','menu':'base/nx-lut-lite/bin/nxl-menu','session':'session/bin/nxl-session.arm','hardware_controller':'hardware/bin/nxl-allocprobe.arm','hardware_worker':'hardware/bin/nxl-bitbworker.arm','restore':'core/bin/nxl-oem-restore.arm','rearm':'core/bin/nxl-session-rearm.arm'}
 bins={n:sha(R/p) for n,p in names.items()}
 checks={}
 with tempfile.TemporaryDirectory(prefix='nx51-r5-real-render-') as tmp:
  t=Path(tmp);module=t/'source-module';shutil.copytree(R/'base/nx-lut-lite',module,ignore=shutil.ignore_patterns('__pycache__'))
  out=t/'rendered';front=R/names['front'];post=R/names['post'];menu=R/names['menu']
  rec=m.render(front,sha(front),post,sha(post),menu,sha(menu),out,module=module)
  p=out/'nx-lut-demo';runtime=p/'nx-lut-lite'
  for line in (p/'module.manifest').read_text().splitlines():
   h,n=line.split(' ',1);assert sha(runtime/n)==h,n
  checks['all_manifest_files_match']=True
  assert 'NX LUT Lite' in (runtime/'README_zh.md').read_text()
  checks['runtime_README_keeps_future_owned_module_marker']=True
  assert (runtime/'bin/nxl-session.arm').read_bytes()==(p/'bin/nxl-session.arm').read_bytes()
  checks['payload_runtime_session_match']=True
  assert (runtime/'bin/nxl-oem-restore.arm').read_bytes()==(p/'bin/nxl-oem-restore.arm').read_bytes()
  assert b'OE' in (runtime/'bin/nxl-oem-restore.arm').read_bytes() and b'OEM_LIVEVIEW_RESTORE_R5' in (runtime/'bin/nxl-oem-restore.arm').read_bytes()
  assert rec['restore_sha256']==bins['restore'] and rec['restore_before_front_ok']
  checks['restore_helper_both_copies_marker_and_identity_match']=True
  assert b'RO_MMIO_WORD_READ_R7' in (runtime/'bin/nxl-oem-restore.arm').read_bytes()
  assert sha(runtime/'bin/nxl-session-rearm.arm')==bins['rearm']==sha(p/'bin/nxl-session-rearm.arm')
  assert b'RESOURCE_REARM_R7' in (runtime/'bin/nxl-session.arm').read_bytes() and b'RESOURCE_REARM_R7' in (runtime/'bin/nxl-menu').read_bytes()
  rw=(runtime/'rearm-session.sh').read_text()
  assert '@DEMO51_' not in rw and 'PROBE_SHA='+bins['rearm'] in rw and 'SESSION_SHA='+bins['session'] in rw and 'MENU_SHA='+bins['menu'] in rw
  checks['R7_helper_marker_probe_both_copies_wrapper_all_pins_match']=True
  assert b'OEM_SD_ALIAS_BOOT_OPTIONAL_R2' in (runtime/'bin/nxl-session.arm').read_bytes()
  assert b'STATE_JOURNAL_R4' in (runtime/'bin/nxl-session.arm').read_bytes()
  assert b'FRESH_SRW_EVERY_JOB_R4' in (runtime/'bin/nxl-session.arm').read_bytes()
  assert rec['fresh_srw_every_job']
  checks['fixed_session_OEM_alias_journal_and_fresh_SRW_markers_present']=True
  assert b'MENU_FRESH_FULL_TEST_R4' in (runtime/'bin/nxl-menu').read_bytes()
  checks['menu_fresh_full_test_marker_present']=True
  assert b'KEYS_LOGS_FOLDER_R4' in (runtime/'bin/nxl-keys').read_bytes()
  checks['keys_dedicated_log_folder_marker_present']=True
  support={'keys':sha(runtime/'bin/nxl-keys'),'fileops':sha(runtime/'bin/nxl-fileops')}
  assert support['fileops']==sha(p/'patch-fileops')
  checks['fileops_payload_runtime_copies_match']=True
  assert 'OUT=${1:-/mnt/mmc/NXDEMO51/logs/diagnostic.txt}' in (runtime/'diagnose.sh').read_text()
  assert 'OUT=/mnt/mmc/NXDEMO51/logs/af-trace' in (runtime/'trace.sh').read_text()
  # Legacy card-root names may occur in the entry's migration input list;
  # none may remain a default output in the installed runtime or support ELF.
  forbidden=[b'/mnt/mmc/nx-lut-lite-startup.log',b'/mnt/mmc/nx-lut-lite-diagnostic.txt',b'OUT=/mnt/mmc/nx-lut-lite-af-trace',b'/mnt/mmc/NX_LUT_DEMO49_STARTUP.log',b'/mnt/mmc/NX_LUT_DEMO49_STARTUP.status',b'LOG=/mnt/mmc/NX_LUT',b'>> /mnt/mmc/NX_LUT']
  for candidate in runtime.rglob('*'):
   if not candidate.is_file() or not (candidate.suffix=='.sh' or candidate.parent.name=='bin'):continue
   data=candidate.read_bytes()
   for path in forbidden:assert path not in data,(candidate.name,path)
  checks['default_SD_diagnostic_paths_centralized_audited']=True
  assert not any('kernel-reset' in q.name for q in p.rglob('*'))
  assert not rec['kernel_reset_component_deployed']
  checks['no_reset_component_in_payload_or_runtime']=True
  runner=(runtime/'memory-test.sh').read_text()
  assert 'SESSION=$D/bin/nxl-session.arm' in runner
  assert 'RESTORE_SCRIPT_SHA='+sha(runtime/'restore-camera.sh') in runner
  assert 'REARM_SCRIPT_SHA='+sha(runtime/'rearm-session.sh') in runner and '--rearm)' in runner
  assert '@DEMO51_' not in (runtime/'restore-camera.sh').read_text()
  assert runner.index('restore_after_front\n    native_verified=1')>runner.index('verify_input\n    restore_after_front')
  assert not (p/'assets').exists()
  checks['restore_wrapper_rendered_pinned_before_front_ok_no_source_asset_in_patch']=True
  checks['runner_uses_same_installed_manager_as_menu']=True
  for token,which in [('FRONT_SHA','front'),('POST_SHA','post'),('MENU_SHA','menu'),('SESSION_SHA','session'),('HWCTL_SHA','hardware_controller'),('HWWORK_SHA','hardware_worker'),('RESTORE_SHA','restore')]:
   assert token+'='+bins[which] in runner,token
  checks['all_seven_runner_component_pins_match']=True
  assert rec['retire_manifest_sha256']==sha(runtime/'data/prior-front-retire.sha256')
  assert rec['retire_manifest_sha256'] in runner
  checks['prior_front_retirement_manifest_copied_and_pinned']=True
  for q in [out/'install.sh',runtime/'bt-start.sh',runtime/'start.sh',runtime/'menu.sh',runtime/'memory-test.sh']:
   text=q.read_text();assert 'boot_id' not in text and 'kernel-reset' not in text and 'demo49-install-session' not in text,q
   assert '@NX52_' not in text and '@DEMO49_' not in text and '@DEMO51_' not in text,q
  checks['entry_and_all_start_paths_have_no_UUID_reset_or_install_marker_gate']=True
  helper=(runtime/'progress.sh').read_text()
  assert 'NXDEMO51_PROGRESS1' in helper and 'end=NXDEMO51_PROGRESS_END1' in helper and 'time=%s' in helper
  checks['verified_progress_helper_and_full_format']=True
  for q in [out/'install.sh',runtime/'bt-start.sh']:
   text=q.read_text();assert 'NXDEMO51' in text and '/logs' in text and 'status.txt' in text and 'NXDEMO51_PROGRESS_END1' in text
  checks['entry_and_Bluetooth_have_visible_status_fallback']=True
  assert 'install_revision=recovery-r7' in (out/'install.sh').read_text()
  assert 'nxl_require_normal_mode' in (runtime/'start.sh').read_text()
  checks['install_revision_and_factory_process_gate_present']=True
  assert not rec['auto_isp_on_install'] and not rec['automatic_reboot'] and not rec['boot_uuid_required_for_install_or_listener']
  checks['install_only_no_auto_ISP_no_auto_reboot']=True
  count=rec['module_files'];manifest=rec['module_manifest_sha256']
 checks['temporary_tree_removed']=not t.exists()
 result={'format':'NX51_RECOVERY_R7_REAL_COMPONENT_RENDER1','passed':all(checks.values()),'hardware_tested':False,'actual_ARM_components':8,'module_files':count,'module_manifest_sha256':manifest,'binaries':bins,'support_ARM_binaries':support,'checks':checks,'source_sha256':{p:sha(R/p) for p in ['installer/render_deploy.py','installer/install.sh','installer/deploy.sh.in','installer/SOURCE/runner.sh.in','base/nx-lut-lite/restore-camera.sh','base/nx-lut-lite/rearm-session.sh','base/nx-lut-lite/bt-start.sh','base/nx-lut-lite/start.sh','base/nx-lut-lite/menu.sh','base/nx-lut-lite/progress.sh','installer/SOURCE/test_real_render_r1.py']},'limits':['Rendering does not execute native engines, camera processes or reboot.']}
 (R/'validation/render-smoke.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
