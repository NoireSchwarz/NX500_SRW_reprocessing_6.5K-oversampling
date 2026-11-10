from pathlib import Path
import sys,subprocess,struct,json,hashlib
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';V=R/'validation/current/progress-reader';V.mkdir(parents=True,exist_ok=True)
sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
from video_hardware_progress import decode_progress,compare_qos_intervals
binary=V/'progress.arm';src=R/'core/tests/test_video_progress.c'
subprocess.run([sys.executable,'-X','utf8',str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross'),'--source',str(src),'--output',str(binary)],check=True)
f=V/'fixture';f.mkdir(exist_ok=True);out=PostFileModel(binary,[],f).run();assert out['exit']==0,out
raw=(f/'progress.bin').read_bytes();assert len(raw)==6*1024
samples=[]
for i in range(6):
 w=struct.unpack_from('<256I',raw,i*1024);samples.append(dict(sequence=i,t=2_000_000_000,mark=0,valid=w[7],progress=list(w[223:255]),completion=[0]*29+[1,1,0]))
d=decode_progress(samples,0);rr=d['records'];assert len(rr)==6
assert rr[1]['first']!=rr[1]['second']and rr[1]['coherent']
assert not rr[2]['coherent']and rr[3]['EP_debug_selector']is None and'0x208210e0'not in rr[3]['first']
assert rr[4]['EP_debug_selector']is None and'0x208210e0'not in rr[4]['second']and '0x208210cc'not in rr[5]['first']
# A cached B request must never classify actual A hardware as a successful B
# result. The valid matched B intervals are also weighted by elapsed duration.
intervals=[]
for i,(dt,rate)in enumerate([(0.2,30),(0.2,30),(0.2,30),(0.2,30),(0.2,30),(0.5,24),(1.0,30)]):
 intervals.append(dict(seconds=sum(x[0]for x in [(0.2,30)]*min(i+1,5))+sum(x[0]for x in [(0.5,24),(1.0,30)][:max(0,i-4)]),interval_s=dt,comparison_phase='B-bridge-qos',comparison_result=1,mark=1,HEVC_bridge_requested='0x14a',HEVC_bridge_readback='0x14a',HEVC_successful_outputs_per_s=rate,source_forwarded_per_s=rate))
matched=compare_qos_intervals(intervals,'0x0')['records'];assert len(matched)==1
assert abs(matched[0]['statistics']['source_forwarded_per_s']['weighted_mean']-28)<1e-8
for row in intervals:row['HEVC_bridge_readback']='0x0'
assert not compare_qos_intervals(intervals,'0x0')['records']
(R/'validation/current/progress-reader.json').write_text(json.dumps(dict(passed=True,cases=6,actual_ARM_result=out,actual_ARM_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),decoded=d,ACK_reads_or_writes=False,native_timer_reads=False,hardware_tested=False),indent=2)+'\n',encoding='utf8',newline='\n')
print('PASS actual ARM progress pairing, changing events, crossing frames, missing status, conditional EP debug and PC decode')
