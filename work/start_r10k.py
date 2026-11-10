from pathlib import Path
import shutil,json
W=Path(__file__).resolve().parent;B=W/'r10j-frame-flow';R=W/'r10k-ep-flow'
assert not R.exists()
def ignore(path,names):return [n for n in names if n in {'validation','build','results','__pycache__','.git','.zig-cache','zig-cache'}or n.startswith('build-')]
shutil.copytree(B,R,ignore=ignore);(R/'validation/current').mkdir(parents=True)
for name in ['build_r10j.py','test_r10j_probe_os.py','test_r10j_path_probe.py','test_r10j_flow_production.py','test_r10j_runtime.py','validate_r10j_install.py']:
    s=(W/name).read_text('utf8').replace('r10j','r10k').replace('R10j','R10k').replace('r10k-frame-flow','r10k-ep-flow')
    # The full-width executable is deliberately frozen at R10j.
    if name=='test_r10j_runtime.py':s=s.replace("if name=='nxl-video-fullwidth.arm':assert 'R10k'", "if name=='nxl-video-fullwidth.arm':assert 'R10j'")
    (W/name.replace('r10j','r10k')).write_text(s,encoding='utf8',newline='\n')
for rel in ['installer/deploy.sh','installer/install-entry.sh','base/nx-lut-lite/src/menu.c']:
    p=R/rel;s=p.read_text('utf8').replace('recovery-r10j-frame-flow','recovery-r10k-ep-flow').replace('R10j VIDEO + SRW','R10k EP + FRAME FLOW');p.write_text(s,encoding='utf8',newline='\n')
for p in (R/'base/nx-lut-lite/src').glob('*.h'):
    s=p.read_text('utf8')
    if 'DCI/UHD FRAME FLOW'in s or 'R10j'in s:
        p.write_text(s.replace('R10j','R10k').replace('DCI/UHD FRAME FLOW','EP + DCI/UHD FLOW'),encoding='utf8',newline='\n')
p=R/'common/video_counter_capture.h';s=p.read_text('utf8').replace('flow[32],commit','flow[32],ep_readback[32],commit').replace('==1152','==1280');p.write_text(s,encoding='utf8',newline='\n')
p=R/'core/src/nxl-video-probe.c';s=p.read_text('utf8').replace('R10j','R10k').replace('#include "../../common/video_flow_capture.h"','#include "../../common/video_flow_capture.h"\n#include "../../common/video_ep_readback.h"').replace('h->schema==6','h->schema==7').replace('h->schema=6','h->schema=7').replace('vp_flow_capture(&reader,s);','vp_flow_capture(&reader,s);vp_ep_readback_capture(&reader,s,vc_now,NULL);').replace('BOOKMARKS_ONLY;','BOOKMARKS_ONLY; EP_AUX_READBACK_MATCHED_FRAMES;')
s=s.replace('if(n==4u&&(a==VC_STATUS', 'if(vp_ep_readback_range(a,n))return 1;\n if(n==4u&&(a==VC_STATUS')
p.write_text(s,encoding='utf8',newline='\n')
p=R/'tools/analyze_video_counters.py';s=p.read_text('utf8').replace('(6,1152)]','(6,1152),(7,1280)]').replace('16384 if schema==6','16384 if schema>=6').replace("if schema==6 else []))","if schema>=6 else [],ep_readback=list(v[287:319])if schema>=7 else []))").replace('if schema==6:', 'if schema>=6:')
s=s.replace(' return summary,intervals',' if schema>=7:\n  from video_ep_readback import decode_ep_readback\n  summary[\'EP_readback_comparison\']=decode_ep_readback(records,intervals,summary.get(\'frame_timing\',{}),h[7])\n return summary,intervals')
p.write_text(s,encoding='utf8',newline='\n')
print('Created isolated R10k EP+flow source; 1280-byte records; R10j RTOS and blue fix unchanged')
