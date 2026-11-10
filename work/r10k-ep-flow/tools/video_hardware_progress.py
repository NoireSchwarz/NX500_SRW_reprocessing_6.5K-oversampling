"""R10i passive status vectors and pinned A/B configuration evidence.

Raw native register bits are not called queue depths, DMA beats, or busy %.
The only candidate write is one original bridge word, outside recording; no MPTOP open or ioctl.
"""
import struct,collections
from video_completion import fnv,PHASES
QOS_PHASES=dict(PHASES,**{})
QOS_PHASES[2]='B-bridge-qos'
ADDRESSES=[0x20810418,0x208210c8,0x208210cc,0x208210d0,0x208210d4,0x208210d8,0x2012f138,0x208210e0]

def compare_qos_intervals(intervals,original_bridge):
 grouped={};last=None;since=0
 for r in intervals:
  key=(r.get('comparison_phase'),r['mark'],r.get('HEVC_bridge_readback'),r.get('HEVC_successful_outputs_per_s',0)>0)
  if key!=last:since=r['seconds'];last=key
  if r['seconds']-since<1 or key[0]not in ['A-original','B-bridge-qos']or not key[3]or r.get('comparison_result')!=1:continue
  if key[2]!=('0x14a'if key[0]=='B-bridge-qos'else original_bridge):continue
  grouped.setdefault(key,[]).append(r)
 rows=[]
 for key,rr in grouped.items():
  metrics={}
  for metric in ['MMF_new_per_s','MMF_added_per_s','source_forwarded_per_s','HEVC_successful_outputs_per_s','process_CPU_percent']:
   vv=[r for r in rr if metric in r];seconds=sum(r['interval_s']for r in vv)
   if seconds>0:metrics[metric]=dict(n=len(vv),seconds=seconds,weighted_mean=sum(r[metric]*r['interval_s']for r in vv)/seconds,min=min(r[metric]for r in vv),max=max(r[metric]for r in vv))
  rows.append(dict(phase=key[0],mark=key[1],actual_bridge=key[2],retained_intervals=len(rr),statistics=metrics))
 return dict(records=rows,transition_exclusion_s=1,requires_independent_matching_bridge_readback=True,interpretation='Only stable native HEVC activity, successful A/B state and independently paired bridge readback matching the requested condition. Weighted by interval duration. Exposure, scene, thermal effects and MP4 checks still require matched recording windows; no automatic causal or fix claim.')

def decode_qos_compare(data):
 raw=data[3968:4096];w=struct.unpack('<32I',raw)
 if raw[:8]!=b'NXLQOSAB'or w[2:4]!=(1,128)or w[-1]!=(fnv(raw[:-4])^0x73a19c46)or w[5]not in QOS_PHASES:
  raise ValueError('HEVC bridge comparison integrity failed')
 events=[]
 for i in range(3):
  seq,request,phase,result,ms=w[16+i*5:21+i*5]
  if not seq:continue
  if phase not in QOS_PHASES or not 1<=result<=8:raise ValueError('Invalid bridge comparison event')
  events.append(dict(sequence=seq,request=request,phase=QOS_PHASES[phase],result=result,monotonic_ms_low32=ms))
 return dict(schema='NXL_HEVC_BRIDGE_AB1',phase=QOS_PHASES[w[5]],original_bridge=hex(w[6]),current_bridge=hex(w[7]),restore_pending=bool(w[8]),last_errno=w[9],last_request=w[10],result=w[11],changes=w[12],verified_restores=w[13],events=sorted(events,key=lambda e:e['sequence']),scope='Candidate 0x14A comes from native HEVC decoder initialization, NOT a proved NX1 recording default. Only one guarded 32-bit store at 2012F138 is permitted, with preimage/readback/restore; no MPTOP open or ioctl, avoiding its shared codec-clock backup side effect. No CPU/DDR frequency changes. Effect on encoding contention requires camera A/B results.')

def decode_progress(samples,started_ns):
 rows=[]
 for s in samples:
  if not s['valid']&262144:continue
  w=s['progress']
  if len(w)!=32 or w[0]!=0x33504756 or w[2]!=0x33544c56 or w[3]not in (3,4):raise ValueError('Invalid progress vector')
  flags=w[1];first=(flags>>8)&255;second=(flags>>16)&255
  if flags&1 and w[4:8]!=w[26:30]:raise ValueError('Progress coherence flag mismatch')
  if (first|second)&128 and (not flags&4 or w[30]!=2):raise ValueError('Unselected EP debug read')
  row=dict(sample=s['sequence'],observed_at_s=(s['t']-started_ns)/1e9,mark=s['mark'],phase=QOS_PHASES.get(s['completion'][29],'unknown')if s['completion']else'unknown',coherent=bool(flags&1),inflight=bool(flags&2),native_sequence=w[4],native_flags=hex(w[5]),hardware_start_tick=w[7],native_sequence_after=w[26],native_flags_after=hex(w[27]),read_span_us=w[31],first_valid_mask=hex(first),second_valid_mask=hex(second),EP_debug_selector=None if w[30]==0xffffffff else w[30],first={hex(a):hex(w[10+i])for i,a in enumerate(ADDRESSES)if first>>i&1},second={hex(a):hex(w[18+i])for i,a in enumerate(ADDRESSES)if second>>i&1})
  rows.append(row)
 groups=[]
 for phase,mark in sorted({(r['phase'],r['mark'])for r in rows}):
  rr=[r for r in rows if (r['phase'],r['mark'])==(phase,mark)];stable=[r for r in rr if r['coherent']and r['inflight']]
  groups.append(dict(phase=phase,mark=mark,samples=len(rr),coherent_inflight=len(stable),register_histograms={hex(a):dict(collections.Counter(r['first'][hex(a)]for r in stable if hex(a)in r['first']))for a in ADDRESSES}))
 return dict(schema='NXL_HW_PROGRESS1',samples=len(rows),by_phase=groups,records=rows,interpretation='Two sparse IPC/EP raw IRQ-status vectors bracketed by the native request header. Reads never acknowledge interrupts. E0 is collected only when selector B0 already equals 2 and is unchanged. A zero vector does not prove DDR contention or exclude a short interrupt delay. These are not hardware utilization or per-tile time counters.')

def decode_phases(data):
 ring=data[4096:struct.unpack_from('<I',data,12)[0]]
 if len(ring)not in (4096,12288):raise ValueError('Missing pinned hardware phase extension')
 magic,schema,size,cap,count,overflow,reserved=struct.unpack_from('<8s6I',ring)
 if (magic,schema,size)!=(b'NXLHWP1\0',1,704)or cap not in (5,16)or count>cap or overflow>1 or reserved:raise ValueError('Invalid pinned phase metadata')
 from video_bus import REGISTERS
 BUS_ADDRESSES=[int(r['address'],16)for r in REGISTERS]
 rows=[]
 for i in range(count):
  raw=ring[32+i*704:32+(i+1)*704];generation,phase,mark,commit=struct.unpack_from('<4I',raw);check=bytearray(raw);check[12:16]=bytes(4)
  if commit!=fnv(check)^0x39a57b62 or phase not in ({0,1} if cap==16 else QOS_PHASES):raise ValueError('Pinned phase integrity failed')
  bus=raw[16:640];bw=struct.unpack('<156I',bus);clk=raw[640:704];cw=struct.unpack('<16I',clk)
  if bw[-1]!=fnv(bus[:-4])^0x6d28a791 or cw[-1]!=fnv(clk[:-4])^0x6039acb7:raise ValueError('Pinned DMA/DDR integrity failed')
  valid=bw[9:14];changed=bw[14:19]
  rows.append(dict(generation=generation,phase=({0:'preview',1:'recording-progress'}[phase] if cap==16 else QOS_PHASES[phase]),mark=mark,elapsed_s=bw[1]/1000,DMA_status=bw[2],driver=hex(bw[4]),scene=hex(bw[5]),IPC_preset=hex(bw[6]),native_sequence_before=bw[7],native_sequence_after=bw[8],DMA_registers={hex(a):hex(bw[19+j])for j,a in enumerate(BUS_ADDRESSES)if valid[j//32]>>(j%32)&1},DMA_changed_during_read=[hex(a)for j,a in enumerate(BUS_ADDRESSES)if changed[j//32]>>(j%32)&1],DDR_readback_status=cw[14],DDR_A_readback_Hz=cw[13],clock_status=cw[2],clock_selector_words=[hex(v)for v in cw[4:13]]))
 return dict(schema='NXL_PINNED_HW_PHASE1',capacity=cap,overflow=bool(overflow),records=rows,interpretation=('Mode/source/recording context stable for2seconds; append-only16-phase evidence, overflow explicit. Configuration snapshots are not bandwidth.' if cap==16 else 'One fresh DMA and DDR readback after at least three seconds of continuous forwarded-frame progress per comparison generation. Appended snapshots retain B after A resumes; five generations maximum, overflow is explicit. They are sparse configuration readbacks, not bandwidth measurements.'))
