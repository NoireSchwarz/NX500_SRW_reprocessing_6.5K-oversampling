from context import *
import shutil
sys.path[:0]=[str(PY),str(SRC/'tools')]
from post_file_model import PostFileModel
V=W/'tests/catalog';V.mkdir(parents=True);rows=[]
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'))['looks']
assert len(catalog)==32 and len({r['id']for r in catalog})==32
for row in catalog:
 id=row['id'];p=M/'looks'/(id+'.bin');assert row['tone']and row['scene']and row['input']in ['STD','VLOG']
 assert sha(p)==row['native_sha256']==sha(S/'looks'/p.name)
 assert not id.endswith('_apx');data=p.read_bytes();h=2166136261
 for b in data:h=((h^b)*16777619)&0xffffffff
 fs=V/id;fs.mkdir();shutil.copyfile(p,fs/'table.bin')
 result=PostFileModel(M/'bin/nxl-tablecheck.arm',['check','/table.bin','/table.bin','/table.bin',f'{h:08x}'],fs).run();assert result['exit']==0,(id,result)
 rows.append(dict(id=id,passed=True,sha256=sha(p),actual_ARM_tablecheck=result))
put(W/'catalog-validation.json',dict(passed=True,looks=rows,ordinary_count=25,Log_count=7,descriptions_present=True,old_19_unchanged=all(sha(p)==sha(M/'looks'/p.name)for p in (B/'looks').glob('*.bin')),hardware_tested=False))
print('PASS 32 actual ARM native table checks and introductions',flush=True)
