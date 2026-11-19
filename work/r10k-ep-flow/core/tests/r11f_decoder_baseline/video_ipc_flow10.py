"""Controller0 native success notification and bounded input-DMA association.

The success marker is AFTER the final IRQ. It is not an internal hardware EOF,
an encoder result, or proof that LDC's same-window event belongs to that buffer.
"""
import collections,struct
from video_ep_flow8 import context
from video_ldc_flow9 import metric,context_runs

MAGIC=0x41544c56
METRICS=['cycle_ms','hardware_to_IRQ_ms','hardware_to_buffer_completion_ms','IRQ_to_buffer_completion_ms','buffer_completion_to_next_hardware_ms','hardware_to_LDC_EOF_ms','LDC_EOF_to_final_IRQ_ms','LDC_EOF_to_buffer_completion_ms']
LDC_METRICS=['hardware_to_LDC_EOF_ms','LDC_EOF_to_final_IRQ_ms','LDC_EOF_to_buffer_completion_ms']
def ram(p,n=4):return not p&3 and 0x82000000<=p and p+n<=0x94000000
def delta(a,b):return ((a-b+0x80000000)&0xffffffff)-0x80000000
def summarize(flow):
 rows=flow['records'];statistics={}
 for k in METRICS:
  gate='valid_for_LDC_statistics'if k in LDC_METRICS else'valid_for_buffer_statistics'if k in ['hardware_to_buffer_completion_ms','IRQ_to_buffer_completion_ms','buffer_completion_to_next_hardware_ms']else'valid_for_statistics'
  vals=[r[k]for r in rows if r.get(gate)and k in r]
  if vals:statistics[k]=metric(vals)
 obs=dict(present=sum(r['LDC_present']for r in rows),missing=sum(not r['LDC_present']for r in rows),multiple=sum(r['LDC_count_min']>1 for r in rows),saturated=sum(r['LDC_count_saturated']for r in rows),owner_or_route_changed=sum(r['owner_or_route_changed']for r in rows),native_owner_candidates=sum(r['native_owner_candidate']for r in rows),typed_route_associated=sum(r['object_association_valid']for r in rows),usable_for_interval_split=sum(r['valid_for_LDC_statistics']for r in rows),hit_count_histogram=dict(collections.Counter(str(r['LDC_count_min'])+('+'if r['LDC_count_saturated']else'')for r in rows)),buffer_identity_proven=False)
 buffers=dict(completion_present=sum(r['buffer_completion_present']for r in rows),native_input_binding_candidates=sum(r['native_input_binding_candidate']for r in rows),typed_context_associated=sum(r['IPC_buffer_association_valid']for r in rows),independent_exact_frame_snapshot_matches=sum(r['independent_buffer_snapshot_matched']for r in rows),usable_for_statistics=sum(r['valid_for_buffer_statistics']for r in rows),failure_kinds=dict(collections.Counter(k for r in rows for k in r['binding_problems'])),hardware_latched_identity_proven=False,output_DMA_or_HEVC_buffer_identity_proven=False)
 flow.update(statistics=statistics,LDC_observation=obs,EP_observation=obs,IPC_buffer_observation=buffers);return flow
def decode_ipc_flow10(samples,started_ns):
 frames={};live=[];contexts=[];last=None;generation=0;ordinal=0;discarded=0
 for s in samples:
  c=context(s,started_ns)
  if s['valid']&524288:contexts.append(c)
  if not s['valid']&65536:continue
  w=s['latency']
  if len(w)!=64 or w[:2]!=[MAGIC,10]:raise ValueError('Invalid IPC buffer timer header')
  n=w[2]
  if last is None:ordinal=n
  elif n<last and not(last>0xf0000000 and n<0x10000000):generation+=1;ordinal=n
  else:ordinal+=((n-last)&0xffffffff)-int(n<last and n!=0 and last!=0)
  last=n;live.append(dict(observed_at_s=c['observed_at_s'],completed_sequence=n,flags=hex(w[3]),LDC_total_events=w[4],hardware_start=w[5],final_IRQ=w[6],first_LDC_EOF=w[7]))
  for j in range(4):
   raw=w[16+j*12:28+j*12];seq,f,total,hw,irq,ldc,owner,inp,pa,out,done,bad=raw
   if not seq:continue
   age=((n-seq)&0xffffffff)-int(n<seq and n!=0 and seq!=0)
   if age>=4 or not f&1:discarded+=1;continue
   key=(generation,seq)
   if key in frames:
    if frames[key]['raw_words']!=raw:raise ValueError('Conflicting committed IPC buffer record')
    continue
   count=(f>>26)&15;saturated=bool(f&64)
   if f&~0x3f017fff or bool(f&32)!=bool(count)or(saturated and count!=15)or(f&128 and not count)or(f&0x10000 and not f&2)or(f&256 and not f&4)or(f&512 and not f&2)or(f&1024 and not f&8):raise ValueError('Invalid IPC buffer flags/count')
   missing=[name for name,bit in [('hardware',2),('final_IRQ',4),('native_buffer_completion',8)]if not f&bit];problems=[]
   if f&16:problems.append('cancelled-before-native-success-notification')
   if f&0x10100:problems.append('repeated-hardware-or-final-IRQ')
   if (f>>24)&3:problems.append('unsupported-controller')
   hi=delta(irq,hw)/26000;hd=delta(done,hw)/26000;post=delta(done,irq)/26000
   if not missing and(not 0<hi<=2000 or not 0<=post<=2000 or not 0<hd<=2000):problems.append('invalid-time-order')
   binding=[]
   if not f&512:binding.append('HW-input-binding-unverified')
   if not f&1024:binding.append('success-notification-binding-unverified')
   for bit,name in [(2048,'descriptor-or-address-changed'),(4096,'input-range-or-output-null'),(8192,'input-CImage-type-or-parent-state'),(16384,'input-DMA-address-mismatch-or-zero')]:
    if f&bit:binding.append(name)
   if not ram(inp,0x70)or inp>=0x93000000 or not ram(out)or inp==out:binding.append('invalid-descriptor-identity')
   if not pa or pa&3:binding.append('invalid-input-physical-address')
   if bad and not f&0x7800:raise ValueError('Unexplained IPC binding error raw value')
   r=dict(generation=generation,sequence=seq,sequence_ordinal=ordinal-age,observed_at_s=c['observed_at_s'],recording_epoch=s['epoch'],driver_mode=c['driver_mode'],IPC_preset=c['IPC_preset'],source_state=c['source_state'],active_backend=c['active_backend'],mark=s['mark'],raw_words=raw,flags=hex(f),controller_type=(f>>24)&3,event_counts=dict(hardware=(2 if f&0x10000 else 1)if f&2 else 0,final_IRQ=(2 if f&256 else 1)if f&4 else 0,LDC_EOF=16 if saturated else count,native_buffer_completion=1 if f&8 else 0,callback=None),completion_count_scope='The first successful parent notification closes the request window; this is not a counter of later duplicate notifications.',repeat_counts_are_lower_bounds=True,missing_events=missing,problems=problems,valid_for_statistics=not missing and not problems,LDC_present=bool(count),LDC_count_min=16 if saturated else count,LDC_count_saturated=saturated,LDC_first_tick=ldc if count else None,HW_LDC_owner=hex(owner),owner_or_route_changed=bool(f&128),owner_agreement_all_hits=bool(count)and not f&128,native_owner_candidate=bool(count)and not f&128 and owner>=0x81006000 and owner+4<=0x94000000 and not owner&3,object_association_valid=False,valid_for_LDC_statistics=False,valid_for_EP_statistics=False,buffer_completion_present=bool(f&8),input_descriptor=hex(inp),input_physical_address=hex(pa),output_descriptor=hex(out),latest_binding_error_raw=hex(bad)if f&0x7800 else None,binding_problems=binding,native_input_binding_candidate=not binding,IPC_buffer_association_valid=False,independent_buffer_snapshot_matched=False,valid_for_buffer_statistics=False,native_buffer_identity_proven=False,LDC_native_buffer_identity_proven=False,callback_timestamp_available=False)
   if f&6==6:r['hardware_to_IRQ_ms']=hi
   if f&10==10:r['hardware_to_buffer_completion_ms']=hd
   if f&12==12:r['IRQ_to_buffer_completion_ms']=post
   if count and f&2:
    r['hardware_to_LDC_EOF_ms']=delta(ldc,hw)/26000
    if f&4:r['LDC_EOF_to_final_IRQ_ms']=delta(irq,ldc)/26000
    if f&8:r['LDC_EOF_to_buffer_completion_ms']=delta(done,ldc)/26000
   r['valid_LDC_interval_candidate']=bool(r['valid_for_statistics']and count==1 and not saturated and r['native_owner_candidate']and 0<=r.get('hardware_to_LDC_EOF_ms',-1)<=hi)
   frames[key]=r
 rows=sorted(frames.values(),key=lambda r:(r['generation'],r['sequence_ordinal']))
 for a,b in zip(rows,rows[1:]):
  if a['generation']==b['generation']and b['sequence_ordinal']==a['sequence_ordinal']+1 and a['recording_epoch']==b['recording_epoch']and a['driver_mode']==b['driver_mode']and a['valid_for_statistics']and b['valid_for_statistics']:
   cycle=delta(b['raw_words'][3],a['raw_words'][3])/26000;post=delta(b['raw_words'][3],a['raw_words'][10])/26000
   if 0<cycle<=2000 and 0<=post<=2000:a['cycle_ms']=cycle;a['buffer_completion_to_next_hardware_ms']=post
 return summarize(dict(schema='NXL_IPC_BUFFER_FLOW10',clock_Hz=26000000,ring_capacity=4,retained_frames=len(rows),valid_frames=sum(r['valid_for_statistics']for r in rows),missed_frame_sequences=sum(max(0,b['sequence_ordinal']-a['sequence_ordinal']-1)for a,b in zip(rows,rows[1:])if a['generation']==b['generation']),invalid_or_inflight_entries_discarded=discarded,records=rows,live_samples=live,flow_contexts=contexts,interpretation='Native controller0 input CImage and RDMA0 address are checked at trigger and at the native IPC parent successful-notification entry801cdc40. The output is a descriptor identity only, not an output-DMA/HEVC address. The marker is after finalIRQ and replaces the older early-callback timestamp. Enabled bank0 event10 and exact bounded hit counts remain; a same-window LDC hit is not proven same-image EOF. IRQ times are observations, not hardware-latched busy time. Four48-byte slots retain full26MHz ticks in the unchanged280-byte lease;100ms drain gaps/overwrites remain explicit.'))
def associate_routes(flow,routes,started_ns,runs,buffer_pins):
 pins=routes.get('records',[])if routes and routes.get('schema')=='NXL_LDC_ROUTE4'else[]
 bp=buffer_pins.get('records',[])if buffer_pins else[]
 for r in flow['records']:
  bounds=r.get('callback_publish_linux_bounds')
  if not bounds or bounds['earliest_linux_ns']is None:continue
  lo=(bounds['earliest_linux_ns']-started_ns)/1e9;hi=(bounds['latest_linux_ns']-started_ns)/1e9
  for run in runs:
   k=run['key']
   if k is None or not(run['begin']+1<=lo<=hi<=run['end']-1)or k[0]!=r['driver_mode']or k[1]!=r['IPC_preset']:continue
   lp=[p for p in pins if p['object_binding_valid']and p['timer_schema']==10 and p['driver']==k[0]and p['preset']==k[1]and p['source_state']==k[2]and p['backend']==k[3]and p['source_pointer']==hex(k[4])and run['begin']<=p['elapsed_s']<=run['end']and p['LDC_owner']==r['HW_LDC_owner']]
   if r['native_owner_candidate']and lp:r.update(object_association_valid=True,object_binding_generation=lp[0]['generation'],source_state=k[2],active_backend=k[3],association_basis=lp[0]['association_basis']);r['valid_for_LDC_statistics']=r['valid_for_EP_statistics']=r['valid_LDC_interval_candidate']
   candidates=[p for p in bp if p['qualified']and run['begin']<=p['observed_at_s']<=run['end']and p['driver']==k[0]and p['preset']==k[1]and p['backend']==k[3]and p['source_pointer']==hex(k[4])and p['source_state']==k[2]and p['input_descriptor']==r['input_descriptor']and p['output_descriptor']==r['output_descriptor']]
   if r['native_input_binding_candidate']and candidates:
    r.update(IPC_buffer_association_valid=True,source_state=k[2],active_backend=k[3]);r['valid_for_buffer_statistics']=r['valid_for_statistics']
    r['independent_buffer_snapshot_matched']=any(p['pending_native_sequence']==r['sequence']and p['HW_tick']==r['raw_words'][3]and p['input_physical_address']==r['input_physical_address']for p in candidates)
   break
 return summarize(flow)
def compare_flows(flow):
 groups=collections.defaultdict(list)
 for r in flow['records']:groups[(r['driver_mode'],r['IPC_preset'],r['source_state'])].append(r)
 out=[]
 for key,rows in groups.items():
  stats={}
  for name in METRICS:
   gate='valid_for_LDC_statistics'if name in LDC_METRICS else'valid_for_buffer_statistics'if name in ['hardware_to_buffer_completion_ms','IRQ_to_buffer_completion_ms','buffer_completion_to_next_hardware_ms']else'valid_for_statistics'
   values=[r[name]for r in rows if r.get(gate)and name in r]
   if values:stats[name]=metric(values)
  out.append(dict(driver_mode=key[0],IPC_preset=key[1],source_state=key[2],frames=sum(r['valid_for_statistics']for r in rows),statistics=stats,LDC_count_histogram=dict(collections.Counter(str(r['LDC_count_min'])+('+'if r['LDC_count_saturated']else'')for r in rows))))
 return dict(groups=out,scope='Buffer timing requires native input binding plus independent typed context. LDC timings remain ordered same-window owner-qualified observations, not same-image hardware EOF proof.')
