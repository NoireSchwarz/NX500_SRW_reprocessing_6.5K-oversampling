#!/usr/bin/env python3
"""Package the existing-install guarded recovery/rearm patch, without redistributing SRW assets."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,shutil,zipfile,subprocess
R=Path(__file__).resolve().parents[1];W=R.parent
RELEASE='NX-LUT-Lite-0.4.0-demo51-recovery-r7-patch'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def cp(a,b):
 assert a.is_file() and not a.is_symlink(),a
 b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(a,b)
def source_tree(a,b):
 allowed={'.c','.h','.cpp','.hpp','.py','.sh','.in','.md','.txt','.json','.S','.s','.inc','.def','.sha256'}
 for p in sorted(a.rglob('*')):
  rel=p.relative_to(a)
  if any(n in ('__pycache__','build','results','validation','bin','.git') or n.startswith('build-') for n in rel.parts):continue
  if p.is_file() and (p.suffix in allowed or p.name.startswith(('LICENSE','NOTICE'))):cp(p,b/rel)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();out=a.output.resolve();assert not out.exists()
 gate=json.loads((R/'validation/recovery-r7-release-checks.json').read_text());assert gate['passed'] and not gate['hardware_tested']
 for n,h in gate['bound_files'].items():assert sha(R/n)==h,n
 spec=importlib.util.spec_from_file_location('renderer',R/'installer/render_deploy.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 front=R/'core/bin/nxl-demo-front.arm';post=R/'post/bin/nxl-demo-post.arm';menu=R/'base/nx-lut-lite/bin/nxl-menu'
 build=m.render(front,sha(front),post,sha(post),menu,sha(menu),out)
 cp(R/'installer/README_zh.md',out/'README_zh.md');cp(R/'DEMO_SCOPE_zh.md',out/'DEMO_SCOPE_zh.md')
 src=out/'SOURCE/demo-p52-recovery-r7';cp(R/'SOURCE_README_zh.md',src/'README_zh.md')
 (out/'SOURCE/README_zh.md').write_text('主源码在 demo-p52-recovery-r7。另两个目录仅含原发布 README，作为升级归属测试的精确字节夹具，不是额外安装包。\n')
 for old in ['demo-p52-timing-r6','demo-p52-restore-r5']:cp(W/old/'installer/README_zh.md',out/'SOURCE'/old/'installer/README_zh.md')
 for name in ['core/src','core/tools','core/tests','hardware/src','hardware/tools','hardware/tests','post/src','post/tools','session','installer/SOURCE','base/nx-lut-lite/src','base/nx-lut-lite/tools','base/nx-lut-lite/vendor','base/nx-lut-lite/data']:
  source_tree(R/name,src/name)
 for n in ['deploy.sh.in','install.sh','uninstall.sh','render_deploy.py','README_zh.md']:cp(R/'installer'/n,src/'installer'/n)
 for n in ['package_recovery_r7_patch.py','validate_recovery_r7_release.py']:cp(R/'tools'/n,src/'tools'/n)
 for n in ['plan.json']:cp(R/n,src/n)
 for n in ['restore-camera.sh','rearm-session.sh']:cp(R/'base/nx-lut-lite'/n,src/'base/nx-lut-lite'/n)
 for n in ['test_post.py']:cp(R/'post'/n,src/'post'/n)
 source_tree(W/'srw-p43/patch/SOURCE/upstream',src/'upstream')
 for n in ['LICENSE','LICENSE_MUSL.txt','LICENSE_GCC_RUNTIME_EXCEPTION.txt','LICENSE_GPL3.txt','LICENSE_LIBRAW_LGPL.txt','LICENSE_ZIG_RUNTIME.txt']:
  p=W/'render-p48r1/release/package'/n
  if not p.exists():p=W/n
  cp(p,out/n)
 for name in gate['bound_files']:
  p=R/name
  if ('validation' in Path(name).parts or 'results' in Path(name).parts or name.startswith('arm-tests/')) and p.suffix in ('.json','.txt','.py'):cp(p,out/'EVIDENCE'/name)
 cp(R/'validation/recovery-r7-release-checks.json',out/'EVIDENCE/validation/recovery-r7-release-checks.json')
 cp(W/'demo51-field-success/JOB_1793869199_415_10dc3936/front_2/front/case_00/p48_idle_handoff.bin',out/'EVIDENCE/field-front2-idle-handoff.bin')
 (out/'EVIDENCE/SCOPE.txt').write_text('R7 guarded recovery and current-resource rearm validation are computer-only. Native image binaries are frozen R6. No actual camera recovery or resource-rearm success is claimed; no firmware libraries or SRW assets included.\n')
 assert not (out/'nx-lut-demo/assets').exists()
 assert not any(p.is_symlink() or p.suffix=='.so' for p in out.rglob('*'))
 build.update(format='NX51_RECOVERY_R7_PATCH1',releasecode=RELEASE,existing_source_asset_required=True,source_asset_copied=False,hardware_tested=False,recovery_camera_test_pending=True,prior_camera_JPEG422_confirmed=True,camera_speedup_measured=False,full_192MiB_proof_retained=True,validation=gate)
 (out/'BUILD.json').write_text(json.dumps(build,indent=2)+'\n')
 for p in out.rglob('*.sh'):subprocess.run(['/bin/sh','-n',str(p)],check=True)
 (out/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.relative_to(out).as_posix()+'\n' for p in sorted(out.rglob('*')) if p.is_file()))
 archive=out.parent/(RELEASE+'.zip');assert not archive.exists()
 with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=False) as z:
  for p in sorted(out.rglob('*')):
   if p.is_file():z.write(p,p.relative_to(out).as_posix())
 with zipfile.ZipFile(archive) as z:
  names=z.namelist();assert len(names)==len(set(n.casefold() for n in names))
  listed={}
  for line in z.read('SHA256SUMS').decode().splitlines():
   h,n=line.split('  ',1);assert hashlib.sha256(z.read(n)).hexdigest()==h,n;listed[n]=h
  assert set(listed)==set(names)-{'SHA256SUMS'}
  assert all(i.compress_type==zipfile.ZIP_DEFLATED and i.extract_version<45 and not i.flag_bits&1 for i in z.infolist())
 result={'passed':True,'file':archive.name,'bytes':archive.stat().st_size,'sha256':sha(archive),'entries':len(names),'all_file_hashes_verified':len(listed),'Windows_DEFLATE_no_ZIP64':True,'hardware_tested':False}
 (out.parent/'ZIP_VALIDATION.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
