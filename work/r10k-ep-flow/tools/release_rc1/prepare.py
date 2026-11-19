"""Capture an isolated formal-release candidate without touching SD/media."""
from context import *
import re,shutil,zipfile
assert not W.exists()
W.mkdir()
for n in ['logs','bin','r8f-reference','inputs']:(W/n).mkdir()
assert sha(B/'nx-lut-demo/module.manifest')=='8d05422000d5113dcd1b4178792d68f5a649df135f65e60fdb887ab2edc0a8ad'
shutil.copytree(B,S)
for n in ['core/src','core/tools','core/tests','common','base/nx-lut-lite/src','base/nx-lut-lite/tools',
          'base/nx-lut-lite/vendor','base/nx-lut-lite/data','tools/log_video']:
 if (SRC/n).exists():shutil.copytree(SRC/n,T/n,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for p in (SRC/'base/nx-lut-lite').glob('*.sh'):shutil.copyfile(p,T/'base/nx-lut-lite'/p.name)
for n in ['core/bin','core/results','base/nx-lut-lite/bin']:(T/n).mkdir(parents=True,exist_ok=True)
before={p.relative_to(T).as_posix():sha(p)for p in T.rglob('*')if p.is_file()}
put(W/'source-before.json',before)
git=['git','-c','safe.directory='+R.as_posix(),'-C',str(R)]
put(W/'git-before.json',{k:subprocess.check_output(git+a,text=True,encoding='utf8')for k,a in [('status',['status','--porcelain=v1','-uall']),('head',['rev-parse','HEAD'])]})
with zipfile.ZipFile(OLD) as z:
 assert z.testzip() is None
 # Extract reference source/tests for review only. Never execute old patchers.
 rows=[]
 for n in z.namelist():
  if n.endswith('/'):continue
  if n.startswith('SOURCE/demo-p52-recovery-r7/base/nx-lut-lite/'):
   rel=n.split('SOURCE/demo-p52-recovery-r7/base/nx-lut-lite/',1)[1]
   p=W/'r8f-reference/base/nx-lut-lite'/rel
  elif n.startswith('SOURCE/packaging/validate_r8f_'):
   p=W/'r8f-reference/tests'/Path(n).name
  elif n.startswith('SOURCE/metol-reference/'):
   p=W/'r8f-reference/metol'/n.split('SOURCE/metol-reference/',1)[1]
  elif n.startswith('nx-lut-demo/nx-lut-lite/bin/')and any(x in n for x in ['lutblend','sharp']):
   p=W/'r8f-reference/bin'/Path(n).name
  elif '/looks/'in n or n.startswith('looks/'):
   rows.append(dict(path=n,bytes=z.getinfo(n).file_size));continue
  else:continue
  assert W.resolve()in p.resolve().parents
  p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(n))
 put(W/'r8f-archive-audit.json',dict(archive=str(OLD),sha256=sha(OLD),entries=len(z.namelist()),look_files=rows))
inputs=[]
for p in Path(r'D:\Download').iterdir():
 if p.is_file()and (p.name in ['（必看）更新区别及命名含义.txt','新命名规则与玩法.txt']or(p.suffix=='.cube'and any(s in p.name for s in ['g200','5207']))):
  shutil.copyfile(p,W/'inputs'/p.name);inputs.append(dict(name=p.name,source=str(p),sha256=sha(p),bytes=p.stat().st_size))
put(W/'input-provenance.json',inputs)
put(W/'preparation.json',dict(passed=True,baseline='R11q',baseline_manifest=sha(B/'nx-lut-demo/module.manifest'),source_files=len(before),
 SD_written=False,firmware_modified=False,VLog_hardware_acceptance_pending=True,user_instructions_override_old_package_readme=True))
print('PASS isolated RC preparation, source capture and exact original LUT copies; no SD touched',flush=True)
