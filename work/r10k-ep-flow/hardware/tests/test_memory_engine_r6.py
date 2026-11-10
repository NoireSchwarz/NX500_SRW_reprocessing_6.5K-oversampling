#!/usr/bin/env python3
"""Run current full-memory/JPEG engine contracts with actual captured input/JPEG."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
from PIL import Image
R=Path(__file__).resolve().parents[1];W=R.parents[1];F=W/'demo51-field-success/JOB_1793869199_415_10dc3936/jpeg/attempt_1/hardware'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 cases=[]
 with tempfile.TemporaryDirectory(prefix='r6-memory-engine-',dir=W) as td:
  t=Path(td);j420=t/'pc-fixture420.jpg'
  with Image.open(F/'native-actual.jpg') as im:im.save(j420,quality=90,subsampling=2)
  for name,count in [('nv16_memory',12),('jpeg422_engine',4),('allocator_stability',5)]:
   src=R/f'tests/test_{name}.c';exe=t/name
   subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation',str(src),'-o',str(exe)],check=True)
   args=[str(exe)] if name=='allocator_stability' else [str(exe),str(F),str(F/'native-actual.jpg'),str(j420)]
   p=subprocess.run(args,check=True,capture_output=True,text=True,cwd=t)
   cases.append({'name':name,'tests_run':count,'test_source_sha256':sha(src),'stdout':p.stdout.strip()})
 out={'format':'NX51_R6_MEMORY_ENGINE_MODELS1','passed':True,'hardware_tested':False,'tests_run':sum(x['tests_run'] for x in cases),'case_count_unit':'assertion groups in three compiled model programs','source_sha256':{str(p.relative_to(R.parent)):sha(p) for p in [R/'src/full_memory.h',R/'src/r6_timing.h',R/'src/jpeg_engine.h',R/'src/jpeg_core.h',R/'src/bitb_job.h',Path(__file__)]},'source_input_sha256':sha(F/'input422.bin'),'JPEG422_sha256':sha(F/'native-actual.jpg'),'cases':cases,'limits':['Physical memory, DMA, driver and IRQ are host models; real captured input and output bytes are used.','The deliberately mismatched JPEG420 fixture is generated offline on PC, not by the camera.']}
 (R/'validation/r6-memory-engine-tests.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
if __name__=='__main__':main()
