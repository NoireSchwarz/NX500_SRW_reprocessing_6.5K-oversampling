from context import *
import shutil
before=json.loads((W/'source-before.json').read_text('utf8'));candidates={}
for folder in ['base/nx-lut-lite/src','base/nx-lut-lite/data','base/nx-lut-lite/tools','core/src']:
 for p in(T/folder).rglob('*'):
  if p.is_file():candidates[p.relative_to(T).as_posix()]=p
for p in [*M.glob('*.sh'),*(M/'bin').iterdir(),M/'README_zh.md']:
 if p.is_file():candidates['base/nx-lut-lite/'+p.relative_to(M).as_posix()]=p
rows=[]
for rel,src in sorted(candidates.items()):
 dest=SRC/rel;assert SRC.resolve()in dest.resolve().parents;old=sha(dest)if dest.exists()else None
 if old==sha(src):continue
 expected=before.get(rel)
 if old is not None:
  if expected is None:
   fallback=B/'nx-lut-demo/nx-lut-lite'/rel.removeprefix('base/nx-lut-lite/');assert fallback.is_file()and sha(fallback)==old,('unrecognized or concurrent change',rel)
  else:assert expected==old,('source changed since inspection',rel)
 rows.append(dict(path=rel,before=old,after=sha(src)))
put(W/'source-publication-plan.json',dict(passed=True,repository=str(R),changes=rows,preexisting_user_changes_preserved=True))
if '--apply'in sys.argv:
 assert json.loads((W/'final-package-validation.json').read_text('utf8'))['passed'];backup=W/'repository-before';backup.mkdir(exist_ok=True)
 for row in rows:
  rel=row['path'];dest=SRC/rel;assert(sha(dest)if dest.exists()else None)==row['before']
  if dest.exists():q=backup/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dest,q);assert sha(q)==row['before']
  dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(candidates[rel],dest);assert sha(dest)==row['after']
 for p in S.glob('RC4_*.txt'):
  dest=R/'docs'/p.name;assert not dest.exists();shutil.copyfile(p,dest)
 target=SRC/'tools/release_rc4';assert not target.exists();shutil.copytree(A,target,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
 shutil.copyfile(W/'runtime_identity.py',target/'runtime_identity.py')
 put(W/'source-publication.json',dict(passed=True,changes=rows,backup=str(backup),no_git_reset_commit_or_remote_push=True))
 print('PASS published',len(rows),'verified RC4 differences; previous files backed up')
else:print('PASS source publication preflight',len(rows),'files')
