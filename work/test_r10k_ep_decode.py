from pathlib import Path
import sys,struct,json,copy
W=Path(__file__).resolve().parent;R=W/'r10k-ep-flow';sys.path.insert(0,str(R/'tools'))
from analyze_video_counters import decode,fnv
captures=[p for p in(R/'validation/current/path-reader').rglob('counts.bin')if p.stat().st_size==16384+275*1280]
assert captures
source=max(captures,key=lambda p:p.stat().st_mtime_ns);raw=source.read_bytes();summary,_=decode(source)
assert summary['EP_readback_comparison']['samples']==275
V=R/'validation/current/ep-decoder';V.mkdir(exist_ok=True)
cases=[]
def corrupted(name,index,value,reason):
 b=bytearray(raw);at=16384+287*4;struct.pack_into('<I',b,at+4*index,value)
 struct.pack_into('<I',b,16384+1276,fnv(b[16384:16384+1276])^0x91d6037b)
 p=V/(name+'.bin');p.write_bytes(b)
 try:decode(p)
 except ValueError as e:assert reason in str(e),(name,str(e));cases.append(dict(case=name,rejected=str(e)))
 else:raise AssertionError('Malformed EP structure accepted: '+name)
flags=struct.unpack_from('<I',raw,16384+288*4)[0]
corrupted('bad-magic',0,0,'Invalid EP')
corrupted('changed-unavailable-group',1,(flags&~1)|16|4096,'Changed unavailable')
corrupted('group-detail-disagree',1,flags|16,'change mask disagreement')
corrupted('coherent-header-disagree',4,struct.unpack_from('<I',raw,16384+291*4)[0]^1,'coherence mismatch')
# Native preview zero configuration is represented as real zero, not failure.
preview=summary['EP_readback_comparison']['records'][0]
assert preview['DMA']['RDMA1_bank0']['available'] and preview['DMA']['RDMA1_bank0']['stride_bytes']==0 and not preview['DMA']['RDMA1_bank0']['nonzero_configuration']
for name in ['r10h-field-evidence','r10i-field-evidence']:
 s,_=decode(next((W/name).rglob('counts.bin')));assert 'EP_readback_comparison'not in s
old6=[p for p in(W/'r10j-frame-flow/validation/current/path-reader').rglob('counts.bin')if p.stat().st_size==16384+275*1152]
assert old6
old_summary,_=decode(old6[-1]);assert old_summary['samples']==275 and 'EP_readback_comparison'not in old_summary
cases.append(dict(case='legacy-real-captures-not-reinterpreted-as-EP-tests',passed=True))
cases.append(dict(case='R10j-schema6-flow-capture-backwards-compatible-without-EP-fabrication',passed=True))
report=dict(passed=True,actual_production_capture=str(source),hardware_tested=False,cases=cases,zero_configuration_distinct_from_unavailable=True)
(R/'validation/current/ep-decoder.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print('PASS EP semantic integrity beyond CRC, real-zero values and legacy data separation')
