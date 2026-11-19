"""Publish only verified RC2 differences without overwriting concurrent edits."""
from context import *
import shutil
before=json.loads((W/'source-before.json').read_text('utf8'));candidates={}
for folder in ['base/nx-lut-lite/src','base/nx-lut-lite/data']:
 for p in (T/folder).rglob('*'):
  if p.is_file():candidates[p.relative_to(T).as_posix()]=p
for p in [*M.glob('*.sh'),*(M/'bin').iterdir(),M/'README_zh.md']:
 if p.is_file():candidates['base/nx-lut-lite/'+p.relative_to(M).as_posix()]=p
rows=[]
for rel,src in sorted(candidates.items()):
 dest=SRC/rel;assert SRC.resolve()in dest.resolve().parents
 old=sha(dest)if dest.exists()else None
 if old==sha(src):continue
 expected=before.get(rel)
 if old is not None:
  if expected is None:
   fallback=B/'nx-lut-demo/nx-lut-lite'/rel.removeprefix('base/nx-lut-lite/')
   assert fallback.is_file()and sha(fallback)==old,('unrecognized or concurrent change',rel)
  else:assert expected==old,('source changed since inspection',rel)
 rows.append(dict(path=rel,before=old,after=sha(src)))
put(W/'source-publication-plan.json',dict(passed=True,repository=str(R),changes=rows,preexisting_user_changes_preserved=True))
if '--apply'in sys.argv:
 assert json.loads((W/'final-package-validation.json').read_text('utf8'))['passed']
 backup=W/'repository-before';backup.mkdir(exist_ok=True)
 for row in rows:
  rel=row['path'];dest=SRC/rel;assert(sha(dest)if dest.exists()else None)==row['before']
  if dest.exists():q=backup/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dest,q);assert sha(q)==row['before']
  dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(candidates[rel],dest);assert sha(dest)==row['after']
 for name in ['RC2_TEST_STEPS_zh.txt','RC2_VALIDATION_REPORT_zh.txt','RC2_VIDEO_RELOAD_RESEARCH_zh.txt']:
  dest=R/'docs'/name;assert not dest.exists();shutil.copyfile(S/name,dest)
 target=SRC/'tools/release_rc2';assert not target.exists();shutil.copytree(A,target,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
 support=target/'rc1-test-support';support.mkdir()
 for name in ['render_ui.py','test_installer.py','deploy_sd.py']:shutil.copyfile(A.parent/'formal-rc1-authoring'/name,support/name)
 shutil.copyfile(W/'runtime_identity.py',target/'runtime_identity.py')
 put(W/'source-publication.json',dict(passed=True,changes=rows,backup=str(backup),no_git_reset_commit_or_remote_push=True))
 print('PASS published',len(rows),'verified RC2 changes; old files backed up',flush=True)
else:print('PASS source publication preflight:',len(rows),'changes',flush=True)
