"""Offline R10g native trigger/IRQ/callback decoder. Standard library only."""
import statistics
def decode_hw_timing(samples,started_ns):
 frames={};live=[];previous=None;generation=0;discarded=0
 for s in samples:
  if not s['valid']&65536:continue
  w=s['latency']
  if len(w)!=64 or w[:2]!=[0x33544c56,3]:raise ValueError('Invalid R10g hardware timing header')
  seq=w[2]
  if previous is not None and seq<previous:generation+=1
  previous=seq
  live.append(dict(observed_at_s=(s['t']-started_ns)/1e9,completed_sequence=seq,flags=hex(w[3]),request=w[4],hardware_start=w[5],last_IRQ=w[6],callback=w[7]))
  for slot in range(8):
   row=list(w[8+slot*7:15+slot*7]);n,request,hw,irq,cb,nxt,flags=row
   if not n:continue
   if n>seq or n+7<seq or not flags&1:discarded+=1;continue
   key=(generation,n)
   if key in frames:
    if frames[key]['raw_words']!=row:raise ValueError('Conflicting committed hardware timing record')
    continue
   elapsed=lambda x:((x-request)&0xffffffff)/26000
   cycle=elapsed(nxt);missing=[name for name,bit in [('request',1),('hardware-start',2),('CTRL0-CS0-frame-done-IRQ',4),('callback',8)]if not flags&bit]
   problems=[]
   for bit,name in [(32,'duplicate-callback'),(64,'frame-done-IRQ-after-callback'),(128,'IRQ-error-bit'),(1<<30,'IRQ-counter-saturated'),(1<<31,'hardware-counter-saturated')]:
    if flags&bit:problems.append(name)
   times={k:elapsed(t)for k,t,bit in [('hardware',hw,2),('IRQ',irq,4),('callback',cb,8)]if flags&bit}
   if not 0<cycle<=2000 or any(t>cycle for t in times.values()):problems.append('timestamp-outside-cycle')
   r=dict(generation=generation,sequence=n,observed_at_s=(s['t']-started_ns)/1e9,recording_epoch=s['epoch'],driver_mode=hex(s['rtos'][0]),raw_words=row,flags=hex(flags),controller_type=(flags>>24)&3,IRQ_count=(flags>>8)&255,hardware_start_count=(flags>>16)&255,cycle_ms=cycle,missing_events=missing,problems=problems,valid_for_statistics=False)
   if len(s.get('completion',[]))==32 and s['valid']&131072:
    r.update(comparison_phase=s['completion'][29],comparison_result=s['completion'][30],comparison_mark=s['mark'],comparison_transition_scope='First observed sample; exclude at least 1 s after each change because the ring can contain frames from the preceding phase.')
   for k,v in times.items():r['request_to_'+k+'_ms']=v
   if not missing and not problems:
    if not 0<=times['hardware']<=times['IRQ']<=times['callback']<=cycle:problems.append('event-order-invalid')
    elif not r['IRQ_count']or not r['hardware_start_count']:problems.append('event-count-missing')
    else:r.update(valid_for_statistics=True,request_to_hardware_ms=times['hardware'],hardware_to_IRQ_ms=times['IRQ']-times['hardware'],IRQ_to_callback_ms=times['callback']-times['IRQ'],callback_to_next_request_ms=cycle-times['callback'])
   frames[key]=r
 ordered=sorted(frames.values(),key=lambda x:(x['generation'],x['sequence']));stats={}
 for k in ['cycle_ms','request_to_hardware_ms','hardware_to_IRQ_ms','IRQ_to_callback_ms','callback_to_next_request_ms']:
  v=sorted(x[k]for x in ordered if x['valid_for_statistics'])
  if v:stats[k]=dict(n=len(v),min=min(v),median=statistics.median(v),mean=statistics.mean(v),p95=v[min(len(v)-1,int(len(v)*.95))],max=max(v))
 gaps=sum(max(0,b['sequence']-a['sequence']-1)for a,b in zip(ordered,ordered[1:])if a['generation']==b['generation'])
 return dict(schema='NXL_HW_TIMING3',clock_Hz=26000000,retained_frames=len(ordered),valid_frames=sum(x['valid_for_statistics']for x in ordered),missed_frame_sequences=gaps,invalid_or_inflight_entries_discarded=discarded,statistics=stats,records=ordered,live_samples=live,interpretation='Hardware start is timestamped after the original trigger STR and DSB; IRQ is the latest IRQ44 entry observing CTRL0_CS0_FRAME_DONE before native acknowledge and before the qualified callback. Hardware-to-IRQ includes physical processing plus interrupt-entry latency, not a pure DDR bandwidth measurement. Counts expose repeated block events. Gaps are recorder overwrite, not camera drops. R10e source/EP gating was measured separately; this revision focuses its bounded ring on IPC internals.')
