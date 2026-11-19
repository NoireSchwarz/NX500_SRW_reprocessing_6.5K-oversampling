"""Decode R10u files, retaining R10k/m/n compatibility and missing-event limits."""
from pathlib import Path
import argparse,json
from analyze_r10l import analyze
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
 d=analyze(a.folder);d['schema']='R10U_COMBINED1'
 d['release_checks']=dict(actual_sampler_revision=d['counts']['revision'],sampler_R10u=d['counts']['revision']=='R10u IPC_BUFFER_COUNTERS',producer10=d['fast']['producer_schemas']==[10],native_buffer_flow10=d['fast']['frames'].get('schema')=='NXL_IPC_BUFFER_FLOW10',camera_install_not_proven_by_capture_alone=True)
 d['release_checks']['R10u_sampling_paths_confirmed']=all(d['release_checks'][k]for k in ['sampler_R10u','producer10','native_buffer_flow10'])
 if a.out:a.out.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(frames=d['fast']['frames']['retained_frames'],frame_coverage_complete=d['fast']['coverage_complete'],EP_frame_coverage_complete=d['fast']['EP_frame_coverage_complete'],LDC=d['fast']['frames'].get('LDC_observation'),typed_LDC_routes=d['counts'].get('EP_route_evidence'),IPC_buffers=d['fast']['frames'].get('IPC_buffer_observation'),HEVC_enabled=False),indent=2))
if __name__=='__main__':main()
