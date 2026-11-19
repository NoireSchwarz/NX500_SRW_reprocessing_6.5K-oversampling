"""Decode R10v files, retaining R10k/m/n compatibility and missing-event limits."""
from pathlib import Path
import argparse,json
from analyze_r10l import analyze
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
 d=analyze(a.folder);d['schema']='R10V_COMBINED1'
 d['release_checks']=dict(actual_sampler_revision=d['counts']['revision'],sampler_R10v=d['counts']['revision']=='R10v IPC_BIT6_COUNTERS',producer11=d['fast']['producer_schemas']==[11],native_bit6_flow11=d['fast']['frames'].get('schema')=='NXL_IPC_BIT6_FLOW11',camera_install_not_proven_by_capture_alone=True)
 d['release_checks']['R10v_sampling_paths_confirmed']=all(d['release_checks'][k]for k in ['sampler_R10v','producer11','native_bit6_flow11'])
 if a.out:a.out.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(frames=d['fast']['frames']['retained_frames'],frame_coverage_complete=d['fast']['coverage_complete'],EP_frame_coverage_complete=d['fast']['EP_frame_coverage_complete'],LDC=d['fast']['frames'].get('LDC_observation'),typed_LDC_routes=d['counts'].get('EP_route_evidence'),IPC_buffers=d['fast']['frames'].get('IPC_buffer_observation'),IPC_bit6=d['fast']['frames'].get('IPC_bit6_observation'),paired_native_Linux_addresses=d['counts'].get('IPC_link_evidence'),HEVC_enabled=False),indent=2))
if __name__=='__main__':main()
