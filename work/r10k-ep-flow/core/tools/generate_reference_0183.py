"""Recreate only this user's pinned SAM0183 fixture; never overwrite original."""
from pathlib import Path
import argparse,hashlib,json,struct
import numpy as np,rawpy
p=argparse.ArgumentParser();p.add_argument('original',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
reference=json.loads((Path(__file__).resolve().parents[1]/'evidence/SAM_0183-reference.json').read_text())
data=bytearray(a.original.read_bytes());sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(data)==reference['original_sha256'] and len(data)==reference['original_bytes']
strip=bytes(data[reference['compressed_strip_offset']:]);assert sha(strip)==reference['compressed_strip_sha256']
for r in reference['zeroed_embedded_jpegs']:
 off,n=r['offset'],r['bytes'];assert data[off:off+2]==b'\xff\xd8' and data[off+n-2:off+n]==b'\xff\xd9';data[off:off+n]=bytes(n)
assert sha(data)==reference['fixture_sha256'] and data[reference['compressed_strip_offset']:]==strip
a.out.mkdir(parents=True,exist_ok=True);fixture=a.out/'p39b_input.SRW';assert fixture.resolve()!=a.original.resolve();fixture.write_bytes(data)
with rawpy.imread(str(fixture)) as raw:
 b=raw.raw_image.astype('<u2',copy=True);assert b.shape==(4336,6496) and raw.raw_pattern.tolist()==[[1,0],[2,3]] and sha(b.tobytes())==reference['bayer_sha256']
 rows=struct.pack('<4I',0x3432524e,1,6496,4336)+b''.join(hashlib.sha256(row.tobytes()).digest() for row in b)
 stage=np.full((4384,6608),128,dtype='<u2');stage[32:4368,88:6584]=b
 assert sha(rows)==reference['reference_rows_sha256'] and sha(stage.tobytes())==reference['staged_sha256']
 (a.out/'p39b_expected_rows.bin').write_bytes(rows)
print('Verified SAM0183 fixture, untouched raw strip, all4336 row references and staged layout; no camera')
