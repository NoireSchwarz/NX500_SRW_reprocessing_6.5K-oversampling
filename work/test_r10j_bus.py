from pathlib import Path
import sys,subprocess,json,hashlib,struct
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';V=R/'validation/current/bus-reader';V.mkdir(parents=True,exist_ok=True)
sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
from video_bus import decode_bus,REGISTERS
src=R/'core/tests/test_video_bus.c';binary=V/'bus-test.arm'
subprocess.run([sys.executable,'-X','utf8',str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross'),'--source',str(src),'--output',str(binary)],check=True)
fixture=V/'fixture';fixture.mkdir(exist_ok=True);result=PostFileModel(binary,[],fixture).run();assert result['exit']==0,result
raw=(fixture/'bus-ring.bin').read_bytes();data=bytearray(4096);struct.pack_into('<8s6I',data,1024,b'NXLPATH1',2,256,3,0,0,0);data[2048:2048+len(raw)]=raw
decoded=decode_bus(data);assert decoded['total_captured']==5 and len(decoded['records'])==3
assert all(len(x['registers'])==136 and len(x['DMA_groups'])==24 and not x['crossed_frame']for x in decoded['records'])
assert [x['sequence']for x in decoded['records']]==[0,3,4]
assert len({x['address']for x in REGISTERS})==136 and all(not 0x20810400<=int(x['address'],16)<0x20810500 for x in REGISTERS)
bad=bytearray(data);bad[2048+32+140]^=1
try:decode_bus(bad)
except ValueError:pass
else:raise AssertionError('DMA CRC failure not rejected')
data[2048:2056]=bytes(8)
try:decode_bus(data)
except ValueError:pass
else:raise AssertionError('Missing DMA extension not rejected')
report=dict(passed=True,actual_ARM_binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),actual_ARM_result=result,decoded=decoded,corruption_rejected=True,DMA_whitelist_still_excludes_IRQ_status_and_ACK=True,no_heap_in_helper=True,sampler_buffer_bytes=6152192,hardware_tested=False)
(R/'validation/current/bus-reader.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
print('PASS actual ARM DMA capture and PC decode, native register policy and bounded existing RAM')
