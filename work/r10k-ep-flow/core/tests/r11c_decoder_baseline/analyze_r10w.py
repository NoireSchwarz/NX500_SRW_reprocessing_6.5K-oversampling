"""R10w native stage-marker analysis, with explicit hardware/identity limits."""
from pathlib import Path
import argparse,json
from analyze_r10l import analyze
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
 d=analyze(a.folder);d['schema']='R10W_COMBINED1'
 d['release_checks']=dict(sampler_R10w=d['counts']['revision']=='R10w PIPELINE_MARKERS',producer11=d['fast']['producer_schemas']==[11],fast3=d['fast']['file_schema']==3,pipeline1=d['pipeline']is not None,camera_install_not_proven_by_capture_alone=True)
 if a.out:a.out.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(release_checks=d['release_checks'],requests=d['fast']['frames']['retained_frames'],complete_ring=d['fast']['coverage_complete'],stage_edges=d['pipeline']['endpoint_matched_rising_edges_by_stage']if d['pipeline']else{},pending_bounds_ms=[b['done_pending_lower_bound_ms']for b in d['pipeline']['bursts']]if d['pipeline']else[]),indent=2))
if __name__=='__main__':main()
