"""R11f end-to-end admission, real HAL count and unchanged-route qualification."""
from pathlib import Path
import argparse,collections,json,struct
from analyze_r10l import analyze
from analyze_video_counters import HEADER

def assess(folder):
 d=analyze(folder);q=d['buffers']['HEVC_admission_ABA']
 data=(Path(folder)/'counts.bin').read_bytes();h=HEADER.unpack_from(data);samples=[]
 for i in range(h[5]):
  raw=data[h[2]+i*h[3]:h[2]+(i+1)*h[3]];w=struct.unpack('<320I',raw);ns=struct.unpack_from('<QQ',raw)
  samples.append(dict(begin=ns[0],end=ns[1],valid=w[7],epoch=w[8],key=(w[47],w[67],w[73],w[81]),geometry=tuple(w[70:72])+tuple(w[257:259])+tuple(w[38:47])+(w[90],w[13]),HAL=w[82]))
 sigs=[];rates=[];details=[]
 for c in q['cohorts']:
  lo,hi=c['begin_ns'],c['end_ns'];ss=[s for s in samples if lo is not None and lo<=s['begin']<=s['end']<=hi and s['valid']&0x38==0x38 and s['valid']&64 and not s['valid']&0x80000000]
  # Independent1Hz readbacks avoid losing HAL/geometry coverage when the
  # heavy sampler adapts to5s. Repeated cached snapshots are deduplicated.
  live={r['HAL_begin_ns']:r for r in q['meta_events']if lo is not None and r['HAL_read_valid']and lo<=r['HAL_begin_ns']<=r['HAL_end_ns']<=hi}
  ll=[live[k]for k in sorted(live)];n=t=0;bad=0
  for a,b in zip(ll,ll[1:]):
   delta=(b['HAL_successful_total']-a['HAL_successful_total'])&0xffffffff;dt=(b['HAL_end_ns']-a['HAL_end_ns'])/1e9
   if dt<=0 or dt>3 or delta>100*dt+10:bad+=1;continue
   n+=delta;t+=dt
  hist=collections.Counter(s['geometry']for s in ss);sig=hist.most_common(1)[0][0]if hist else None
  shapes=collections.Counter(tuple(r['shape'])for r in ll)
  sigs.append(sig);rates.append(n/t if t else None)
  details.append(dict(phase=c['phase'],samples=len(ss),independent_snapshots=len(ll),HAL_successful_outputs=n,observed_s=t,HAL_successful_outputs_per_s=rates[-1],counter_unqualified_intervals=bad,maximum_independent_snapshot_gap_s=max([(b['HAL_end_ns']-a['HAL_end_ns'])/1e9 for a,b in zip(ll,ll[1:])]or[0]),geometry_signatures=[dict(words=list(k),samples=v)for k,v in hist.items()],independent_shapes=[dict(words=list(k),samples=v)for k,v in shapes.items()],geometry_stable=len(hist)==1 and len(shapes)==1 and next(iter(shapes),None)==(3840,2160,3840,2160)))
 geometry=bool(sigs[0]and all(v>0 for v in sigs[0][:4])and sigs[0]==sigs[1]==sigs[2]and all(x['geometry_stable']and x['samples']>=4 and x['independent_snapshots']>=20 and not x['counter_unqualified_intervals']for x in details))
 HALdose=bool(all(x is not None and x>0 for x in rates)and rates[1]<.45*min(rates[0],rates[2]))
 clocks=all(len(c[k])==1 for c in q['cohorts']for k in ['CPU0_kHz','DDR_A_Hz','governor'])and q['clock_match']
 qualified=bool(q['evidence_qualified']and q['recovery_within_1ms']and geometry and HALdose and clocks)
 verdict='INSUFFICIENT_CAUSAL_EXPERIMENT'
 if qualified:
  gain=q['bit6_to_bit2_reduction_in_B_ms']
  verdict='ENCODER_RELATED_LOAD_MATERIALLY_LIMITS_UPSTREAM'if gain>=1. else'HEVC_LOAD_REDUCTION_NO_MATERIAL_UPSTREAM_GAIN'if abs(gain)<=.5 else'SMALL_OR_MIXED_EFFECT'
 result=dict(revision=d['counts']['revision'],diagnostic='R11f HEVC normal-quarter-normal',verdict=verdict,qualified=qualified,cohorts=q['cohorts'],independent_HAL_and_geometry=details,
  actual_HEVC_completion_dose_qualified=HALdose,unchanged_route_signature_qualified=geometry,stable_matched_observed_frequencies=clocks,bit6_to_bit2_reduction_in_B_ms=q['bit6_to_bit2_reduction_in_B_ms'],recovery_within_1ms=q['recovery_within_1ms'],
  unique_DDR_root_cause_proven=False,frame_rate_fix_proven=False,requires_final_review=['Path snapshot RAW/output dimensions and native flags','QoS/bridge snapshot equivalence','MP4 finalization and A2 normal recovery; B intentionally has fewer encoded frames'],
  inference='A qualified gain implicates encoder-related shared bandwidth/cache/credits/scheduling as a group. A qualified near-zero effect deprioritizes HEVC admission load at this dose and directs work to RSZ2/WDMA2/EP-internal work. One A/B/A does not identify unique DDR latency.')
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--out',type=Path);a=p.parse_args();q=assess(a.capture);text=json.dumps(q,ensure_ascii=False,indent=2)+'\n';print(text)
 if a.out:a.out.write_text(text,'utf8')
