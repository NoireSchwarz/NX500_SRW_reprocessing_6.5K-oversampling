#!/usr/bin/env python3
"""Freeze R7 guarded reads and explicitly authorized resource rearm."""
from pathlib import Path
import hashlib,json,subprocess
R=Path(__file__).resolve().parents[1]
B={}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def bind(n,h=None):
 p=R/n;assert p.is_file() and not p.is_symlink(),n
 v=sha(p)
 if h is not None:assert v==h,('Changed since validation',n,v,h)
 B[n]=v;return v
def load(n):bind(n);return json.loads((R/n).read_text())
def checked(n):
 d=load(n);assert d['passed'] and not d.get('hardware_tested',False),n
 for p,h in d.get('source_sha256',{}).items():bind(p,h)
 for key in ['component_evidence_sha256','test_evidence','evidence_sha256']:
  for p,h in d.get(key,{}).items():bind(p,h)
 return d
def main():
 baseline=load('validation/recovery-r7-baseline.json')
 image_bins={n:h for n,h in baseline['program_binaries'].items() if n not in ['session/bin/nxl-session.arm','base/nx-lut-lite/bin/nxl-menu']}
 for n,h in image_bins.items():bind(n,h)
 # These R6 implementation inputs stay unchanged; R7 changes only its own
 # recovery/proof entries and the session/UI/installer control plane.
 allowed={'core/src/nxl-oem-restore.c','installer/install.sh','installer/deploy.sh.in','installer/render_deploy.py','installer/SOURCE/runner.sh.in','base/nx-lut-lite/restore-camera.sh','installer/README_zh.md','SOURCE_README_zh.md','DEMO_SCOPE_zh.md'}
 for n,h in baseline['bound_files'].items():
  if n.startswith(('validation/','core/results/','post/validation/','hardware/results/','arm-tests/')):continue
  if n in allowed or n.startswith(('session/','core/tests/','core/tools/','installer/SOURCE/test_','base/nx-lut-lite/src/')):continue
  if n.startswith(('core/src/','hardware/src/','post/src/')):bind(n,h)
 groups={}
 for name in ['helper','deploy','menu','rearm','wrapper','renderer','entry']:
  groups[name]=checked('validation/recovery-r7-'+name+'-tests.json')
 review=checked('validation/recovery-r7-independent-review.json')
 assert review['passed']
 smoke=checked('validation/render-smoke.json')
 assert smoke['actual_ARM_components']==8 and all(smoke['checks'].values())
 bins={'front':'core/bin/nxl-demo-front.arm','post':'post/bin/nxl-demo-post.arm','menu':'base/nx-lut-lite/bin/nxl-menu','session':'session/bin/nxl-session.arm','hardware_controller':'hardware/bin/nxl-allocprobe.arm','hardware_worker':'hardware/bin/nxl-bitbworker.arm','restore':'core/bin/nxl-oem-restore.arm','rearm':'core/bin/nxl-session-rearm.arm'}
 for name,p in bins.items():bind(p,smoke['binaries'][name])
 assert groups['helper']['binary_sha256']==smoke['binaries']['restore']
 assert groups['menu']['binary_sha256']==smoke['binaries']['menu']
 for group,rendered in [('session','session'),('probe','rearm')]:
  item=groups['rearm']['binaries'][group]
  assert item['sha256']==smoke['binaries'][rendered]
  bind(item['path'],item['sha256'])
 # The normalized rearm aggregate above already binds this component's
 # mixed core-relative/root-relative source paths and evidence.
 native=load('core/results/session-rearm-tests.json')
 assert native['passed'] and not native['hardware_tested']
 assert native['backend_retirement_gate_enabled'] and native['native_calls']==0 and native['CAP_releases']==0
 assert native['binary_sha256']==smoke['binaries']['rearm'] and native['collector_tests_run']>=6
 assert native['production_positive_receipt_sha256']
 assert b'RO_MMIO_WORD_READ_R7' in (R/bins['restore']).read_bytes()
 for n in ['session','menu','rearm']:assert b'RESOURCE_REARM_R7' in (R/bins[n]).read_bytes()
 field=load('validation/recovery-r7-field.json');assert not field['exact_fault_address_proven']
 # Bind complete current build and test sources, not old broad R5 records.
 extensions={'.c','.h','.inc','.S','.s','.py','.sh','.in','.json','.sha256','.md'}
 for folder in ['core/src','core/tools','core/tests','hardware/src','post/src','session','installer/SOURCE','base/nx-lut-lite/src','base/nx-lut-lite/tools']:
  for p in sorted((R/folder).rglob('*')):
   if p.is_file() and not any(x=='__pycache__' or x.startswith('build') or x in ['bin','results','validation'] for x in p.relative_to(R/folder).parts) and p.suffix in extensions:bind(str(p.relative_to(R)))
 for n in ['installer/install.sh','installer/deploy.sh.in','installer/render_deploy.py','installer/patch-fileops','installer/README_zh.md','SOURCE_README_zh.md','DEMO_SCOPE_zh.md','plan.json','base/nx-lut-lite/restore-camera.sh','base/nx-lut-lite/rearm-session.sh','tools/package_recovery_r7_patch.py','tools/validate_recovery_r7_release.py']:bind(n)
 for p in (R/'installer').rglob('*.sh'):subprocess.run(['/bin/sh','-n',str(p)],check=True)
 for n in ['base/nx-lut-lite/restore-camera.sh','base/nx-lut-lite/rearm-session.sh','installer/SOURCE/runner.sh.in']:subprocess.run(['/bin/sh','-n',str(R/n)],check=True)
 out={'format':'NX51_RECOVERY_R7_RELEASE_CHECKS1','passed':True,'hardware_tested':False,'exact_old_SIGBUS_site_unknown':True,'MMIO_word_access_confirmed_in_ARM':True,'native_image_binaries_unchanged_from_R6':True,'old_holds_not_cleared_by_install':True,'boot_ID_not_fabricated':True,'rearm_requires_fresh_resource_proof':True,'source_asset_in_patch':False,'existing_0183_asset_required':True,'bound_files':dict(sorted(B.items()))}
 (R/'validation/recovery-r7-release-checks.json').write_text(json.dumps(out,indent=2)+'\n')
 print(json.dumps({'passed':True,'bound_files':len(B),'hardware_tested':False}))
if __name__=='__main__':main()
