from pathlib import Path
import sys,subprocess,json,hashlib
W=Path(__file__).resolve().parent;R=W/'r10k-ep-flow';V=R/'validation/current';sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
binary=V/'ep-contract.arm'
subprocess.run([sys.executable,'-X','utf8',str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross'),'--source',str(R/'core/tests/test_video_ep_readback.c'),'--output',str(binary)],check=True)
result=PostFileModel(binary,[],V).run();assert result['exit']==0 and 'PASS actual EP capture'in result['stdout'],result
report=dict(passed=True,actual_ARM=True,production_source_included=True,hardware_tested=False,result=result,
  source_sha256=hashlib.sha256((R/'core/src/nxl-video-probe.c').read_bytes()).hexdigest(),
  capture_sha256=hashlib.sha256((R/'common/video_ep_readback.h').read_bytes()).hexdigest(),
  scope='Exact read ranges and no timer/ACK admission; all20 per-field races; unavailable groups; full live-header change; mode change; missing prerequisites; no camera device access.')
(V/'ep-contract.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print(result['stdout'])
