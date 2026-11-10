"""Offline R10d native callback phase decoder. Standard library only."""
import statistics
BITS={'start':1,'IPC_complete_callback':2,'EP_complete_notification':4,'source_notification':8}
def decode_stage_timing(samples,started_ns):
 frames={};live=[];generation=0;previous=None;discarded=0;pending_since=0;pending_tick=None;max_pending=0
 for s in samples:
  if not s['valid']&65536:continue
  w=s['latency']
  if len(w)!=64 or w[:2]!=[0x32544c56,2]:raise ValueError('Invalid R10d phase header')
  seq=w[2]
  if previous is not None and seq<previous:generation+=1
  previous=seq
  if w[3]&1:
   if pending_tick!=w[4]:pending_since=s['t'];pending_tick=w[4]
   max_pending=max(max_pending,(s['t']-pending_since)/1e9)
  else:pending_tick=None
  live.append(dict(observed_at_s=(s['t']-started_ns)/1e9,completed_sequence=seq,mask=hex(w[3]),start_tick=w[4],IPC_complete_tick=w[5],EP_complete_tick=w[6],source_note_tick=w[7],missing=[n for n,b in BITS.items()if not w[3]&b]))
  for i in range(8):
   row=list(w[8+i*7:15+i*7]);n,start,ipc,ep,src,nxt,flags=row
   if not n:continue
   if n>seq or n+7<seq or not flags&1:discarded+=1;continue
   key=(generation,n)
   if key in frames:
    if frames[key]['raw_words']!=row:raise ValueError('Conflicting committed phase record')
    continue
   delta=lambda tick:((tick-start)&0xffffffff)/26000
   cycle=delta(nxt);cycle_valid=0<cycle<=2000
   times={}
   for name,bit,tick in [('IPC_callback',2,ipc),('EP',4,ep),('source',8,src)]:
    if flags&bit:times[name]=delta(tick)
   problems=[]
   if not cycle_valid:problems.append('cycle-out-of-range')
   if flags&16:problems.append('EP-error')
   for bit,name in [(32,'repeated-IPC-callback'),(64,'repeated-EP-note'),(128,'repeated-source-note'),(256,'owner-mismatch')]:
    if flags&bit:problems.append(name)
   for name,t in times.items():
    if not cycle_valid or t>cycle:problems.append(name+'-outside-frame-cycle')
   def gate(shift):
    v=(flags>>shift)&7
    return dict(running=bool(v&1),source_ready=bool(v&2),EP_ready=bool(v&4))
   r=dict(generation=generation,sequence=n,observed_at_s=(s['t']-started_ns)/1e9,driver_mode=hex(s['rtos'][0]),recording_epoch=s['epoch'],raw_words=row,mask=hex(flags),cycle_ms=cycle,cycle_valid=cycle_valid,EP_status_byte=(flags>>24)&255,missing_events=[name for name,bit in BITS.items()if not flags&bit],problems=problems)
   if flags&4:r['gate_before_EP']=gate(16)
   if flags&8:r['gate_before_source']=gate(20)
   # Keep raw event ordering, including a callback routed after another stage.
   for name,t in times.items():r['from_start_to_'+name+'_ms']=t
   usable=cycle_valid and not flags&(16|256) and all(t<=cycle for t in times.values())
   if usable:
    r['valid_cycle_for_statistics']=True
    if flags&2 and not flags&32:r['IPC_ms']=times['IPC_callback']
    if flags&4 and not flags&64:r['IPC_launch_to_EP_ms']=times['EP']
    if flags&6==6 and not flags&(32|64):r['IPC_callback_to_EP_ms']=times['EP']-times['IPC_callback']
    if flags&12==12 and not flags&(64|128):
     r['EP_to_source_signed_ms']=times['source']-times['EP']
     r['wait_for_source_after_EP_ms']=max(0,times['source']-times['EP'])
     r['both_ready_to_next_launch_ms']=cycle-max(times['source'],times['EP'])
     r['last_required_event']='EP'if times['EP']>=times['source']else'source'
   else:r['valid_cycle_for_statistics']=False
   frames[key]=r
 ordered=sorted(frames.values(),key=lambda x:(x['generation'],x['sequence']));gaps=sum(max(0,b['sequence']-a['sequence']-1)for a,b in zip(ordered,ordered[1:])if a['generation']==b['generation'])
 stats={}
 for key in ['cycle_ms','IPC_ms','IPC_launch_to_EP_ms','IPC_callback_to_EP_ms','EP_to_source_signed_ms','wait_for_source_after_EP_ms','both_ready_to_next_launch_ms']:
  values=sorted(x[key]for x in ordered if key in x and x['valid_cycle_for_statistics'])
  if values:stats[key]=dict(count=len(values),min=min(values),median=statistics.median(values),p95=values[min(len(values)-1,int(len(values)*.95))],max=max(values))
 return dict(schema='NXL_STAGE_TIMING2',clock_Hz=26000000,clock_read_protocol='native high 0x06002060 then low 0x06002064',ring_capacity=8,retained_frames=len(ordered),missed_frame_sequences=gaps,invalid_or_inflight_entries_discarded=discarded,producer_restarts=generation,longest_observed_same_pending_request_s=max_pending,statistics=stats,records=ordered,live_samples=live,interpretation='Original software callback arrival times, not hardware IRQ-cycle boundaries. Each row is published at the next native IPC launch. Source/EP pre-notification gate flags preserve native values. Sequence gaps are recorder overwrite, not camera frame loss. Fixed DDR frequency does not measure bandwidth utilization; split delays narrow contention versus native notification/buffer wait without alone proving causality.')
