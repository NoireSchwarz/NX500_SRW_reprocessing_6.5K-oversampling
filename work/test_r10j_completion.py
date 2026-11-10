from pathlib import Path
import sys,subprocess,json,hashlib,struct
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';V=R/'validation/current/completion-reader';V.mkdir(parents=True,exist_ok=True)
sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
from video_completion import decode_completion
binary=V/'completion.arm';src=R/'core/tests/test_video_completion.c'
subprocess.run([sys.executable,'-X','utf8',str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross'),'--source',str(src),'--output',str(binary)],check=True)
f=V/'fixture';f.mkdir(exist_ok=True);result=PostFileModel(binary,[],f).run();assert result['exit']==0,result
b=(f/'completion.bin').read_bytes();assert len(b)==15*1024
rows=[]
for i in range(15):
 raw=b[i*1024:(i+1)*1024];w=struct.unpack('<256I',raw)
 rows.append(dict(sequence=i,t=2_000_000_000,valid=w[7],mark=0,completion=list(w[191:223])))
d=decode_completion(rows,0);by={r['sample']:r for r in d['records']}
assert by[0]['classification']=='completion-not-observed-pending'
assert by[1]['classification']=='completion-latched-before-tracked-IRQ'and by[1]['context_changed_during_followup']and not by[1]['observed_pending_lower_ms']
assert by[2]['observed_pending_lower_ms']>=2 and by[2]['followups']==2
assert by[3]['classification']=='completion-latched-IRQ-disabled'
assert by[4]['classification']=='completion-latched-handler-mismatch'
assert by[5]['classification']=='unavailable'
assert by[10]['classification']=='cross-request-or-changing-header'
assert by[14]['followups']==1 and by[14]['observed_pending_lower_ms']>=3
assert all(r['followups']<=2 for r in d['records'])
report=dict(passed=True,actual_ARM_result=result,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),cases=15,decoded=d,no_native_timer_reads=True,no_ACK_or_device_writes=True,hardware_tested=False)
(R/'validation/current/completion-reader.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
print('PASS 15 actual ARM completion pairing, persistence, IRQ/gate/race/error/late-wakeup scenarios and PC decode')
