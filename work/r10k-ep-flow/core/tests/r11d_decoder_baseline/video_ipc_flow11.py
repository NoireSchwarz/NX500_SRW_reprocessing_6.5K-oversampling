"""Accepted native IPC bank2 bit6, unchanged event10 and endpoint binding.

The final IRQ timestamp is now the accepted bit2 dispatch before ACK.
Native success still closes the request; its old timestamp is unavailable.
"""
import collections
from video_ep_flow8 import context
from video_ldc_flow9 import metric,context_runs
from video_ipc_flow10 import delta,ram

MAGIC=0x42544c56
METRICS=['cycle_ms','hardware_to_IRQ_ms','hardware_to_LDC_EOF_ms','LDC_EOF_to_final_IRQ_ms','hardware_to_IPC_bit6_ms','IPC_bit6_to_final_IRQ_ms']
LDC_METRICS=METRICS[2:4]
BIT6_METRICS=METRICS[4:]

def summarize(flow):
 rows=flow['records'];statistics={}
 for key in METRICS:
  gate='valid_for_LDC_statistics'if key in LDC_METRICS else'valid_for_bit6_statistics'if key in BIT6_METRICS else'valid_for_statistics'
  values=[r[key]for r in rows if r.get(gate)and key in r]
  if values:statistics[key]=metric(values)
 ldc=dict(present=sum(r['LDC_present']for r in rows),missing=sum(not r['LDC_present']for r in rows),multiple=sum(r['LDC_count_min']>1 for r in rows),saturated=sum(r['LDC_count_saturated']for r in rows),owner_or_route_changed=sum(r['owner_or_route_changed']for r in rows),native_owner_candidates=sum(r['native_owner_candidate']for r in rows),typed_route_associated=sum(r['object_association_valid']for r in rows),usable_for_interval_split=sum(r['valid_for_LDC_statistics']for r in rows),hit_count_histogram=dict(collections.Counter(str(r['LDC_count_min'])+('+'if r['LDC_count_saturated']else'')for r in rows)),buffer_identity_proven=False)
 buffers=dict(completion_present=sum(r['buffer_completion_present']for r in rows),native_input_binding_candidates=sum(r['native_input_binding_candidate']for r in rows),typed_context_associated=sum(r['IPC_buffer_association_valid']for r in rows),independent_exact_frame_snapshot_matches=sum(r['independent_buffer_snapshot_matched']for r in rows),usable_for_statistics=sum(r['valid_for_buffer_statistics']for r in rows),failure_kinds=dict(collections.Counter(k for r in rows for k in r['binding_problems'])),hardware_latched_identity_proven=False,output_DMA_or_HEVC_buffer_identity_proven=False)
 bit6=dict(present=sum(bool(r['IPC_bit6_count_min'])for r in rows),missing=sum(not r['IPC_bit6_count_min']for r in rows),multiple=sum(r['IPC_bit6_count_min']>1 for r in rows),saturated=sum(r['IPC_bit6_count_saturated']for r in rows),native_callback_changed=sum(r['IPC_bit6_callback_changed']for r in rows),ordered_native_candidates=sum(r['valid_bit6_interval_candidate']for r in rows),Linux_slot6_context_associated=sum(r['bit6_route_context_associated']for r in rows),exact_request_slot6_snapshot_matches=sum(r['independent_bit6_exact_request_snapshot']for r in rows),usable_for_interval_split=sum(r['valid_for_bit6_statistics']for r in rows),hit_count_histogram=dict(collections.Counter(str(r['IPC_bit6_count_min'])+('+'if r['IPC_bit6_count_saturated']else'')for r in rows)),interpretation='Accepted bank2 bit6 before original ACK.0..14 exact;15 means15-or-more. The native loaded-table callback is checked at every hit. Missing/multiple/changed/out-of-order events never become an internal split. Hardware EOF/DMA-retirement semantics are not proven.')
 flow.update(statistics=statistics,LDC_observation=ldc,EP_observation=ldc,IPC_buffer_observation=buffers,IPC_bit6_observation=bit6)
 return flow

def decode_ipc_flow11(samples,started_ns):
 frames={};live=[];contexts=[];last=None;generation=0;ordinal=0;discarded=0
 for s in samples:
  c=context(s,started_ns)
  if s['valid']&524288:contexts.append(c)
  if not s['valid']&65536:continue
  w=s['latency']
  if len(w)!=64 or w[:2]!=[MAGIC,11]:raise ValueError('Invalid IPC bit6 timer header')
  n=w[2]
  if last is None:ordinal=n
  elif n<last and not(last>0xf0000000 and n<0x10000000):generation+=1;ordinal=n
  else:ordinal+=((n-last)&0xffffffff)-int(n<last and n!=0 and last!=0)
  last=n
  live.append(dict(observed_at_s=c['observed_at_s'],completed_sequence=n,flags=hex(w[3]),LDC_total_events=w[4],IPC_bit6_total_events=w[15],hardware_start=w[5],final_IRQ=w[6],first_LDC_EOF=w[7],first_IPC_bit6=w[12],last_IPC_bit6_callback=hex(w[14]),RTOS_input_descriptor=hex(w[9]),RTOS_input_PA=hex(w[10]),RTOS_output_descriptor=hex(w[11])))
  for j in range(4):
   raw=w[16+j*12:28+j*12];seq,f,total,hw,irq,ldc,owner,inp,pa,out,bit6,bad=raw
   if not seq:continue
   age=((n-seq)&0xffffffff)-int(n<seq and n!=0 and seq!=0)
   if age>=4 or not f&1:discarded+=1;continue
   key=(generation,seq)
   if key in frames:
    if frames[key]['raw_words']!=raw:raise ValueError('Conflicting committed IPC bit6 record')
    continue
   lc=(f>>20)&15;ls=bool(f&64);bc=f>>28
   if f&~0xf3f37fff or bool(f&32)!=bool(lc)or(ls and lc!=15)or(f&128 and not lc)or(f&65536 and not f&2)or(f&256 and not f&4)or(f&512 and not f&2)or(f&1024 and not f&8)or(f&0x20000 and not bc)or(not bc and bit6):raise ValueError('Invalid IPC bit6 flags/count')
   missing=[name for name,bit in [('hardware',2),('final_IRQ',4),('native_buffer_completion',8)]if not f&bit];problems=[]
   if f&16:problems.append('cancelled-before-native-success-notification')
   if f&0x10100:problems.append('repeated-hardware-or-final-IRQ')
   if (f>>24)&3:problems.append('unsupported-controller')
   hi=delta(irq,hw)/26000
   if f&6==6 and not 0<hi<=2000:problems.append('invalid-HW-IRQ-order')
   binding=[]
   if not f&512:binding.append('HW-input-binding-unverified')
   if not f&1024:binding.append('success-notification-binding-unverified')
   for bit,name in [(2048,'descriptor-or-address-changed'),(4096,'input-range-or-output-null'),(8192,'input-CImage-type-or-parent-state'),(16384,'input-DMA-address-mismatch-or-zero')]:
    if f&bit:binding.append(name)
   if not ram(inp,0x70)or inp>=0x93000000 or not ram(out)or inp==out:binding.append('invalid-descriptor-identity')
   if not pa or pa&3:binding.append('invalid-input-physical-address')
   if bad and not f&0x7800:raise ValueError('Unexplained IPC binding error raw value')
   r=dict(generation=generation,sequence=seq,sequence_ordinal=ordinal-age,observed_at_s=c['observed_at_s'],recording_epoch=s['epoch'],driver_mode=c['driver_mode'],IPC_preset=c['IPC_preset'],source_state=c['source_state'],active_backend=c['active_backend'],mark=s['mark'],raw_words=raw,flags=hex(f),controller_type=(f>>24)&3,event_counts=dict(hardware=(2 if f&65536 else 1)if f&2 else 0,final_IRQ=(2 if f&256 else 1)if f&4 else 0,LDC_EOF=16 if ls else lc,IPC_bit6=bc,native_buffer_completion=1 if f&8 else 0,callback=None),missing_events=missing,problems=problems,valid_for_statistics=not missing and not problems,LDC_present=bool(lc),LDC_count_min=16 if ls else lc,LDC_count_saturated=ls,LDC_first_tick=ldc if lc else None,HW_LDC_owner=hex(owner),owner_or_route_changed=bool(f&128),owner_agreement_all_hits=bool(lc)and not f&128,native_owner_candidate=bool(lc)and not f&128 and owner>=0x81006000 and owner+4<=0x94000000 and not owner&3,object_association_valid=False,valid_for_LDC_statistics=False,valid_for_EP_statistics=False,buffer_completion_present=bool(f&8),input_descriptor=hex(inp),input_physical_address=hex(pa),output_descriptor=hex(out),latest_binding_error_raw=hex(bad)if f&0x7800 else None,binding_problems=binding,native_input_binding_candidate=not binding,IPC_buffer_association_valid=False,independent_buffer_snapshot_matched=False,valid_for_buffer_statistics=False,IPC_bit6_count_min=bc,IPC_bit6_count_saturated=bc==15,IPC_bit6_first_tick=bit6 if bc else None,IPC_bit6_callback_changed=bool(f&0x20000),native_bit6_callback_verified=bool(bc)and not f&0x20000,bit6_route_context_associated=False,independent_bit6_exact_request_snapshot=False,valid_for_bit6_statistics=False,native_buffer_identity_proven=False,LDC_native_buffer_identity_proven=False,callback_timestamp_available=False,completion_timestamp_available=False,IRQ_timestamp_scope='accepted original bank2 bit2 before ACK',request_window_scope='Native success closes/publishes before later bits in a coalesced IRQ dispatch; an accepted bit6 after close increments only the global counter. This is not a lifetime duplicate-success counter.')
   if f&6==6:r['hardware_to_IRQ_ms']=hi
   if lc and f&2:
    r['hardware_to_LDC_EOF_ms']=delta(ldc,hw)/26000
    if f&4:r['LDC_EOF_to_final_IRQ_ms']=delta(irq,ldc)/26000
   if bc and f&2:
    r['hardware_to_IPC_bit6_ms']=delta(bit6,hw)/26000
    if f&4:r['IPC_bit6_to_final_IRQ_ms']=delta(irq,bit6)/26000
   r['valid_LDC_interval_candidate']=bool(r['valid_for_statistics']and lc==1 and not ls and r['native_owner_candidate']and 0<=r.get('hardware_to_LDC_EOF_ms',-1)<=hi)
   r['valid_bit6_interval_candidate']=bool(r['valid_for_statistics']and not binding and bc==1 and not f&0x20000 and 0<=r.get('hardware_to_IPC_bit6_ms',-1)<=hi)
   frames[key]=r
 rows=sorted(frames.values(),key=lambda r:(r['generation'],r['sequence_ordinal']))
 for a,b in zip(rows,rows[1:]):
  if a['generation']==b['generation']and b['sequence_ordinal']==a['sequence_ordinal']+1 and a['recording_epoch']==b['recording_epoch']and a['driver_mode']==b['driver_mode']and a['IPC_preset']==b['IPC_preset']and a['valid_for_statistics']and b['valid_for_statistics']:
   cycle=delta(b['raw_words'][3],a['raw_words'][3])/26000
   if 0<cycle<=2000:a['cycle_ms']=cycle
 return summarize(dict(schema='NXL_IPC_BIT6_FLOW11',clock_Hz=26000000,ring_capacity=4,retained_frames=len(rows),valid_frames=sum(r['valid_for_statistics']for r in rows),missed_frame_sequences=sum(max(0,b['sequence_ordinal']-a['sequence_ordinal']-1)for a,b in zip(rows,rows[1:])if a['generation']==b['generation']),invalid_or_inflight_entries_discarded=discarded,records=rows,live_samples=live,flow_contexts=contexts,interpretation='Native accepted bank2 bit6 and actual loaded-table callback guard, enabled bank0 event10, unchanged endpoint input binding. First bit6 replaces the old success timestamp; success still closes/publishes. Accepted bit2 replaces R10u IRQ-entry time. No hardware EOF, bandwidth, DMA retirement or HEVC identity is inferred from these observations.'))

def associate_routes(flow,routes,started_ns,runs,buffer_pins):
 pins=routes.get('records',[])if routes and routes.get('schema')=='NXL_LDC_ROUTE4'else[]
 bp=buffer_pins.get('records',[])if buffer_pins and buffer_pins.get('schema')=='NXL_IPC_LINK2'else[]
 for r in flow['records']:
  bounds=r.get('callback_publish_linux_bounds')
  if not bounds or bounds['earliest_linux_ns']is None:continue
  lo=(bounds['earliest_linux_ns']-started_ns)/1e9;hi=(bounds['latest_linux_ns']-started_ns)/1e9
  for run in runs:
   k=run['key']
   if k is None or not(run['begin']+1<=lo<=hi<=run['end']-1)or k[0]!=r['driver_mode']or k[1]!=r['IPC_preset']:continue
   lp=[p for p in pins if p['object_binding_valid']and p['timer_schema']==11 and p['driver']==k[0]and p['preset']==k[1]and p['source_state']==k[2]and p['backend']==k[3]and p['source_pointer']==hex(k[4])and run['begin']<=p['elapsed_s']<=run['end']and p['LDC_owner']==r['HW_LDC_owner']]
   if r['native_owner_candidate']and lp:
    r.update(object_association_valid=True,object_binding_generation=lp[0]['generation'],source_state=k[2],active_backend=k[3],association_basis=lp[0]['association_basis']);r['valid_for_LDC_statistics']=r['valid_for_EP_statistics']=r['valid_LDC_interval_candidate']
   current=[p for p in bp if run['begin']<=p['observed_at_s']<=run['end']and p['driver']==k[0]and p['preset']==k[1]and p['backend']==k[3]and p['source_pointer']==hex(k[4])and p['source_state']==k[2]]
   candidates=[p for p in current if p['qualified']and p['input_descriptor']==r['input_descriptor']and p['output_descriptor']==r['output_descriptor']]
   if r['native_input_binding_candidate']and candidates:
    r.update(IPC_buffer_association_valid=True,source_state=k[2],active_backend=k[3]);r['valid_for_buffer_statistics']=r['valid_for_statistics']
    r['independent_buffer_snapshot_matched']=any(p['pending_native_sequence']==r['sequence']and p['HW_tick']==r['raw_words'][3]and p['input_physical_address']==r['input_physical_address']for p in candidates)
   checked=[p for p in current if p['Linux_slot6_verified']]
   r['bit6_route_context_associated']=bool(checked)
   r['independent_bit6_exact_request_snapshot']=any(p['pending_native_sequence']==r['sequence']and p['HW_tick']==r['raw_words'][3]for p in checked)
   r['valid_for_bit6_statistics']=bool(r['valid_bit6_interval_candidate']and checked and r['IPC_buffer_association_valid'])
   break
 return summarize(flow)

def compare_flows(flow):
 groups=collections.defaultdict(list)
 for r in flow['records']:groups[(r['driver_mode'],r['IPC_preset'],r['source_state'])].append(r)
 rows=[]
 for k,rr in groups.items():
  g=summarize(dict(records=rr))
  rows.append(dict(driver_mode=k[0],IPC_preset=k[1],source_state=k[2],frames=sum(r['valid_for_statistics']for r in rr),statistics=g['statistics'],LDC_count_histogram=g['LDC_observation']['hit_count_histogram'],IPC_bit6=g['IPC_bit6_observation']))
 return dict(groups=rows,scope='One-second mode/source transition exclusion, typed owner/input context and actual slot6/enable context. Missing/multiple/unverified bit6 never yields a split. Programmed-address correspondence is not hardware-latched image identity.')
