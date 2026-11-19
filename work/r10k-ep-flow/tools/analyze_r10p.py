"""Decode R10p files, retaining R10k/m/n compatibility and missing-event limits."""
from pathlib import Path
import argparse,json
from analyze_r10l import analyze
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
 d=analyze(a.folder);d['schema']='R10P_COMBINED1'
 if a.out:a.out.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(frames=d['fast']['frames']['retained_frames'],frame_coverage_complete=d['fast']['coverage_complete'],EP_frame_coverage_complete=d['fast']['EP_frame_coverage_complete'],EP=d['fast']['frames'].get('EP_observation'),HEVC_enabled=False),indent=2))
if __name__=='__main__':main()
