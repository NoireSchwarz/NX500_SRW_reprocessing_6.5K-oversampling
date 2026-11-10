"""Offline decode of R9v passive video evidence. Python3 standard library only.
Raw interrupt rates never become frame-rate claims. No camera access or writes.
"""
from pathlib import Path
import argparse,csv,json,statistics,struct
HEADER=struct.Struct('<8s6I6Q8I4I4Q256s160s32sI4x')
IRQ_NAMES=['SSIF','PP','IPC','IPC_DMA','EP_A','EP_B','EP_DMA','ISP_CTRL','JPEG','HEVC','SRP','IPCC0','IPCC1','DRAM_A_PERF','DRAM_B_PERF','AXIERR']
def fnv(b):
 h=2166136261
 for v in b:h=((h^v)*16777619)&0xffffffff
 return h
def text(b):return b.split(b'\0',1)[0].decode('utf8','strict')
def difference(before,after,bits=32):
 if before==(1<<bits)-1 or after==(1<<bits)-1:return None
 if after>=before:return after-before
 if before>(1<<bits)*15//16 and after<(1<<bits)//16:return (after-before)%(1<<bits)
 return None

def decode_paths(data):
 """Optional R9y extension; older v1 captures have zero padding here."""
 offset=1024;ring=data[offset:offset+2848]
 if ring[:8]==bytes(8):return None
 if ring[:8]!=b'NXLPATH1':raise ValueError('Unknown path snapshot extension')
 magic,schema,size,capacity,total,res0,res1=struct.unpack_from('<8s6I',ring)
 if schema not in (1,2) or size!=256 or capacity not in (3,11) or res0 or res1:raise ValueError('Invalid path ring metadata')
 entries=[]
 for seq in range(max(0,total-capacity),total):
  slot=seq%capacity
  raw=ring[32+slot*size:32+(slot+1)*size];w=struct.unpack('<64I',raw)
  if w[0]!=seq or w[63]!=(fnv(raw[:252])^0x75b3a129):raise ValueError('Path snapshot integrity failed')
  flags=w[4];changed=bool(flags&0x80000000)
  row=dict(sequence=seq,elapsed_s=w[1]/1000,driver_mode=hex(w[2]),IPC_preset=hex(w[3]),flags=hex(flags),configuration_changed_during_read=changed,map_word=hex(w[5]),raw_words=list(w[:63]))
  if flags&1:row['IPC_parent_state']=w[8];row['active_backend']=hex(w[6]);row['active_backend_vtable']=hex(w[7]);row['parent_descriptor_mode']=hex(w[9]);row['parent_descriptor_ports_word']=hex(w[10]);row['source_image_pointer']=hex(w[11]);row['destination_image_pointer']=hex(w[12])
  if flags&2:row['native_IPC_global_pointer']=hex(w[13]);row['native_IPC_global_words']=list(w[14:28])
  def img(words):
   names=['vtable','global_width','global_height','active_width','active_height','crop_width','crop_height','tile_width','tile_height','overlap_x','overlap_y','origin_x']
   d=dict(zip(names,words[:12]));d['vtable']=hex(d['vtable']);d['raw_words']=list(words)
   if len(words)>=15:d.update(origin_y=words[12],aspect_width=words[13],aspect_height=words[14])
   if len(words)>=19:d.update(native_flags_word=hex(words[15]),native_format_word=hex(words[16]),native_pack_word=hex(words[17]),native_stride_word=words[18])
   return d
  if flags&4:row['source_image']=img(w[28:48])
  if flags&8:row['destination_image']=img(w[48:60])
  if flags&16:row['LDC_backend_state']=w[60]
  if flags&32:row['resize0_backend_state']=w[61]
  if schema>=2:
   if flags&64:row['LDC_recipe_policy']=dict(UD_flag=w[62]&255,QHD_flag=(w[62]>>8)&255)
   if flags&128:row['LDC_op_mode_bytes']=dict(offset_1d=(w[62]>>16)&255,offset_1e=(w[62]>>24)&255)
  entries.append(row)
 return dict(schema='NXL_PATH_SNAPSHOTS'+str(schema),total_captured=total,retained=len(entries),capacity=capacity,records=entries,interpretation='Native RAM snapshots only, once per second in E005. A changed-during-read flag excludes cross-field coherence. Backend state numbers and raw descriptor words are not hardware completion events.')
def decode_clocks(data):
 paths=decode_paths(data)
 offset=1024+32+256*paths['capacity'] if paths is not None else 1024+2848;ring=data[offset:4096]
 if ring[:8]==bytes(8):return None
 if ring[:8]!=b'NXLCLK1\0':raise ValueError('Unknown clock snapshot extension')
 magic,schema,size,capacity,total,res0,res1=struct.unpack_from('<8s6I',ring)
 if (schema,size) not in [(1,56),(2,64),(3,64)] or capacity!=3 or res1 or (res0 and (schema!=3 or res0!=1)):raise ValueError('Invalid clock ring metadata')
 entries=[]
 indices=([0]+list(range(max(1,total-2),total)))if schema==3 and res0 and total else range(max(0,total-capacity),total)
 for seq in indices:
  slot=1+(seq-1)%2 if schema==3 and res0 and seq else seq%capacity
  raw=ring[32+slot*size:32+(slot+1)*size];w=struct.unpack('<%dI'%(size//4),raw)
  if w[0]!=seq or w[-1]!=(fnv(raw[:-4])^0x6039acb7):raise ValueError('Clock snapshot integrity failed')
  row=dict(sequence=w[0],elapsed_s=w[1]/1000,status=w[2],scene=hex(w[3]),selector_words=[hex(x)for x in w[4:13]])
  if schema>=2:
   row.update(DDR_A_readback_Hz=w[13],DDR_readback_status=w[14],DDR_source='NX500 native ch_a sysfs live PLL/divider getter')
   if w[14]==1:row['DDR_A_readback_MHz']=w[13]/1e6
  if schema==3:row['phase']='preview-before-recording'if res0 and seq==0 else'recording-progress'
  entries.append(row)
 return dict(schema='NXL_CLOCK_SNAPSHOTS'+str(schema),total_captured=total,capacity=capacity,records=entries,interpretation='CCU words are selectors. R10c DDR_A Hz is the native live selector/divider readback, sampled twice; status 1 matching, 2 changed, 3 unavailable. It is neither MBUS frequency nor memory bandwidth or data transfer MT/s. No clocks are written.')

def decode_latency_legacy(records,started_ns):
 frames={};discarded=0;resets=0;generation=0;last_seq=None;restarts=0;pending_tick=None;pending_since=0;pending_max=0
 for s in records:
  if not s['valid']&65536:continue
  w=s['latency']
  if len(w)!=48 or w[:2]!=[0x31544c56,1]:raise ValueError('Invalid timing sample metadata')
  seq=w[2]
  if last_seq is not None and seq<last_seq:generation+=1;resets+=1
  last_seq=seq
  restarts=max(restarts,w[5])
  if w[3]:
   if pending_tick!=w[4]:pending_tick=w[4];pending_since=s['t']
   pending_max=max(pending_max,(s['t']-pending_since)/1e9)
  else:pending_tick=None
  for i in range(8):
   row=w[8+i*5:13+i*5];n,start,end,dt,owner=row
   if not n:continue
   if n>seq or n+7<seq or ((end-start)&0xffffffff)!=dt or dt>52_000_000:discarded+=1;continue
   key=(generation,n);value=dict(generation=generation,sequence=n,IPC_start_tick=start,EP_done_tick=end,elapsed_ticks=dt,IPC_to_EP_ms=dt/26000,owner=hex(owner),observed_at_s=(s['t']-started_ns)/1e9,driver_mode=hex(s['rtos'][0]),recording_epoch=s['epoch'])
   if key in frames:
    before=frames[key]
    if any(before[k]!=value[k]for k in ['IPC_start_tick','EP_done_tick','elapsed_ticks','owner']):raise ValueError('Conflicting frame timing sequence')
   else:frames[key]=value
 ordered=sorted(frames.values(),key=lambda x:(x['generation'],x['sequence']));gaps=0
 for a,b in zip(ordered,ordered[1:]):
  if a['generation']==b['generation']:
   gaps+=max(0,b['sequence']-a['sequence']-1)
   if b['sequence']==a['sequence']+1:
    dt=(b['IPC_start_tick']-a['IPC_start_tick'])&0xffffffff
    if dt<=52_000_000:b['IPC_launch_interval_ms']=dt/26000
 values=sorted(x['IPC_to_EP_ms']for x in ordered)
 summary=dict(clock_Hz=26_000_000,clock='native CMDQ free-running low tick',ring_capacity=8,retained_frames=len(ordered),missed_frame_sequences=gaps,invalid_or_inflight_entries_discarded=discarded,producer_restarts=resets,overlapping_or_restarted_IPC_requests=restarts,longest_observed_same_pending_request_s=pending_max,records=ordered,interpretation='Paired IPC launch marker to original MtoO successful EP notification event 2, including IPC/LDC/EP and notification delay; not HEVC encode time. Not IRQ-count timing. Eight-frame ring may overwrite during sampling delays; gaps are reported. RTOS code/caches add a small unmeasured hardware cost; Linux CPU cost excludes RTOS instructions.')
 if values:summary['IPC_to_EP_ms']=dict(min=min(values),median=statistics.median(values),p95=values[min(len(values)-1,int(len(values)*0.95))],max=max(values))
 return summary
def decode_latency(records,started_ns):
 if any(x['valid']&65536 and x['latency'][:2]==[0x33544c56,4] for x in records):
  from video_frame_flow import decode_frame_flow
  return decode_frame_flow(records,started_ns)
 if any(x['valid']&65536 and x['latency'][:2]==[0x33544c56,3] for x in records):
  from video_hw_timing import decode_hw_timing
  return decode_hw_timing(records,started_ns)
 if any(len(x['latency'])==64 for x in records):
  from video_stage_timing import decode_stage_timing
  return decode_stage_timing(records,started_ns)
 return decode_latency_legacy(records,started_ns)

def decode(path):
 data=Path(path).read_bytes()
 if len(data)<4096:raise ValueError('Incomplete header')
 h=HEADER.unpack_from(data);magic,schema,hbytes,rbytes,capacity,count,phase=h[:7]
 if (schema,rbytes)not in [(1,512),(2,704),(3,768),(4,896),(5,1024),(6,1152)]or (magic,hbytes,capacity,phase)!=(b'NXLVCTR1',16384 if schema==6 else 8192 if schema==5 else 4096,6000,6)or count>capacity or len(data)!=hbytes+count*rbytes:raise ValueError('Unknown schema, unsaved or incorrect file size')
 if h[-1]!=fnv(data[:608]):raise ValueError('Header integrity failed')
 records=[]
 for i in range(count):
  raw=data[hbytes+i*rbytes:hbytes+(i+1)*rbytes];v=struct.unpack('<%dI'%(rbytes//4),raw);times=struct.unpack_from('<3Q',raw)
  if v[6]!=i or v[-1]!=(fnv(raw[:-4])^0x91d6037b)or times[1]<times[0]:raise ValueError('Invalid sample '+str(i))
  records.append(dict(t=times[1],start=times[0],cpu=times[2],sequence=v[6],valid=v[7],epoch=v[8],period_ms=v[9],read_calls=v[10],read_bytes=v[11],mark=v[12],backend_selector=v[13],irq=v[14:30],pp=v[30:34],rtos=v[34:38],policy=v[38:47],camera=v[47:67],source=v[67:73],encoder=v[73:81],hevc=v[81:91],queue=[v[91+j*6:97+j*6]for j in range(5)],route=v[121:124],policy_index=v[124],CPU_kernel_kHz=v[125],CPU_governor=v[126],latency=list(v[127:191]if schema>=3 else v[127:-1])if schema>=2 else [],completion=list(v[191:223])if schema>=4 else [],progress=list(v[223:255])if schema>=5 else [],flow=list(v[255:287])if schema==6 else []))
 intervals=[];issues=[]
 for a,b in zip(records,records[1:]):
  dt=(b['t']-a['t'])/1e9
  if dt<=0:issues.append(dict(sample=b['sequence'],reason='non-monotonic time'));continue
  row=dict(sample=b['sequence'],seconds=(b['t']-h[7])/1e9,interval_s=dt,epoch=b['epoch'],period_ms=b['period_ms'],mark=b['mark'],valid=b['valid'],driver_mode=b['rtos'][0],scene_id=b['rtos'][1],IPC_preset=hex(b['rtos'][2]),map_word=hex(b['rtos'][3]),sample_wall_ms=(b['t']-b['start'])/1e6,reader_calls=b['read_calls'],reader_bytes=b['read_bytes'],process_CPU_percent=(b['cpu']-a['cpu'])/(b['t']-a['t'])*100)
  if schema>=4 and b['valid']&131072:
   from video_completion import PHASES
   from video_hardware_progress import QOS_PHASES
   row['comparison_phase']=(QOS_PHASES if schema>=5 else PHASES).get(b['completion'][29],'unknown');row['comparison_result']=b['completion'][30]
   if schema>=5:
    row['HEVC_bridge_requested']=hex(b['completion'][31])
    z=b['progress']
    if b['valid']&262144 and z[1]&1 and z[1]&(1<<14) and z[1]&(1<<22) and z[16]==z[24]:row['HEVC_bridge_readback']=hex(z[16])
  stable=a['epoch']==b['epoch'] and not(a['valid']&0x80000000 or b['valid']&0x80000000)
  if b['CPU_kernel_kHz']:row['CPU_kernel_reported_kHz']=b['CPU_kernel_kHz']
  if 1<=b['CPU_governor']<=6:row['CPU_governor']=['interactive','performance','ondemand','powersave','userspace','conservative'][b['CPU_governor']-1]
  if b['valid']&128:
   for i,name in enumerate(['IPC_init_function','IPC_update_function','IPC_setup_function']):row[name]=hex(b['route'][i])
  if b['valid']&16384:row['active_IPC_backend_vtable']=hex(b['hevc'][9]);row['IPC_method_scope']='active native backend'
  if b['valid']&32768:row['IPC_setup_selector']=hex(b['backend_selector'])
  if b['valid']&4:
   row['policy_index']=b['policy_index']
   for i,name in enumerate(['pp','pp_dma','ipc_a','ipc_b','ipc_dma','ep_a','ep_b','dp','mbus']):row['requested_'+name+'_selector']=hex(b['policy'][i])
  def rate(key,flag,group,index,bits=32):
   if not stable or a['valid']&flag!=flag or b['valid']&flag!=flag:return
   before=a[group][index];after=b[group][index]
   if bits==64:before|=a[group][index+1]<<32;after|=b[group][index+1]<<32
   d=difference(before,after,bits)
   if d is None or d>10_000_000*dt+100:issues.append(dict(sample=b['sequence'],counter=key,reason='reset, unknown or implausible jump'));return
   row[key]=d/dt
  for i,n in enumerate(IRQ_NAMES):rate('IRQ_'+n+'_per_s',1,'irq',i)
  for i,n in enumerate(['PP_EOF','PP_WDMA','SSIF_SOF','SSIF_EOF']):rate(n+'_per_s',2,'pp',i)
  for key,index in [('MMF_new',4),('MMF_added',6),('MMF_adjust',8)]:rate(key+'_per_s',8,'camera',index,64)
  for key,index in [('sink_enter',10),('sink_return',11)]:rate(key+'_per_s',8,'camera',index)
  for key,index in [('source_entries',1),('source_forwarded',2)]:rate(key+'_per_s',16,'source',index)
  rate('encoder_process_attempts_per_s',32,'encoder',1);rate('HEVC_successful_outputs_per_s',64,'hevc',1)
  if b['valid']&8:row['sink_outstanding_raw']=(b['camera'][10]-b['camera'][11])&0xffffffff
  if b['valid']&64:row['HEVC_raw_11c']=b['hevc'][2];row['HEVC_codec_state']=b['hevc'][3]
  for i,n in enumerate(['source0','source1','source2','encoder_input','encoder_output']):
   if not b['valid']&(256<<i):continue
   q=b['queue'][i]
   row[n+'_full']=q[2];row[n+'_free']=q[3];row[n+'_capacity']=q[4];row[n+'_race_flag']=bool(q[5]&2)
  intervals.append(row)
 elapsed=(h[8]-h[7])/1e9;cpu=(h[10]-h[9])/1e9
 stats={}
 for name in ['reader_calls','reader_bytes','sample_wall_ms']:
  values=[v[name]for v in intervals]
  if values:stats[name]=dict(median=statistics.median(values),maximum=max(values))
 summary=dict(schema='R9v_VIDEO_COUNTER_SUMMARY1',file=str(Path(path).resolve()),samples=count,report=text(h[-4]),revision=text(h[-2]),elapsed_sampling_s=elapsed,sampling_process_CPU_s=cpu,sampling_process_CPU_percent=100*cpu/elapsed if elapsed>0 else None,last_period_ms=h[19],reason_flags=hex(h[20]),max_sample_ms=h[25]/1e6,deadline_misses=h[27],epochs=sorted({v['epoch']for v in records}),marks=sorted({v['mark']for v in records}),reader_cost=stats,issues=issues,
  interpretation=['IRQ rates are raw interrupt totals per second, not frames. IPC/SRA interrupt scaling needs matched full/crop/QHD controls.','MMF new/added and source forwarding describe delivery; persistent sink debt or full encoder input queue can indicate downstream backpressure.','HEVC outputs are successful native encode returns. Compare queue pressure and upstream delivery before locating a bottleneck.','Clock policy words are requested selectors. No measured clock or automatic attribution is claimed.','CPU is measured by the collector process during sampling; arming hashes and post-stop SD export are excluded.'],hardware_measurement_source='This file only; synthetic validation files are not camera measurements.')
 paths=decode_paths(data)
 if paths is not None:summary['path_snapshots']=paths
 clocks=decode_clocks(data)
 if clocks is not None:summary['clock_snapshots']=clocks
 from video_qos import decode_qos
 summary['qos_snapshots']=decode_qos(data)
 from video_bus import decode_bus
 summary['bus_snapshots']=decode_bus(data)
 if schema>=2:summary['frame_timing']=decode_latency(records,h[7])
 if schema>=4:
  from video_completion import decode_compare,decode_completion,compare_intervals
  if schema==4:
   summary['CPU_comparison']=decode_compare(data);summary['hardware_completion']=decode_completion(records,h[7]);summary['recording_AB_intervals']=compare_intervals(intervals,summary['CPU_comparison']['original_governor'])
  else:
   from video_hardware_progress import decode_qos_compare,decode_progress,decode_phases,QOS_PHASES,compare_qos_intervals
   if schema==6:
    from video_frame_flow import compare_flows
    summary['flow_bookmarks']=decode_qos_compare(data);summary['flow_bookmarks']['schema']='NXL_FLOW_BOOKMARKS1';summary['flow_bookmarks']['scope']='Bookmarks only. R10j makes no bridge/CPU/clock writes.'
    summary['hardware_progress']=decode_progress(records,h[7]);summary['pinned_hardware_phases']=decode_phases(data)
    summary['frame_flow_comparison']=compare_flows(summary['frame_timing'])
   else:
    summary['HEVC_bridge_comparison']=decode_qos_compare(data);summary['hardware_progress']=decode_progress(records,h[7]);summary['pinned_hardware_phases']=decode_phases(data)
    summary['recording_AB_intervals']=compare_qos_intervals(intervals,summary['HEVC_bridge_comparison']['original_bridge'])

   summary['hardware_completion']=decode_completion(records,h[7])
   for row in summary['hardware_completion']['records']:
    if row['phase']=='B-performance':row['phase']='B-bridge-qos'
    row.pop('governor',None)
   for group in summary['hardware_completion']['by_phase']:
    if group['phase']=='B-performance':group['phase']='B-bridge-qos'
 return summary,intervals
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('counts',type=Path);p.add_argument('--out',type=Path);a=p.parse_args();summary,rows=decode(a.counts)
 if a.out:
  a.out.mkdir(parents=True,exist_ok=True);(a.out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n')
  frames=summary.get('frame_timing',{}).get('records',[])
  if frames:
   columns=list(dict.fromkeys(k for row in frames for k in row))
   with(a.out/'frame-timing.csv').open('w',encoding='utf8',newline='')as f:
    writer=csv.DictWriter(f,fieldnames=columns);writer.writeheader();writer.writerows(frames)
  completion=summary.get('hardware_completion',{}).get('records',[])
  if completion:
   with(a.out/'hardware-completion.csv').open('w',encoding='utf8',newline='')as f:
    writer=csv.DictWriter(f,fieldnames=list(completion[0]));writer.writeheader();writer.writerows(completion)
  columns=list(dict.fromkeys(key for row in rows for key in row))
  with(a.out/'intervals.csv').open('w',encoding='utf8',newline='')as f:
   writer=csv.DictWriter(f,fieldnames=columns);writer.writeheader();writer.writerows(rows)
 print(json.dumps(summary,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
