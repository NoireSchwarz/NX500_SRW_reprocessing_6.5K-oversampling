#!/usr/bin/env python3
"""Targeted host-only RO cleanup tracing; never mount or access camera paths."""
from pathlib import Path
import importlib.util,json,os,subprocess,time
R=Path(__file__).resolve().parents[1];spec=importlib.util.spec_from_file_location('demo49_deploy_model',R/'SOURCE/test_deploy.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
audit=R/'results/fs_events.so';subprocess.run(['gcc','-shared','-fPIC','-O2',str(R/'SOURCE/fs_events.c'),'-o',str(audit),'-ldl'],check=True)
m.Deploy.setUpClass();records=[]
try:
 for i in range(8):
  f=m.Deploy('test_original_ro_mount_restored');f.setUp();f.mode('ro');events=f.w/'fs-events.log';f.env.update(LD_PRELOAD=str(audit),NXL_FS_EVENTS=str(events));p=f.call();lock=f.w/'tmp/nx-lut-demo-setup.lock'
  def info():
   d={'exists':lock.exists()}
   if d['exists']:
    s=lock.lstat();d.update(inode=s.st_ino,entries=sorted(x.name for x in lock.iterdir()),mtime_ns=s.st_mtime_ns,ctime_ns=s.st_ctime_ns)
   return d
  now=info();time.sleep(.1);later=info();record={'iteration':i,'fixture':str(f.w),'returncode':p.returncode,'immediate':now,'after_100ms':later,'log':f.log(),'events':events.read_text() if events.exists() else ''};records.append(record)
  if p.returncode or now['exists'] or later['exists']:f.tmp._finalizer.detach();record['retained']=True
  else:f.tmp.cleanup()
finally:m.Deploy.tearDownClass()
(R/'results/ro-cleanup-replays.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps({'iterations':len(records),'unexpected_retained':sum(bool(r.get('retained')) for r in records),'results':'results/ro-cleanup-replays.json'}))
