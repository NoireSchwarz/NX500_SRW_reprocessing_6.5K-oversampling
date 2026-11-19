"""Decode R10x three-file capture; maintain legacy capture compatibility."""
from pathlib import Path
import argparse,json
from analyze_r10l import analyze
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 d=analyze(a.folder);a.out.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(revision=d['counts']['revision'],coverage_complete=d['fast']['coverage_complete'],recovered_retry_drains=d['fast'].get('recovered_retry_drains',0),pipeline_schema=d['pipeline']['schema']if d.get('pipeline')else None,output_address_matches=d.get('pipeline',{}).get('endpoint_matched_WDMA2_HEVC_address_matches',0),HEVC_same_buffer_return_proven=False,hardware_busy_time_proven=False),ensure_ascii=False,indent=2))
if __name__=='__main__':main()
