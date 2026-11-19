"""Hash-guarded publication over the user's existing uncommitted work."""
from context import *
import shutil
before=json.loads((W/'source-before.json').read_text('utf8'));candidates={}
for folder in ['base/nx-lut-lite/src','base/nx-lut-lite/data','core/src','common','session/src']:
 for p in(T/folder).rglob('*'):
  if p.is_file():candidates[p.relative_to(T).as_posix()]=p
for p in M.glob('*.sh'):candidates['base/nx-lut-lite/'+p.name]=p
for p in(M/'bin').iterdir():
 if p.is_file():candidates['base/nx-lut-lite/bin/'+p.name]=p
for p in(M/'looks').glob('*.bin'):candidates['base/nx-lut-lite/looks/'+p.name]=p
candidates['base/nx-lut-lite/README_zh.md']=M/'README_zh.md'
candidates['session/bin/nxl-session.arm']=M/'bin/nxl-session.arm'
rows=[]
for rel,src in sorted(candidates.items()):
 dest=SRC/rel;assert SRC.resolve()in dest.resolve().parents
 if dest.exists()and sha(dest)==sha(src):continue
 old=sha(dest)if dest.exists()else None;expected=before.get(rel)
 if old is not None:
  if expected is None:
   fallback=B/'nx-lut-demo/nx-lut-lite'/rel.removeprefix('base/nx-lut-lite/')
   if rel=='session/bin/nxl-session.arm':fallback=B/'nx-lut-demo/nx-lut-lite/bin/nxl-session.arm'
   if not(fallback.is_file()and sha(fallback)==old):
    leaf=rel.removeprefix('base/nx-lut-lite/')
    if rel=='session/bin/nxl-session.arm':leaf='bin/nxl-session.arm'
    known=any((old+' '+leaf+'\n')in p.read_text('utf8')for p in(S/'nx-lut-demo/ownership').glob('*.manifest'))
    assert known and dest.stat().st_mtime<=(W/'preparation.json').stat().st_mtime,('unknown or newly changed build artifact',rel)
  else:assert old==expected,('source changed since takeover',rel)
 rows.append(dict(path=rel,before=old,after=sha(src)))
put(W/'source-publication-plan.json',dict(passed=True,repository=str(R),changes=rows,preexisting_user_changes_preserved=True))
if '--apply'in sys.argv:
 assert json.loads((W/'final-package-validation.json').read_text('utf8'))['passed']
 backup=W/'repository-before';backup.mkdir(exist_ok=True)
 for row in rows:
  rel=row['path'];dest=SRC/rel;src=candidates[rel]
  assert(sha(dest)if dest.exists()else None)==row['before']
  if dest.exists():q=backup/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dest,q);assert sha(q)==row['before']
  dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest);assert sha(dest)==row['after']
 for name in ['RC1_TEST_STEPS_zh.txt','RC1_VALIDATION_REPORT_zh.txt','RC1_LUT_CATALOG_zh.txt']:
  dest=R/'docs'/name;assert not dest.exists();shutil.copyfile(S/name,dest)
 target=SRC/'tools/release_rc1';assert not target.exists();shutil.copytree(A,target,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
 shutil.copyfile(W/'runtime_identity.py',target/'runtime_identity.py')
 put(W/'source-publication.json',dict(passed=True,changes=rows,backup=str(backup),no_git_reset_commit_or_remote_push=True))
 print('PASS published',len(rows),'verified files over preserved local work',flush=True)
else:print('PASS source hash preflight:',len(rows),'file changes; no repository writes',flush=True)
