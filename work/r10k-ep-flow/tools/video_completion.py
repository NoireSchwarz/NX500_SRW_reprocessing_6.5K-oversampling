"""R10h sparse read-only hardware completion and reversible CPU comparison."""
import struct,collections,statistics

def fnv(b):
 h=2166136261
 for v in b:h=((h^v)*16777619)&0xffffffff
 return h

GOVERNORS=['unknown','interactive','performance','ondemand','powersave','userspace','conservative']
PHASES={1:'A-original',2:'B-performance',3:'unavailable',4:'restore-required',5:'externally-changed'}

def decode_compare(data):
 raw=data[3968:4096]
 if raw[:8]!=b'NXLCPU1\0':raise ValueError('Unknown CPU comparison metadata')
 w=struct.unpack('<32I',raw)
 if w[2:4]!=(1,128)or w[-1]!=(fnv(raw[:-4])^0x73a19c46)or w[5]not in PHASES:raise ValueError('CPU comparison integrity failed')
 events=[]
 for i in range(3):
  seq,request,phase,result,ms=w[16+i*5:21+i*5]
  if not seq:continue
  if phase not in PHASES or not 1<=result<=8:raise ValueError('Invalid CPU comparison event')
  events.append(dict(sequence=seq,request=request,phase=PHASES[phase],result=result,monotonic_ms_low32=ms))
 return dict(schema='NXL_CPU_COMPARISON1',phase=PHASES[w[5]],original_governor=GOVERNORS[w[6]]if w[6]<7 else 'unknown',current_governor=GOVERNORS[w[7]]if w[7]<7 else 'unknown',restore_pending=bool(w[8]),last_errno=w[9],last_request=w[10],result=w[11],changes=w[12],verified_restores=w[13],events=sorted(events,key=lambda x:x['sequence']),scope='Opt-in Linux stock CPU governor only. No min/max frequency, voltage, DDR, IRQ-mask, bus-arbitration or RTOS clock writes. This tests DVFS/scheduling coupling; it is not proof of RTOS IRQ priority or a camera-tested fix.')

def decode_completion(samples,started_ns):
 rows=[]
 for s in samples:
  if not s['valid']&131072:continue
  w=s['completion']
  if len(w)!=32 or w[0]!=0x31435056:raise ValueError('Invalid hardware completion record')
  flags=w[1];before=w[2:10];after=w[13:21]
  if flags&1 and not flags&2:kind='cross-request-or-changing-header'
  elif not flags&1:kind='unavailable'
  elif flags&8:
   kind='completion-latched-before-tracked-IRQ'if flags&16 and flags&32 else 'completion-latched-IRQ-disabled'if not flags&16 else 'completion-latched-handler-mismatch'
  elif flags&512:kind='IRQ-already-tracked'
  elif flags&4:kind='completion-not-observed-pending'
  else:kind='outside-qualified-hardware-request'
  first=w[21]|w[22]<<32;last=w[23]|w[24]<<32
  if w[26]>2 or w[27]>3:raise ValueError('Unbounded completion followup count')
  if flags&64 and (not flags&8 or w[27]<2 or last<first or w[28]!=min(0xffffffff,last-first)):raise ValueError('Invalid completion persistence bound')
  rows.append(dict(sample=s['sequence'],observed_at_s=(s['t']-started_ns)/1e9,mark=s['mark'],phase=PHASES.get(w[29],'unknown'),comparison_result=w[30],governor=GOVERNORS[w[31]]if w[31]<7 else 'unknown',classification=kind,flags=hex(flags),native_sequence_before=before[2],request_tick=before[4]if before[1]!=4 else None,frame_start_tick=before[4]if before[1]==4 else None,hardware_start_tick=before[5],native_flags_before=hex(before[3]),native_sequence_after=after[2],native_flags_after=hex(after[3]),raw_status_first=hex(w[10]),raw_status_last=hex(w[25]),IRQ_enable=hex(w[11]),callback_handler=hex(w[12]),followups=w[26],pending_observations=w[27],observed_pending_lower_ms=w[28]/1e6,first_read_end_ns=first,last_persistent_read_begin_ns=last,context_changed_during_followup=bool(flags&128)))
 groups=[]
 for phase in sorted({r['phase']for r in rows}):
  records=[r for r in rows if r['phase']==phase];pending=[r for r in records if r['classification']=='completion-latched-before-tracked-IRQ']
  groups.append(dict(phase=phase,samples=len(records),classifications=dict(collections.Counter(r['classification']for r in records)),qualified_pending_hits=len(pending),maximum_pending_lower_ms=max((r['observed_pending_lower_ms']for r in pending),default=0)))
 return dict(schema='NXL_COMPLETION1',samples=len(rows),by_phase=groups,records=rows,interpretation='Sparse Linux monotonic read windows, paired against one native request. A latched completion before the tracked IRQ is positive evidence of a completed event awaiting IRQ observation; persistent observations provide a lower bound only. Zero pending hits does not prove hardware/DDR wait or exclude IRQ delay. No shared native timer reads, status acknowledge, mask changes or callbacks are performed. Initial pairing can be valid even when a followup crosses a request; those followups are flagged and not used to extend the bound. Cache visibility and sparse sampling limit conclusions.')

def compare_intervals(intervals,original_governor):
 """Discard transition windows; never compare counters across an A/B change."""
 grouped={};last=None;changed=0
 for r in intervals:
  key=(r.get('comparison_phase'),r['mark'],r.get('CPU_governor'),r.get('HEVC_successful_outputs_per_s',0)>0)
  if key!=last:changed=r['seconds'];last=key
  if r['seconds']-changed<1.0 or key[0]not in ['A-original','B-performance']or not key[3]:continue
  if key[2]!=('performance'if key[0]=='B-performance'else original_governor):continue
  if r.get('comparison_result')!=1:continue
  group=grouped.setdefault(key,[]);group.append(r)
 out=[]
 for key,rows in grouped.items():
  stats={}
  for metric in ['MMF_new_per_s','MMF_added_per_s','source_forwarded_per_s','HEVC_successful_outputs_per_s','process_CPU_percent']:
   v=[r[metric]for r in rows if metric in r]
   if v:stats[metric]=dict(n=len(v),mean=statistics.mean(v),min=min(v),max=max(v))
  out.append(dict(phase=key[0],mark=key[1],governor=key[2],retained_intervals=len(rows),statistics=stats))
 return dict(records=out,transition_exclusion_s=1,interpretation='Only stable phase/mark/governor intervals retained. Match recording windows with HEVC activity and compare corresponding MP4s; preview, camera exposure and scene differences must not be mixed. No automatic claim that CPU performance fixes the hardware bottleneck.')
