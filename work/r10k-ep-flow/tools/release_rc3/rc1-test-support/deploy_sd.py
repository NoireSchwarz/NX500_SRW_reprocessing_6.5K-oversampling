"""Identity-pinned, non-mirroring SD update. Never targets media/history/backup trees."""
from context import *
import ctypes,datetime,shutil,uuid
EXPECTED=4040321079
def volumes():
 k=ctypes.WinDLL('kernel32',use_last_error=True);f=k.GetVolumeInformationW
 f.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32),ctypes.c_wchar_p,ctypes.c_uint32];f.restype=ctypes.c_int
 rows=[]
 for letter in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
  root=letter+':\\';label=ctypes.create_unicode_buffer(261);fs=ctypes.create_unicode_buffer(64);serial=ctypes.c_uint32();length=ctypes.c_uint32();flags=ctypes.c_uint32()
  if f(root,label,len(label),ctypes.byref(serial),ctypes.byref(length),ctypes.byref(flags),fs,len(fs))and serial.value==EXPECTED:
   rows.append(dict(root=root,serial=serial.value,filesystem=fs.value,label=label.value))
 return rows
vol=volumes();assert len(vol)==1,vol;vol=vol[0];assert vol['filesystem'].lower()=='exfat';SD=Path(vol['root'])
assert(SD/'NX500.bin').is_file()and(SD/'NX500.bin').stat().st_size==352121435
assert(SD/'nx-lut-demo/module.manifest').is_file()
P=W/'release-payload';package=json.loads((W/'final-package-validation.json').read_text('utf8'));assert package['passed']and Path(package['payload'])==P
assert sha(package['archive'])==package['sha256']
expected={}
for line in(P/'SHA256SUMS').read_text('utf8').splitlines():
 h,rel=line.split('  ',1);assert len(h)==64 and rel not in expected;expected[rel]=h
for rel,h in expected.items():assert sha(P/rel)==h,('payload changed',rel)
controls={'install.sh','nx_cs.adj','info.tg'}
rootfiles={'BUILD.json','README_zh.md','SHA256SUMS','RC1_TEST_STEPS_zh.txt','RC1_VALIDATION_REPORT_zh.txt','RC1_LUT_CATALOG_zh.txt'}
protected=['DCIM','ALBM','NX500.bin','NXDEMO51','nx-lut-activation','nx-lut-startup-check','nx-lut-lite-ui','ROLLBACK','SYSTEM','System Volume Information']
def allowed(rel):
 parts=Path(rel).parts
 if len(parts)==1:return rel in rootfiles or rel in controls
 if parts[0]in ['nx-lut-demo','looks','PC_LUT_SOURCES','PC_VLOG_LUTs']:return True
 if parts[:2]in [('SOURCE','FORMAL_0_4_RC1'),('EVIDENCE','RC1')]:return True
 return False
selected=[];omitted=[]
for p in sorted(P.rglob('*')):
 if not p.is_file():continue
 rel=p.relative_to(P).as_posix()
 if allowed(rel):
  assert Path(rel).parts[0]not in protected and '..'not in Path(rel).parts
  selected.append(rel)
 else:omitted.append(rel)
assert controls.issubset(selected)and len(selected)>100
def safe_target(rel):
 q=SD/Path(rel);assert SD.resolve()in q.resolve().parents
 current=SD
 for part in Path(rel).parts:
  current=current/part;assert not current.is_symlink(),current
 return q
def snapshot():
 out={}
 for name in protected:
  p=SD/name
  if p.exists():s=p.stat();out[name]=dict(size=s.st_size,mtime_ns=s.st_mtime_ns,is_dir=p.is_dir())
 return out
preserved=snapshot();free=shutil.disk_usage(SD).free;assert free>2*sum((P/r).stat().st_size for r in selected)
plan=[]
for rel in selected:
 q=safe_target(rel);assert not q.exists()or q.is_file(),q
 want=sha(P/rel);old=sha(q)if q.exists()else None
 plan.append(dict(path=rel,old=old,new=want,write=old!=want))
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ');backup=W/('SD-before-RC1-'+stamp)
put(W/'SD-deployment-plan.json',dict(passed=True,volume=vol,selected=len(selected),changes=sum(x['write']for x in plan),
 backup=str(backup),protected=protected,omitted=len(omitted),archive_sha256=package['sha256']))
if '--apply'not in sys.argv:
 print('PASS SD preflight',vol,'changes',sum(x['write']for x in plan),'protected trees untouched',flush=True);sys.exit(0)
backup.mkdir(exist_ok=False)
# Save every overwritten file and all existing entry triggers before touching
# the payload. Copies are read back and pinned; no historical log is opened.
for row in plan:
 if row['old']is not None and(row['write']or row['path']in controls):
  q=safe_target(row['path']);assert sha(q)==row['old']
  b=backup/row['path'];b.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(q,b);assert sha(b)==row['old']
put(backup/'backup-manifest.json',dict(volume=vol,files=[x for x in plan if x['old']is not None and(x['write']or x['path']in controls)]))
assert volumes()==[vol]
# Only three named installer triggers are retired, after their verified
# backup. A disconnected/partial copy cannot launch an incomplete package.
for rel in controls:
 q=safe_target(rel)
 if q.exists():q.unlink()
written=[];same=[]
def transfer(row):
 rel=row['path'];src=P/rel;q=safe_target(rel);q.parent.mkdir(parents=True,exist_ok=True)
 if q.exists()and sha(q)==row['new']:same.append(rel);return
 if q.exists():assert sha(q)==row['old'],('SD file changed during update',rel)
 temp=q.with_name(q.name+'.rc1tmp-'+uuid.uuid4().hex[:8]);assert not temp.exists()
 with src.open('rb')as a,temp.open('xb')as b:
  shutil.copyfileobj(a,b,1024*1024);b.flush();os.fsync(b.fileno())
 assert sha(temp)==row['new'];os.replace(temp,q);assert sha(q)==row['new'];written.append(rel)
for i,row in enumerate(plan):
 if row['path']not in controls:transfer(row)
 if i%200==0:print('SD copy',i,'/',len(plan),flush=True)
for rel in ['install.sh','nx_cs.adj','info.tg']:transfer(next(x for x in plan if x['path']==rel))
assert volumes()==[vol]
for row in plan:assert sha(safe_target(row['path']))==row['new'],row['path']
assert snapshot()==preserved,('protected root metadata changed',snapshot(),preserved)
record=dict(passed=True,release='0.4.0-RC1',volume=vol,archive_sha256=package['sha256'],module_manifest_sha256=sha(SD/'nx-lut-demo/module.manifest'),
 verified_files=len(plan),written_files=len(written),already_identical_files=len(same),backup=str(backup),protected=protected,
 protected_root_metadata_unchanged=True,history_and_user_backups_not_targeted=True,omitted_inherited_docs_sources_evidence=len(omitted),
 trigger_hashes={x:sha(SD/x)for x in controls},installer_triggers_written_last=True,
 filesystem_repairs_or_formatting=False,SD_deployment_only_camera_install_not_yet_observed=True,hardware_tested=False)
put(W/'RC1_SD_deployment.json',record);put(W.parent/'outputs/RC1_SD_deployment.json',record)
put(SD/'RC1_SD_DEPLOYMENT.json',record)
print('PASS SD deployment and complete selected readback:',len(plan),'files;',len(written),'written;',len(same),'identical; protected data untouched',flush=True)
