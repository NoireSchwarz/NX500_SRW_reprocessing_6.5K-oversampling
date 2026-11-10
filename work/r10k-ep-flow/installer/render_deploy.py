#!/usr/bin/env python3
"""Render a complete Demo51 session runtime/payload; does not execute ARM or package ZIP."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,sys,importlib.util
ROOT=Path(__file__).resolve().parent
BASE=ROOT.parent/'base/nx-lut-lite'
HELPER_SHA='c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d'
KNOWN_LOOKS={'fuji_provia100f':'efa7b4a4620b7f11371e585871ec99d5406b850996285d0ffc2cea3644a1e928','kodak_portra400':'5ea7bfd6367ce4830ab9104e976040ab6ff163fe07bd38aa8620bf252b34d7db','mono_hp5':'f773afdf8f5e22329df2a3ac29ceddd7ae41b831f87457de03a1ff923920b874'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def exact_arm(p,h):
 if p.is_symlink() or not p.is_file() or sha(p)!=h:raise SystemExit('ARM identity mismatch: '+str(p))
 b=p.read_bytes()
 if b[:7]!=b'\x7fELF\x01\x01\x01' or b[18:20]!=b'\x28\0':raise SystemExit('Not ARM32 LE ELF: '+str(p))
def render(front,front_sha,post,post_sha,menu,menu_sha,out,module=BASE,session=None,session_sha=None,reset=None,reset_sha=None,restore=None,restore_sha=None,rearm=None,rearm_sha=None):
 if any(p.is_symlink() for p in module.rglob('*')):raise SystemExit('Source module symlinks are not supported')
 session=session or ROOT.parent/'session/bin/nxl-session.arm';session_sha=session_sha or sha(session)
 restore=restore or ROOT.parent/'core/bin/nxl-oem-restore.arm';restore_sha=restore_sha or sha(restore)
 rearm=rearm or ROOT.parent/'core/bin/nxl-session-rearm.arm';rearm_sha=rearm_sha or sha(rearm)
 # Legacy optional reset arguments are ignored: no reboot component is deployed.
 for p,h in [(front,front_sha),(post,post_sha),(menu,menu_sha),(session,session_sha),(restore,restore_sha),(rearm,rearm_sha)]:exact_arm(p,h)
 if sha(ROOT/'patch-fileops')!=HELPER_SHA:raise SystemExit('Bundled helper identity mismatch')
 if b'KEYS_LOGS_FOLDER_R4' not in (module/'bin/nxl-keys').read_bytes():raise SystemExit('Missing keys dedicated-log-folder fix')
 if b'NXL_DEMO_FRONT=0.3.6-demo50;' not in front.read_bytes():raise SystemExit('Wrong frontend version')
 if b'NXL_RAW422_POST=0.4.0-demo51-422;' not in post.read_bytes():raise SystemExit('Wrong native post version')
 if b'FNV_WORDLOAD_R6' not in front.read_bytes() or b'OBSERVED_PHASE_TIMING_R6' not in front.read_bytes():raise SystemExit('Missing R6 full-frame FNV or phase timing')
 if b'FINAL_CHECK_DELEGATION_R6' not in post.read_bytes():raise SystemExit('Missing R6 final-check delegation contract')
 if b'NX_LUT_MENU_VERSION=0.4.0-demo51-422\0' not in menu.read_bytes() or b'STDOUT_STATUS_ONLY_R3' not in menu.read_bytes() or b'MENU_FRESH_FULL_TEST_R4' not in menu.read_bytes():raise SystemExit('Wrong menu version or missing stdout status fix')
 if b'NXL_OEM_RESTORE=0.4.0-demo51-422; OEM_LIVEVIEW_RESTORE_R5' not in restore.read_bytes():raise SystemExit('Wrong OEM restore helper version')
 if b'RO_MMIO_WORD_READ_R7' not in restore.read_bytes():raise SystemExit('Missing R7 guarded register-read implementation')
 if b'CAMERA_RECOVERY_R5' not in menu.read_bytes():raise SystemExit('Missing camera recovery menu action')
 if any(b'RESOURCE_REARM_R7' not in p.read_bytes() for p in [menu,session,rearm]):raise SystemExit('Missing R7 resource rearm protocol')
 if b'NX51STATUS1' not in session.read_bytes() or b'NXL_SESSION_VERSION=0.4.0-demo51-422;' not in session.read_bytes() or b'OEM_SD_ALIAS_BOOT_OPTIONAL_R2' not in session.read_bytes() or b'STATE_JOURNAL_R4' not in session.read_bytes() or b'FRESH_SRW_EVERY_JOB_R4' not in session.read_bytes():raise SystemExit('Wrong session manager version or missing OEM SD alias fix')
 for component,prefix in [(ROOT.parent/'hardware/bin/nxl-allocprobe.arm',b'NXL_ALLOCPROBE=0.4.0-demo51-422;'),(ROOT.parent/'hardware/bin/nxl-bitbworker.arm',b'NXL_PIPEWORKER=0.4.0-demo51-422;')]:
  exact_arm(component,sha(component))
  if prefix not in component.read_bytes() or b'ONE_NATIVE_JPEG; CANDIDATE_FORMAT1_NV16; NO_LUT' not in component.read_bytes():raise SystemExit('Wrong RAW422 native component: '+component.name)
 if out.exists():raise SystemExit('Output must be a new directory: '+str(out))
 out.mkdir(parents=True);dst=out/'nx-lut-demo';runtime=dst/'nx-lut-lite';(runtime/'bin').mkdir(parents=True)
 binary_names=['cube2lut.arm','nx_slotwrite.arm','nx_regtabwrite.arm','nxl-lutobserve.arm','nxl-tablecheck.arm','nxl-keys']
 scripts=['lut.sh','install.sh','start.sh','menu.sh','process.sh','bt-start.sh','diagnose.sh','trace.sh','bluetoothd-wrapper.sh','progress.sh','restore-camera.sh','rearm-session.sh']
 docs=['LICENSE','NOTICE.md','STRENGTHS_NOTICE.md']
 for n in binary_names:shutil.copy2(module/'bin'/n,runtime/'bin'/n)
 shutil.copy2(restore,runtime/'bin/nxl-oem-restore.arm');shutil.copy2(menu,runtime/'bin/nxl-menu');shutil.copy2(session,runtime/'bin/nxl-session.arm');shutil.copy2(ROOT/'patch-fileops',runtime/'bin/nxl-fileops')
 shutil.copy2(rearm,runtime/'bin/nxl-session-rearm.arm')
 for n in scripts+docs:shutil.copy2(module/n,runtime/n)
 restore_script=(runtime/'restore-camera.sh').read_text().replace('@DEMO51_RESTORE_SHA256@',restore_sha).replace('@DEMO51_MENU_SHA256@',menu_sha)
 if '@DEMO51_' in restore_script:raise SystemExit('Unresolved restore wrapper token')
 (runtime/'restore-camera.sh').write_text(restore_script)
 rearm_script=(runtime/'rearm-session.sh').read_text().replace('@DEMO51_SESSION_SHA256@',session_sha).replace('@DEMO51_REARM_SHA256@',rearm_sha).replace('@DEMO51_MENU_SHA256@',menu_sha)
 if '@DEMO51_' in rearm_script:raise SystemExit('Unresolved rearm wrapper token')
 (runtime/'rearm-session.sh').write_text(rearm_script)
 # Hook-family marker changes, while known legacy backup paths remain compatible.
 wrapper=runtime/'bluetoothd-wrapper.sh';wrapper.write_text(wrapper.read_text().replace('NX_LUT_LITE_DEMO49_BT_HOOK_V1','NX_LUT_LITE_DEMO51_BT_HOOK_V1'))
 shutil.copy2(ROOT/'uninstall.sh',runtime/'uninstall.sh')
 shutil.copy2(ROOT/'README_zh.md',runtime/'README_zh.md')
 for folder in ['integration','looks','data']:shutil.copytree(module/folder,runtime/folder,symlinks=False)
 if {p.stem for p in (runtime/'looks').glob('*.bin')}!=set(KNOWN_LOOKS):raise SystemExit('Demo requires exactly three known native source LUTs')
 tools=BASE/'tools';sys.path.insert(0,str(tools))
 spec=importlib.util.spec_from_file_location('demo49_strengths',tools/'make_strengths.py');strengths=importlib.util.module_from_spec(spec);spec.loader.exec_module(strengths)
 cases=[];look_record={}
 for ident,source_hash in sorted(KNOWN_LOOKS.items()):
  source=runtime/'looks'/(ident+'.bin')
  if sha(source)!=source_hash:raise SystemExit('Unknown demo LUT source: '+ident)
  raw=source.read_bytes();strengths.validate(raw);effective={}
  for percent in (25,50,75,100):
   h=hashlib.sha256(strengths.variant(raw,percent)).hexdigest();effective[str(percent)]=h
   cases.append(ident+':'+str(percent)+') known_base_sha='+source_hash+';known_effective_sha='+h+';;')
  look_record[ident]={'source_sha256':source_hash,'effective_sha256':effective}
 sys.path.pop(0)
 declaration={'format':'NXDEMO49_KNOWN_LUTS1','space':'CrCbY-BT601-full-v1','source_bytes':19712,'strength_policy':'existing live effective_strength and validated cache; no global cache installation','looks':look_record}
 (runtime/'demo-luts.json').write_text(json.dumps(declaration,indent=2)+'\n');shutil.copy2(runtime/'demo-luts.json',dst/'demo-luts.json')
 values={'HWCTL_SHA256':sha(ROOT.parent/'hardware/bin/nxl-allocprobe.arm'),'HWWORK_SHA256':sha(ROOT.parent/'hardware/bin/nxl-bitbworker.arm'),'FRONT_SHA256':front_sha,'POST_SHA256':post_sha,'MENU_SHA256':menu_sha,'CV_SHA256':sha(runtime/'bin/cube2lut.arm'),'TC_SHA256':sha(runtime/'bin/nxl-tablecheck.arm'),'ENGINE_SHA256':sha(runtime/'lut.sh')}
 runner=(ROOT/'SOURCE/runner.sh.in').read_text()
 runner=runner.replace('# @DEMO49_LUT_ALLOW_CASES@','\n'.join(cases))
 for k,v in values.items():runner=runner.replace('@DEMO49_'+k+'@',v)
 runner=runner.replace('@DEMO51_SESSION_SHA256@',session_sha)
 runner=runner.replace('@DEMO51_RESTORE_SHA256@',restore_sha).replace('@DEMO51_RESTORE_SCRIPT_SHA256@',sha(runtime/'restore-camera.sh'))
 runner=runner.replace('@DEMO51_REARM_SCRIPT_SHA256@',sha(runtime/'rearm-session.sh'))
 runner=runner.replace('@DEMO51_RETIRE_MANIFEST_SHA256@',sha(runtime/'data/prior-front-retire.sha256'))
 if '@DEMO49_' in runner or '@DEMO51_' in runner:raise SystemExit('Unresolved runner token')
 (runtime/'memory-test.sh').write_text(runner)
 # The reviewed base copy is kept consistent for the parent's integration build.
 (module/'memory-test.sh').write_text(runner)
 for p in runtime.rglob('*'):
  if p.is_symlink() or (not p.is_dir() and not p.is_file()):raise SystemExit('Unsafe module entry: '+str(p))
  if p.is_file() and (p.suffix=='.sh' or p.parent.name=='bin'):p.chmod(0o755)
 files=sorted(p for p in runtime.rglob('*') if p.is_file())
 if any(not re.fullmatch(r'[A-Za-z0-9_./-]+',str(p.relative_to(runtime))) or '..' in str(p.relative_to(runtime)) for p in files):raise SystemExit('Unsupported module filename')
 manifest=''.join(sha(p)+' '+str(p.relative_to(runtime))+'\n' for p in files)
 (dst/'module.manifest').write_text(manifest)
 deploy=(ROOT/'deploy.sh.in').read_text().replace('@DEMO49_MANIFEST_SHA256@',sha(dst/'module.manifest'))
 if '@DEMO49_' in deploy:raise SystemExit('Unresolved deploy token')
 (dst/'deploy.sh').write_text(deploy);(dst/'deploy.sh').chmod(0o755)
 shutil.copy2(ROOT/'patch-fileops',dst/'patch-fileops');(dst/'patch-fileops').chmod(0o755)
 (dst/'bin').mkdir();shutil.copy2(restore,dst/'bin/nxl-oem-restore.arm');shutil.copy2(session,dst/'bin/nxl-session.arm');shutil.copy2(front,dst/'bin/nxl-demo-front.arm');shutil.copy2(post,dst/'bin/nxl-demo-post.arm')
 shutil.copy2(rearm,dst/'bin/nxl-session-rearm.arm')
 shutil.copy2(ROOT.parent/'hardware/bin/nxl-allocprobe.arm',dst/'bin/nxl-hw-controller.arm');shutil.copy2(ROOT.parent/'hardware/bin/nxl-bitbworker.arm',dst/'bin/nxl-hw-worker.arm')
 for p in (dst/'bin').iterdir():p.chmod(0o755)
 for n in ['info.tg','nx_cs.adj']:shutil.copy2(ROOT/n,out/n)
 entry=(ROOT/'install.sh').read_text()
 if '@NX52_' in entry:raise SystemExit('Unresolved entry token')
 (out/'install.sh').write_text(entry)
 for folder in ['UNINSTALL','ROLLBACK']:shutil.copytree(ROOT/folder,out/folder)
 for p in [out/'install.sh',out/'UNINSTALL/install.sh',out/'ROLLBACK/install.sh',dst/'deploy.sh',runtime/'memory-test.sh',runtime/'restore-camera.sh']:subprocess.run(['/bin/sh','-n',str(p)],check=True)
 record={'format':'NXDEMO51_RENDERED_INSTALL1','version':'0.4.0-demo51-422','install_revision':'recovery-r7','restore_sha256':restore_sha,'rearm_sha256':rearm_sha,'resource_rearm_native_jobs':0,'resource_rearm_CAP_free':0,'existing_source_asset_sha256':'dfd6608e4ee05f0d5b4b402fedd3c928aee509595e6964efa4c1ce87f1d9715c','restore_before_front_ok':True,'manual_restore_changes_session_state':False,'oem_sd_alias_supported':True,'fresh_srw_every_job':True,'automatic_reboot':False,'boot_uuid_required_for_install_or_listener':False,'session_sha256':session_sha,'retire_manifest_sha256':sha(runtime/'data/prior-front-retire.sha256'),'kernel_reset_component_deployed':False,'front_sha256':front_sha,'post_sha256':post_sha,'menu_sha256':menu_sha,'module_manifest_sha256':sha(dst/'module.manifest'),'module_files':len(files),'auto_isp_on_install':False,'source':'SAM_0183.SRW','source_assets_copied':False,'payload_sha256':{str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file()}}
 (out/'installer-identities.json').write_text(json.dumps(record,indent=2)+'\n');return record
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--front',type=Path,required=True);p.add_argument('--front-sha',required=True);p.add_argument('--post',type=Path,required=True);p.add_argument('--post-sha',required=True);p.add_argument('--menu',type=Path,required=True);p.add_argument('--menu-sha',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 print(json.dumps(render(a.front,a.front_sha,a.post,a.post_sha,a.menu,a.menu_sha,a.output),indent=2))
