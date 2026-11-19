"""Passive matched-mode EP auxiliary readback comparison; no device access.

Configuration, native IRQ totals, and IPC flow timing are distinct evidence.
Neither zero IRQ samples nor nonzero DMA configuration proves transfer/busy %.
"""
import collections,statistics
BASES=[0x20821400,0x20821800,0x20821480,0x20821880]
NAMES=['RDMA1_bank0','WDMA1_bank0','RDMA1_bank1','WDMA1_bank1']
FIELDS=['plane0_address','plane1_address','format_stride','control0','control1']

def decode_ep_readback(samples,intervals,frame_timing,started_ns):
 rows=[]
 for sample in samples:
  if not sample['valid']&1048576:continue
  w=sample['ep_readback']
  if len(w)!=32 or w[0]!=0x31525045:raise ValueError('Invalid EP readback extension')
  flags=w[1];available=flags&15;changed=(flags>>4)&15
  if changed&~available:raise ValueError('Changed unavailable EP DMA group')
  for g in range(4):
   detail=(flags>>(12+g*5))&31
   if bool(changed&(1<<g))!=bool(detail):raise ValueError('EP change mask disagreement')
  if flags&1024 and (flags&768!=768 or w[2:4]!=w[4:6]):raise ValueError('EP frame-header coherence mismatch')
  groups={}
  for g,name in enumerate(NAMES):
   raw=w[6+g*5:11+g*5]
   values=dict(zip(FIELDS,raw))if available&(1<<g)else None
   groups[name]=dict(base=hex(BASES[g]),available=values is not None,
     changed_fields=[FIELDS[k]for k in range(5)if flags&(1<<(12+g*5+k))])
   if values is not None:
    groups[name].update(raw={k:hex(v)for k,v in values.items()},
       stride_bytes=(raw[2]>>12)&0x7ffff,bit_depth=8+2*((raw[2]>>8)&7),
       nonzero_configuration=any(raw))
  aliases=[]
  for bank in [0,1]:
   rd=groups[NAMES[bank*2]];wd=groups[NAMES[bank*2+1]]
   if not rd['available']or not wd['available']:continue
   rp=int(rd['raw']['plane0_address'],16);wp=int(wd['raw']['plane0_address'],16)
   if rp and wp:aliases.append(dict(configuration_bank=bank,plane0_same=rp==wp,
      plane0_signed_distance_bytes=rp-wp,stride_equal=rd['stride_bytes']==wd['stride_bytes'],
      plane1_same=int(rd['raw']['plane1_address'],16)==int(wd['raw']['plane1_address'],16)))
  flow=sample.get('flow',[])
  context=dict(width=flow[2],height=flow[3],fps_rounded=flow[4])if len(flow)==32 and flow[1]&1 else None
  rows.append(dict(sample=sample['sequence'],observed_at_s=(sample['t']-started_ns)/1e9,
     epoch=sample['epoch'],driver_mode=hex(sample['rtos'][0]),IPC_preset=hex(sample['rtos'][2]),
     source_state=sample['source'][5]if sample['valid']&16 else None,encoder=context,
     header_before=dict(sequence=w[2],flags=hex(w[3]))if flags&256 else None,
     header_after=dict(sequence=w[4],flags=hex(w[5]))if flags&512 else None,
     same_IPC_header=bool(flags&1024),same_mode=bool(flags&2048),
     all_groups_stable=available==15 and not changed,read_span_us=None if w[31]==0xffffffff else w[31],
     DMA=groups,address_relationships=aliases,
     earlier_EP_status={hex(0x208210c8+4*i):hex(w[26+i])for i in range(5)if w[26+i]!=0xffffffff}))
 groups=collections.defaultdict(list)
 for row in rows:groups[(row['epoch'],row['driver_mode'],row['IPC_preset'],row['source_state'])].append(row)
 comparisons=[]
 for key,rr in sorted(groups.items(),key=lambda pair:repr(pair[0])):
  lo=min(r['observed_at_s']for r in rr)+1;hi=max(r['observed_at_s']for r in rr)-1
  retained=[r for r in rr if lo<=r['observed_at_s']<=hi and r['same_IPC_header']and r['same_mode']and r['all_groups_stable']]
  selected={r['sample']for r in retained}
  rates={}
  for metric in ['MMF_new_per_s','source_forwarded_per_s','HEVC_successful_outputs_per_s','IRQ_EP_A_per_s','IRQ_EP_B_per_s','IRQ_EP_DMA_per_s']:
   valid=[r for r in intervals if r['sample']in selected and r['sample']-1 in selected and metric in r]
   seconds=sum(r['interval_s']for r in valid)
   if seconds:rates[metric]=dict(samples=len(valid),seconds=seconds,weighted_mean=sum(r[metric]*r['interval_s']for r in valid)/seconds)
  tt=[r for r in frame_timing.get('records',[])if r['recording_epoch']==key[0]and r['driver_mode']==key[1]and r.get('source_state')==key[3]and lo<=r['observed_at_s']<=hi and r.get('valid_for_statistics')]
  timing={}
  for metric in ['hardware_to_IRQ_ms','cycle_ms','IRQ_to_callback_ms','start_to_final_IRQ_ms']:
   values=[r[metric]for r in tt if metric in r]
   if values:timing[metric]=dict(n=len(values),mean=statistics.mean(values),median=statistics.median(values),max=max(values))
  signatures={}
  for name in NAMES:
   counts=collections.Counter((r['DMA'][name]['stride_bytes'],r['DMA'][name]['raw']['format_stride'],r['DMA'][name]['raw']['control0'],r['DMA'][name]['raw']['control1'])for r in retained)
   signatures[name]=[dict(stride_bytes=k[0],format_stride=k[1],control0=k[2],control1=k[3],samples=v)for k,v in counts.items()]
  comparisons.append(dict(epoch=key[0],driver_mode=key[1],IPC_preset=key[2],source_state=key[3],steady_window_s=[lo,hi],observed_samples=len(rr),retained_samples=len(retained),configuration_signatures=signatures,native_rates=rates,matched_mode_frame_timing=timing))
 return dict(schema='NXL_EP_READBACK_COMPARISON1',test_kind='passive matched preview/record and three-mode comparison',samples=len(rows),groups=comparisons,records=rows,
   EP_previously_cleared=False,EP_reopened_by_user=True,device_writes=False,
   limitations='Double reads and IPC/mode brackets expose races; they are not a hardware latch. Nonzero configuration/nearby addresses do not prove DMA activity, shared physical DDR channel, bandwidth, or causal delay. IRQ totals retain native meanings. Timing is grouped by matched stable mode/epoch, not causally assigned to each DMA snapshot. Earlier EP status is copied from the preceding progress capture. No auxiliary branch disable switch or EP-bypass A/B is implemented; an OEM reversible control has not been established.')
