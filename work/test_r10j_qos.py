from pathlib import Path
import sys,subprocess,json,hashlib
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';V=R/'validation/current/qos-reader';V.mkdir(parents=True,exist_ok=True);sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
from video_qos import decode_qos
binary=V/'qos-test.arm';src=R/'core/tests/test_video_qos.c'
subprocess.run([sys.executable,'-X','utf8',str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross'),'--source',str(src),'--output',str(binary)],check=True)
fixture=V/'fixture';fixture.mkdir(exist_ok=True);out=PostFileModel(binary,[],fixture).run();assert out['exit']==0,out
raw=(fixture/'qos-ring.bin').read_bytes();data=bytearray(4096);data[640:640+len(raw)]=raw;decoded=decode_qos(data);assert len(decoded['records'])==3
preview,record,after=decoded['records'];assert preview['registers']['0x2012f138']is None and after['registers']['0x2012f138']is None and record['registers']['0x2012f138']=='0x1006'
data[640+32+20]^=1
try:decode_qos(data)
except ValueError:pass
else:raise AssertionError('Corrupt bus snapshot was accepted')
(R/'validation/current/qos-reader.json').write_text(json.dumps(dict(passed=True,actual_ARM_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),actual_ARM_result=out,decoded=decoded,corruption_rejected=True,no_heap_in_helper=True,hardware_tested=False),indent=2)+'\n',encoding='utf8',newline='\n')
print('PASS actual ARM QoS reader, native power-state policy, existing header bounds and PC integrity checks')
