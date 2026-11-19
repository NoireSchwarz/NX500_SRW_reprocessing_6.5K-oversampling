from context import *
import shutil
assert not W.exists();W.mkdir();(W/'logs').mkdir();shutil.copytree(B,S)
for n in ['base/nx-lut-lite/src','base/nx-lut-lite/vendor','base/nx-lut-lite/tools','base/nx-lut-lite/data','core/src','core/tools','core/tests','common']:
 shutil.copytree(SRC/n,T/n,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for p in (SRC/'base/nx-lut-lite').glob('*.sh'):shutil.copyfile(p,T/'base/nx-lut-lite'/p.name)
put(W/'source-before.json',{p.relative_to(T).as_posix():sha(p)for p in T.rglob('*')if p.is_file()})
put(W/'git-before.json',dict(head=subprocess.check_output(['git','-C',str(R),'rev-parse','HEAD'],text=True),status=subprocess.check_output(['git','-C',str(R),'status','--porcelain=v1','-uall'],text=True,encoding='utf8')))
shutil.copytree(A/'upstream',W/'research/Metol')
put(W/'preparation.json',dict(passed=True,source='RC3 verified by user',Metol_commit='b10ec8413ce5f9a03079288cb9880e4d0fac983f',no_repository_or_SD_changes=True))
print('PASS isolated RC4 baseline and pinned upstream evidence',flush=True)
