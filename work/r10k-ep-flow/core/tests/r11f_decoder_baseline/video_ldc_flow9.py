"""Native LDC EOF observations with exact bounded hit counts and owner checks."""
import collections,statistics,struct
from video_ep_flow8 import context

SPLITS=['hardware_to_LDC_EOF_ms','LDC_EOF_to_final_IRQ_ms']
def metric(v):
 v=sorted(v)
 return dict(n=len(v),min=v[0],median=statistics.median(v),mean=statistics.mean(v),p95=v[min(len(v)-1,int(.95*len(v)))],max=v[-1])if v else None
def summarize(flow):
 rows=flow['records'];stats={}
 for name in ['hardware_to_IRQ_ms','IRQ_to_callback_ms','cycle_ms','callback_to_next_hardware_ms',*SPLITS]:
  vv=[r[name]for r in rows if name in r and r['valid_for_LDC_statistics'if name in SPLITS else'valid_for_statistics']]
  if vv:stats[name]=metric(vv)
 obs=dict(present=sum(r['LDC_present']for r in rows),missing=sum(not r['LDC_present']for r in rows),multiple=sum(r['LDC_count_min']>1 for r in rows),saturated=sum(r['LDC_count_saturated']for r in rows),owner_or_route_changed=sum(r['owner_or_route_changed']for r in rows),native_owner_candidates=sum(r['native_owner_candidate']for r in rows),typed_route_associated=sum(r['object_association_valid']for r in rows),usable_for_interval_split=sum(r['valid_for_LDC_statistics']for r in rows),hit_count_histogram=dict(collections.Counter(str(r['LDC_count_min'])+('+'if r['LDC_count_saturated']else'')for r in rows)),buffer_identity_proven=False)
 flow.update(statistics=stats,LDC_observation=obs,EP_observation=obs);return flow
def decode_ldc_flow9(samples,started_ns):
 frames={};live=[];contexts=[];last=None;gen=0;ordinal=0;discarded=0
 delta=lambda a,b:((a-b+0x80000000)&0xffffffff)-0x80000000
 for s in samples:
  c=context(s,started_ns)
  if s['valid']&524288:contexts.append(c)
  if not s['valid']&65536:continue
  w=s['latency']
  if len(w)!=64 or w[:2]!=[0x39544c56,9]:raise ValueError('Invalid LDC timing header')
  n=w[2]
  if last is None:ordinal=n
  elif n<last and not(last>0xf0000000 and n<0x10000000):gen+=1;ordinal=n
  else:ordinal+=((n-last)&0xffffffff)-int(n<last and n!=0 and last!=0)
  last=n;live.append(dict(observed_at_s=c['observed_at_s'],completed_sequence=n,flags=hex(w[3]),LDC_total_events=w[4],hardware_start=w[5],final_IRQ=w[6],first_LDC_EOF=w[7]))
  for slot in range(8):
   raw=w[8+slot*7:15+slot*7];seq,hw,ldc,owner,irq,cb,f=raw
   if not seq:continue
   age=((n-seq)&0xffffffff)-int(n<seq and n!=0 and seq!=0)
   if age>=8 or not f&1:discarded+=1;continue
   key=(gen,seq)
   if key in frames:
    if frames[key]['raw_words']!=raw:raise ValueError('Conflicting committed LDC record')
    continue
   count=(f>>26)&15;saturated=bool(f&64)
   if f&~0x3f0101ff or bool(f&32)!=bool(count)or(saturated and count!=15)or(f&128 and not count)or(f&0x10000 and not f&2)or(f&256 and not f&4):raise ValueError('Invalid LDC flags/count')
   missing=[name for name,bit in [('hardware',2),('final_IRQ',4),('callback',8)]if not f&bit];problems=[]
   if f&16:problems.append('cancelled-before-callback')
   if f&0x10100:problems.append('repeated-hardware-or-final-IRQ')
   dt=delta(irq,hw)/26000;post=delta(cb,irq)/26000
   if not missing and(not 0<dt<=2000 or not 0<=post<=2000):problems.append('invalid-time-order')
   r=dict(generation=gen,sequence=seq,sequence_ordinal=ordinal-age,observed_at_s=c['observed_at_s'],recording_epoch=s['epoch'],driver_mode=c['driver_mode'],IPC_preset=c['IPC_preset'],source_state=c['source_state'],active_backend=c['active_backend'],mark=s['mark'],raw_words=raw,flags=hex(f),controller_type=(f>>24)&3,event_counts=dict(hardware=(2 if f&0x10000 else 1)if f&2 else 0,final_IRQ=(2 if f&256 else 1)if f&4 else 0,SRAM_load=None,CS0_start=None,LDC_EOF=16 if saturated else count),repeat_counts_are_lower_bounds=True,missing_events=missing,problems=problems,valid_for_statistics=not missing and not problems,LDC_present=bool(count),LDC_count_min=16 if saturated else count,LDC_count_saturated=saturated,LDC_first_tick=ldc if count else None,HW_LDC_owner=hex(owner),owner_or_route_changed=bool(f&128),owner_agreement_all_hits=bool(count)and not f&128,native_owner_candidate=bool(count)and not f&128 and owner>=0x81006000 and owner+4<=0x94000000 and not owner&3,object_association_valid=False,valid_for_LDC_statistics=False,valid_for_EP_statistics=False,native_buffer_identity_proven=False)
   if f&6==6:r['hardware_to_IRQ_ms']=dt
   if f&12==12:r['IRQ_to_callback_ms']=post
   if count and f&2:
    first=delta(ldc,hw)/26000;r['hardware_to_LDC_EOF_ms']=first
    if f&4:r['LDC_EOF_to_final_IRQ_ms']=delta(irq,ldc)/26000
   r['valid_LDC_interval_candidate']=bool(r['valid_for_statistics']and count==1 and not saturated and r['native_owner_candidate']and 0<=r.get('hardware_to_LDC_EOF_ms',-1)<=dt)
   frames[key]=r
 rows=sorted(frames.values(),key=lambda r:(r['generation'],r['sequence_ordinal']))
 for a,b in zip(rows,rows[1:]):
  if a['generation']==b['generation']and b['sequence_ordinal']==a['sequence_ordinal']+1 and a['recording_epoch']==b['recording_epoch']and a['driver_mode']==b['driver_mode']and a['valid_for_statistics']and b['valid_for_statistics']:
   d=delta(b['raw_words'][1],a['raw_words'][1])/26000
   if 0<d<=2000:a['cycle_ms']=d;a['callback_to_next_hardware_ms']=delta(b['raw_words'][1],a['raw_words'][5])/26000
 return summarize(dict(schema='NXL_LDC_FLOW9',clock_Hz=26000000,retained_frames=len(rows),valid_frames=sum(r['valid_for_statistics']for r in rows),missed_frame_sequences=sum(max(0,b['sequence_ordinal']-a['sequence_ordinal']-1)for a,b in zip(rows,rows[1:])if a['generation']==b['generation']),invalid_or_inflight_entries_discarded=discarded,records=rows,live_samples=live,flow_contexts=contexts,interpretation='Native enabled bank0 event10 (LDC EOF), before unchanged ACK. Each frame stores the HW-time LDC owner; every LDC event compares that owner and the native callback slot. Count0..15 is exact; saturation means16-or-more. HW/final repeat fields are lower bounds2-or-more. A split requires one ordered hit, owner agreement, the selected typed native LDC operation and a typed stable route pin in the same conservatively bounded mode/source run. Sparse type pins do not prove uninterrupted lifetime, per-buffer identity, hardware-latched times or pure hardware busy duration. Multiple/missing/changed/unbound records remain raw evidence, not usable split timings.'))
def context_runs(path):
 from analyze_video_counters import HEADER
 data=path.read_bytes();h=HEADER.unpack_from(data);start=h[7];runs=[]
 for i in range(h[5]):
  off=h[2]+h[3]*i;w=struct.unpack_from('<320I',data,off);beg,end=struct.unpack_from('<2Q',data,off);valid=w[7]
  key=(hex(w[34]),hex(w[36]),w[72],hex(w[90]),w[67],w[257]if w[256]&1 else None,w[258]if w[256]&1 else None)if valid&16404==16404 and not valid&0x80000000 else None
  if not runs or runs[-1]['key']!=key:runs.append(dict(key=key,begin=(beg-start)/1e9,end=(end-start)/1e9))
  else:runs[-1]['end']=(end-start)/1e9
 return start,runs
def associate_routes(flow,routes,started_ns,runs):
 if routes and routes.get('schema')=='NXL_LDC_ROUTE4':
  flow['interpretation']=flow['interpretation'].replace('the selected typed native LDC operation','the registered typed native notification object and its canonical member')
 pins=routes.get('records',[])if routes and routes.get('schema')in ['NXL_LDC_ROUTE3','NXL_LDC_ROUTE4']else[]
 for r in flow['records']:
  r['object_association_valid']=False;r['valid_for_LDC_statistics']=r['valid_for_EP_statistics']=False
  bounds=r.get('callback_publish_linux_bounds')
  if not r['native_owner_candidate']or not bounds or bounds['earliest_linux_ns']is None:continue
  lo=(bounds['earliest_linux_ns']-started_ns)/1e9;hi=(bounds['latest_linux_ns']-started_ns)/1e9
  for run in runs:
   k=run['key']
   if k is None or not(run['begin']+1<=lo<=hi<=run['end']-1)or k[0]!=r['driver_mode']or k[1]!=r['IPC_preset']:continue
   selected=[p for p in pins if p['object_binding_valid']and p['timer_schema']==9 and p['driver']==k[0]and p['preset']==k[1]and p['source_state']==k[2]and p['backend']==k[3]and run['begin']<=p['elapsed_s']<=run['end']and p['LDC_owner']==r['HW_LDC_owner']and(routes.get('schema')!='NXL_LDC_ROUTE4'or p['source_pointer']==hex(k[4]))]
   if selected:
    if routes.get('schema')=='NXL_LDC_ROUTE4':r['association_basis']=selected[0]['association_basis']
    r.update(object_association_valid=True,object_binding_generation=selected[0]['generation'],source_state=k[2],active_backend=k[3]);r['valid_for_LDC_statistics']=r['valid_for_EP_statistics']=r['valid_LDC_interval_candidate'];break
 return summarize(flow)
def compare_flows(flow):
 groups=collections.defaultdict(list)
 for r in flow['records']:groups[(r['driver_mode'],r['IPC_preset'],r['source_state'])].append(r)
 out=[]
 for key,rows in groups.items():
  good=[r for r in rows if r['valid_for_statistics']];metrics={}
  for n in ['hardware_to_IRQ_ms','IRQ_to_callback_ms','cycle_ms',*SPLITS]:
   v=[r[n]for r in good if n in r and(n not in SPLITS or r['valid_for_LDC_statistics'])]
   if v:metrics[n]=metric(v)
  out.append(dict(driver_mode=key[0],IPC_preset=key[1],source_state=key[2],frames=len(good),statistics=metrics,LDC_count_histogram=dict(collections.Counter(str(r['LDC_count_min'])+('+'if r['LDC_count_saturated']else'')for r in rows))))
 return dict(groups=out,scope='Split timings already require typed route binding and1s mode/source transition exclusion; raw HW/IRQ metrics retain their own sample context.')
