"""Add-only curve delivery. Refuses replacement of every existing SD file."""
from pathlib import Path
from ctypes import wintypes
import argparse,ast,ctypes,hashlib,json,os,shutil,zipfile

def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def put(p,value):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def sd_identity(repo):
 k=ctypes.WinDLL('kernel32',use_last_error=True);k.SetErrorMode(0x8001)
 k.GetVolumeInformationW.argtypes=[wintypes.LPCWSTR,wintypes.LPWSTR,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD),ctypes.POINTER(wintypes.DWORD),ctypes.POINTER(wintypes.DWORD),wintypes.LPWSTR,wintypes.DWORD]
 k.GetVolumeNameForVolumeMountPointW.argtypes=[wintypes.LPCWSTR,wintypes.LPWSTR,wintypes.DWORD]
 k.GetDriveTypeW.argtypes=[wintypes.LPCWSTR]
 k.GetDiskFreeSpaceExW.argtypes=[wintypes.LPCWSTR,ctypes.POINTER(ctypes.c_ulonglong),ctypes.POINTER(ctypes.c_ulonglong),ctypes.POINTER(ctypes.c_ulonglong)]
 baseline=json.loads((repo/'baseline.json').read_text('utf8'))['volume'];found=[]
 for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ':
  root=c+':\\'
  if k.GetDriveTypeW(root)!=2:continue
  serial=wintypes.DWORD();fs=ctypes.create_unicode_buffer(256);guid=ctypes.create_unicode_buffer(256)
  if not k.GetVolumeInformationW(root,None,0,ctypes.byref(serial),None,None,fs,256) or serial.value!=4040321079:continue
  assert k.GetVolumeNameForVolumeMountPointW(root,guid,256);free=ctypes.c_ulonglong();total=ctypes.c_ulonglong();assert k.GetDiskFreeSpaceExW(root,ctypes.byref(free),ctypes.byref(total),None)
  v={'serial':serial.value,'GUID':guid.value,'filesystem':fs.value,'total':total.value}
  assert v==baseline,(v,baseline);found.append((Path(c+':/'),v,free.value))
 assert len(found)==1,found
 return found[0]

def metadata(sd,exclude):
 entries={};errors=[]
 def error(e):errors.append({'path':str(e.filename),'errno':e.errno,'winerror':getattr(e,'winerror',None)})
 for d,dirs,files in os.walk(sd,onerror=error):
  for n in files:
   p=Path(d)/n;rel=p.relative_to(sd).as_posix()
   if rel in exclude:continue
   try:
    s=p.stat();entries[rel]={'bytes':s.st_size,'mtime_ns':s.st_mtime_ns,'attributes':s.st_file_attributes}
   except OSError as e:error(e)
 return {'files':entries,'enumeration_errors':sorted(errors,key=lambda e:e['path'])}

def prepare(a):
 w=a.work;r=a.repo;source=r/'work/r10k-ep-flow';before=json.loads((w/'source-before.json').read_text('utf8'))
 for name,digest in before.items():assert sha(source/name)==digest,('Existing source changed',name)
 for p in (source/'tools/log_video').glob('*.py'):ast.parse(p.read_text('utf8'),filename=str(p))
 assert json.loads((w/'curve-validation.json').read_text('utf8'))['passed']
 assert json.loads((w/'reload-validation.json').read_text('utf8'))['passed']
 ready=w/'delivery';ready.mkdir(exist_ok=True);card=ready/'SD_CARD';card.mkdir(exist_ok=True)
 for p in (w/'deploy/looks').rglob('*'):
  if not p.is_file() or p.suffix=='.cube':continue
  dest=card/'looks'/p.relative_to(w/'deploy/looks');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
 docs=card/'NXDEMO51/LOG_RELOAD_V1';docs.mkdir(parents=True,exist_ok=True)
 for name in ['README_TEST_STEPS.txt','curve-validation.json','reload-validation.json','curve-and-error.png']:
  shutil.copyfile(w/name,docs/name)
 for p in (source/'tools/log_video').glob('*.py'):
  dest=ready/'SOURCE'/p.name;dest.parent.mkdir(exist_ok=True);shutil.copyfile(p,dest)
 off=ready/'OFFLINE_NOT_CAMERA';off.mkdir(exist_ok=True)
 for p in list(w.glob('*.cube'))+[w/'transfer-4097.csv']+list((w/'native-gamma-NOT-DEPLOYED').glob('*.bin'))+list((w/'deploy/looks').glob('*.cube')):
  shutil.copyfile(p,off/p.name)
 put(ready/'validation-summary.json',{'passed':True,'existing_source_files_unchanged':len(before),'R11g_runtime_changed':False,'core_libraries_changed':False,'RTOS_changed':False,'installer_triggers_added':False,'camera_hardware_tested':False})
 manifest={p.relative_to(ready).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)} for p in ready.rglob('*') if p.is_file() and p.name!='MANIFEST.json'}
 put(ready/'MANIFEST.json',manifest)
 archive=w/'NX500_Log_reload_V1.zip'
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=7) as z:
  for p in sorted(ready.rglob('*')):
   if p.is_file():z.write(p,p.relative_to(ready).as_posix())
 with zipfile.ZipFile(archive) as z:
  for name,row in manifest.items():assert hashlib.sha256(z.read(name)).hexdigest()==row['sha256']
 sd,volume,free=sd_identity(r);payload={p.relative_to(card).as_posix():{'bytes':p.stat().st_size,'sha256':sha(p)} for p in card.rglob('*') if p.is_file()}
 assert free>sum(row['bytes'] for row in payload.values())+1024*1024
 for name,row in payload.items():
  if (sd/name).exists():assert sha(sd/name)==row['sha256'],('Refuse overwrite',name)
 triggers={name:(sd/name).exists() for name in ['install.sh','info.tg','nx_cs.adj']};assert not any(triggers.values()),triggers
 # Narrow content hashes supplement all-file metadata; media files are never opened for writing.
 selected=[p for p in (sd/'nx-lut-demo/nx-lut-lite').rglob('*') if p.is_file()]
 selected += [p for p in (sd/'looks').rglob('*') if p.is_file() and p.relative_to(sd).as_posix() not in payload]
 selected += [sd/'NX500.bin']
 selected += list((sd/'NXDEMO51').glob('R11F*_ACTIVATION_*/snapshot-before.img'))
 hashes={}
 for i,p in enumerate(selected):
  if i%30==0:print('Protecting existing content',i,'/',len(selected),flush=True)
  hashes[p.relative_to(sd).as_posix()]=sha(p)
 assert hashes['NX500.bin']==json.loads((r/'baseline.json').read_text('utf8'))['firmware_sha256']
 original=metadata(sd,set(payload));put(w/'SD-before.json',{'volume':volume,'metadata':original,'hashes':hashes})
 result={'passed':True,'drive':str(sd),'volume':volume,'archive':str(archive),'archive_sha256':sha(archive),'archive_bytes':archive.stat().st_size,'payload':payload,'new_SD_files':sum(not (sd/name).exists()for name in payload),'existing_files_will_not_be_overwritten':True,'existing_file_metadata_count':len(original['files']),'existing_hashes':len(hashes),'enumeration_errors':original['enumeration_errors'],'triggers':triggers,'SD_written':False}
 put(w/'delivery-preflight.json',result);print(json.dumps({k:v for k,v in result.items() if k not in ['payload','enumeration_errors']},ensure_ascii=False))

def deploy(a):
 w=a.work;pre=json.loads((w/'delivery-preflight.json').read_text('utf8'));assert pre['passed'];sd,volume,free=sd_identity(a.repo);assert volume==pre['volume']
 before=json.loads((w/'SD-before.json').read_text('utf8'));payload=pre['payload'];assert metadata(sd,set(payload))==before['metadata'],'SD changed since preflight'
 assert sha(Path(pre['archive']))==pre['archive_sha256']
 card=w/'delivery/SD_CARD';created=[];skipped=[]
 for name,row in payload.items():
  src=card/name;assert sha(src)==row['sha256'];dest=sd/name
  if dest.exists():assert sha(dest)==row['sha256'];skipped.append(name);continue
  dest.parent.mkdir(parents=True,exist_ok=True)
  # Exclusive creation prevents accidental replacement even on a retry.
  with dest.open('xb') as f:
   with src.open('rb') as g:shutil.copyfileobj(g,f)
   f.flush();os.fsync(f.fileno())
  assert sha(dest)==row['sha256'];created.append(name)
 after=metadata(sd,set(payload));assert after==before['metadata'],'Existing SD file metadata or known errors changed'
 for i,(name,digest) in enumerate(before['hashes'].items()):
  if i%30==0:print('Verifying preserved content',i,'/',len(before['hashes']),flush=True)
  assert sha(sd/name)==digest,('Protected content changed',name)
 for name,row in payload.items():assert sha(sd/name)==row['sha256']
 assert sd_identity(a.repo)[1]==volume
 result={'passed':True,'drive':str(sd),'volume':volume,'created':created,'skipped_identical':skipped,'payload_files_read_back':len(payload),'existing_file_metadata_unchanged':len(after['files']),'existing_content_hashes_unchanged':len(before['hashes']),'known_enumeration_errors_unchanged':len(after['enumeration_errors']),'runtime_changed':False,'installer_triggers_added':False,'SD_written':True,'camera_hardware_tested':False,'archive':pre['archive'],'archive_sha256':pre['archive_sha256']}
 put(w/'SD-deployment.json',result);print(json.dumps(result,ensure_ascii=False,indent=2))

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--mode',choices=['prepare','deploy'],required=True);ap.add_argument('--repo',type=Path,required=True);ap.add_argument('--work',type=Path,required=True);a=ap.parse_args();(prepare if a.mode=='prepare' else deploy)(a)
if __name__=='__main__':main()
