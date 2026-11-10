"""Offline, read-only R8r LDC evidence comparison. No completion inference.
Usage: python analyze_ldc_hardware.py CARD_ROOT JOB_DIRECTORY OUTPUT_JSON
Native phase 10 means a typed request was observed, not that DMA ran.
"""
import argparse,json,struct
from pathlib import Path
from compare_selected_photo import frames,media_path

def checked_snapshot(s,offsets):
 for bank in ('top','config','debug'):
  assert len(s[bank])==len(offsets[bank]),'snapshot array length mismatch'
 return s

def native_snapshot(payload,schema):
 assert len(payload)==schema['bytes']==700
 v=struct.unpack('<175I',payload);assert v[0]==1 and v[1]==700
 s=dict(phase=v[2],flags=v[3],clock_before=v[4:6],clock_after=v[6:8]);at=8
 for bank in ('top','config','debug'):
  n=schema[bank+'_count'];s[bank]=list(v[at:at+n]);at+=n
 assert at==len(v)
 return s

def states(snapshots,offsets):
 result={}
 for bank,bit in [('top',1),('config',2),('debug',4)]:
  valid=[s for s in snapshots if s['flags']&bit and s['flags']&8]
  result[bank]=[dict(offset=hex(off),observed_states=len(values),values=[hex(x) for x in sorted(values)[:16]],values_truncated=len(values)>16)
   for i,off in enumerate(offsets[bank]) if (values:={s[bank][i] for s in valid})]
 return result

def analyze(card,job):
 card,job=Path(card),Path(job)
 fronts=list(job.glob('front_*/front'));assert len(fronts)==1,'choose one experiment job'
 front=fronts[0];audit=front/'ldc-hardware-audit.json';out=dict(schema=1,hardware_causality_proven=False,completion_or_release_authority=False,issues=[])
 if not audit.exists():out['issues'].append('NO_WORKER_HARDWARE_EVIDENCE');return out
 w=json.loads(audit.read_text('utf8'));assert w['schema']==1 and len(w['snapshots'])==4 and not w['checkpoint_mask']&~15 and w['checkpoints']==bin(w['checkpoint_mask']).count('1')
 offsets={b:w[b+'_offsets'] for b in ('top','config','debug')}
 snapshots=[checked_snapshot(s,offsets) for i,s in enumerate(w['snapshots']) if w['checkpoint_mask']&(1<<i)]
 out['worker']=dict(source_sha256=w['source_sha256'],mapped_bytes=w['ldc_mapped_bytes'],phases=[dict(phase=s['phase'],flags=s['flags'],clock_before=s['clock_before'],clock_after=s['clock_after']) for s in snapshots],states=states(snapshots,offsets),output_witness={k:v for k,v in w['output_witness'].items() if not k.endswith('_hex')})
 out['cautions']=['Snapshots are sequential reads, not atomic.','Changed destination bytes do not prove completion; unchanged bytes do not prove no DMA.','TOP 0x100c and channel FSM registers are controls, not established busy counters.','Native and standalone use different contexts; differences require interpretation.']
 linkfile=front/'native-recorder-link.json'
 if not linkfile.exists():out['issues'].append('NO_NATIVE_RECORDER_LINK');return out
 link=json.loads(linkfile.read_text('utf8'));report=media_path(card,link['recorder_report']);schemafile=report/'ldc-hardware-schema.json'
 if not schemafile.exists():out['issues'].append('NO_NATIVE_HARDWARE_SCHEMA');return out
 schema=json.loads(schemafile.read_text('utf8'))
 assert all(schema[b+'_offsets']==offsets[b] for b in offsets),'worker/native schemas differ'
 native=[];after_selection=0
 for h,payload in frames(report/'contexts.bin'):
  if h['kind']!=15:continue
  if h['end_ns']>link['selection_mono_ns']:after_selection+=1;continue
  s=checked_snapshot(native_snapshot(payload,schema),offsets);s.update(h);native.append(s)
 active=[s for s in native if s['phase']==10]
 out['native']=dict(recorder_report=link['recorder_report'],source_sha256_matches=link['source_sha256']==w['source_sha256'],pairing_verified=link['native_pairing_verified'],frames_before_selection=len(native),request_associated_frames=len(active),excluded_after_selection=after_selection,states=states(active,offsets))
 if not active:out['issues'].append('NO_NATIVE_LDC_REQUEST_HARDWARE_SAMPLES_BEFORE_SELECTION')
 if not link['native_pairing_verified']:out['issues'].append('NATIVE_PHOTO_PAIRING_NOT_VERIFIED')
 if active and snapshots:
  latest=snapshots[-1];diff={}
  for bank,bit in [('top',1),('config',2),('debug',4)]:
   valid=[s for s in active if s['flags']&bit and s['flags']&8]
   if latest['flags']&bit and latest['flags']&8 and valid:
    diff[bank]=[hex(off) for i,off in enumerate(offsets[bank]) if latest[bank][i] not in {s[bank][i] for s in valid}]
  out['worker_final_values_absent_from_observed_native_states']=diff
 return out

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('card');p.add_argument('job');p.add_argument('output');a=p.parse_args()
 result=analyze(a.card,a.job);dest=Path(a.output);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(output=str(dest),issues=result['issues'])))
