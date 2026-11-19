"""R11c output-tail and parallel-surface observations, never DDR busy time.

The strict full-vector proof remains schema4 compatible. Tail-only reads
have no syscall/stack proof: anchor context never becomes a new wait hit.
"""
import collections

NAMES=['Y_bottom_left','Y_bottom_center','Y_bottom_right','UV_bottom_left','UV_bottom_center','UV_bottom_right']
REGS=['WDMA_before','WDMA_after','RSZ2_mux_before','RSZ2_mux_after','DMA_status_before','DMA_status_after','DMA_mask_before','DMA_mask_after',
 'CORE_status_before','CORE_status_after','CORE_mask_before','CORE_mask_after','native_RSZ_counts_before','native_RSZ_counts_after','RDMA0_before','RDMA0_after',
 'driver_A','driver_B','preset_A','preset_B','debug_selector_A','debug_selector_B','guarded_debug_raw']
RAW=[(32,5),(37,5),(98,1),(99,1),(100,3),(103,3),(106,3),(109,3),(112,1),(113,1),(114,1),(115,1),
 (116,2),(118,2),(120,1),(121,1),(122,1),(123,1),(124,1),(125,1),(126,1),(127,1),(128,1)]
def key(d,off=0):return ((d[off+2]+1)&0xffffffff or 1,d[off+5],hex(d[off+9]),hex(d[off+10]),hex(d[off+11]))
def active(d,off=0):
 return d[off:off+2]==[0x42544c56,11]and d[off+3]&15==3 and not d[off+3]&0x10000 and d[off+3]>>24&3==0
def good_native(d,off=0):
 return active(d,off)and bool(d[off+3]&512)and(d[off+3]>>28)&15==1 and not d[off+3]&0x20000 and d[off+14]==0x801cdf4c
def surface(y,uv,word,mux):
 return y%4==uv%4==0 and 0x94000000<=y<uv and uv-y==3840*2160 and uv+3840*1080<=0xc0000000 and word>>12&0x7ffff==3840 and mux>>8&15==7
def pixels(y,uv):return [y+2159*3840+x for x in [16,1920,3824]]+[uv+1079*3840+x for x in [16,1920,3824]]
def ids_equal(a,b):return a[:5]==b[:5]and a[6:]==b[6:]
def decode_tail(flags,d):
 if flags&~3:raise ValueError('Unknown tail header flags')
 for bit,off in [(1,0),(2,16)]:
  if not flags&bit and d[off:off+16]!=[0xffffffff]*16:raise ValueError('Partial failed tail header')
 if d[97]==0xffffffff:
  if flags&~1 or any(x!=0xffffffff for x in d[32:]):raise ValueError('Inactive tail exceeded initial RAM header')
  return dict(short_inactive_tail=True,tail_qualified=False,tail_transition_candidate=False)
 m,om=d[95],d[96]
 if m&~0x7fffff or om&~15 or d[94]&~63 or d[97]!=1 or d[129]not in [0,1]or d[136]!=4 or any(x!=0xffffffff for x in d[137:]):
  raise ValueError('Invalid tail payload policy/masks')
 ia,ib=d[42:50],d[50:58];mb,cb=d[130:132]
 def typed(v):return(v[1]==mb+0x4c608 and v[3]==cb+0x59c28 and v[4]==2 and v[7]==mb+0x4c9c8 and v[0]==d[132]and v[2]==d[133])
 identity=bool(om&3==3 and typed(ia)and typed(ib)and ids_equal(ia,ib))
 # C suppresses all second-pass device reads after changed/failed roots.
 second=sum(1<<i for i in [1,3,5,7,9,11,13,15,17,19,21])
 if not identity and m&second:raise ValueError('Tail second-pass MMIO without paired recording roots')
 if m&1 and not om&1 or m&2 and not identity:raise ValueError('Tail WDMA read without live identity')
 before=bool(m&5==5 and surface(d[32],d[33],d[34],d[98]))
 expected=pixels(d[32],d[33])
 if d[94]and(not before or any(d[94]>>i&1 and d[82+i]!=p for i,p in enumerate(expected))):raise ValueError('Tail probe outside verified output surface')
 debug=bool(m&0x700000==0x700000 and d[126]==d[127]==2)
 if bool(d[129])!=debug or m&(1<<22)and(not m&(1<<20)or d[126]!=2):raise ValueError('Unguarded native debug selector read')
 same=bool(flags==3 and d[:16]==d[16:32]and good_native(d))
 transition=bool(flags==3 and good_native(d)and good_native(d,16)and key(d,16)[0]==key(d)[0]+1 and key(d,16)[1]!=key(d)[1])
 driver=bool(m&0xf0000==0xf0000 and d[122]==d[123]and d[124]==d[125])
 rdma=bool(m&0xc000==0xc000 and d[120]==d[10]and d[121]==d[26])
 dma=bool(m&15==15 and before and d[32:37]==d[37:42]and d[98]==d[99])
 hal_identity=bool(identity and om&12==12 and d[58]==d[62]==mb+0x5b148 and d[60:62]==d[64:66]and d[66:74]==d[74:82])
 hal=bool(hal_identity and d[59]==d[63])
 qualified=bool(same and identity and driver and rdma and dma)
 candidate=bool(transition and identity and driver and rdma and before)
 return dict(short_inactive_tail=False,tail_qualified=qualified,tail_transition_candidate=candidate,same_request=same,
  source_identity_stable=identity,source_state=2 if identity else None,pending_sequence=key(d)[0],HW_tick=d[5],
  native_after_sequence=key(d,16)[0]if flags&2 else None,WDMA2_Y=hex(d[32])if before else None,WDMA2_UV=hex(d[33])if before else None,
  WDMA2_configuration_stable=dma,HAL=hex(ia[6])if identity else None,HEVC_descriptor_stable=hal,
  HEVC_Y=hex(d[68])if hal else None,HEVC_UV=hex(d[69])if hal else None,HEVC_result_path_count=d[59]if hal else None,
  HEVC_descriptor_identity_stable=hal_identity,HEVC_result_path_count_before=d[59]if om&4 else None,HEVC_result_path_count_after=d[63]if om&8 else None,
  within_vector_result_path_advance=bool(hal_identity and d[63]-d[59]==1),
  pixel_read_mask=hex(d[94]),row_fingerprints=[dict(name=NAMES[i],address=hex(d[82+i]),FNV=hex(d[88+i]),available=bool(d[94]>>i&1))for i in range(6)],
  read_mask=hex(m),raw_RO_registers={name:dict(available=bool(m>>i&1),raw=[hex(x)for x in d[RAW[i][0]:sum(RAW[i])]]if m>>i&1 else None)for i,name in enumerate(REGS)},
  parent_anchor_sequence=d[135]if d[135]!=0xffffffff else None,guarded_debug_available=debug,
  raw_status_scope='Native RSZ2->WDMA2 mux and RO dispatch status/masks/counters; configuration or set bits are not module busy durations.',
  native_endpoints_matched=False,qualified_HEVC_completion_wait=False,DDR_bandwidth_measured=False,DDR_stall_cycles=None,EOF_proven=False)

def finish_tail(result,native):
 rows=result['records'];byid={r['sequence']:r for r in rows};tails=[r for r in rows if r['kind']==9]
 full=[r for r in rows if r['kind']==4];windows={w['window']:w for w in result['stage_windows']['windows']}
 counts=collections.Counter()
 for r in(native or{}).get('records',[]):
  if r.get('valid_for_statistics')and r.get('native_input_binding_candidate'):
   counts[(r['sequence'],r['raw_words'][3],r['input_descriptor'],r['input_physical_address'],r['output_descriptor'])]+=1
 def matched(d,off=0):return counts[key(d,off)]==1
 def physical_mapped(w,address,n=16):
  return any(m['pa']<=address and address+n<=m['pa']+m['hi']-m['lo']for m in w)
 maps={w:[r for r in rows if r['kind']==5 and r['window']==w and r['side']=='before']for w in windows}
 for r in tails:
  d=r['raw_words'];w=windows[r['window']];r['window_mapping_stable']=w['CAP_mapping_bookends_stable']
  r['native_endpoints_matched']=bool(r.get('tail_qualified')and matched(d))
  r['transition_native_endpoints_matched']=bool(r.get('tail_transition_candidate')and matched(d)and matched(d,16))
  r['physical_output_mapped']=bool(w['CAP_mapping_bookends_stable']and maps[r['window']]and r.get('WDMA2_Y')and
   all(not p['available']or physical_mapped(maps[r['window']][0]['CAP_maps'],int(p['address'],16))for p in r.get('row_fingerprints',[])))
  a=byid.get(r.get('parent_anchor_sequence'));valid=bool(a and a['kind']==4 and a['window']==r['window']and a['end_ns']<=r['begin_ns'])
  if r.get('parent_anchor_sequence')is not None and not valid:raise ValueError('Tail parent anchor is not a preceding full vector in this window')
  r['prior_anchor_wait_observed']=bool(valid and a.get('qualified_HEVC_completion_wait'))
  r['tail_interpretable']=bool(r['physical_output_mapped']and(r['native_endpoints_matched']or r['transition_native_endpoints_matched']))
  hy=int(r['HEVC_Y'],16)if r.get('HEVC_Y')else 0;huv=int(r['HEVC_UV'],16)if r.get('HEVC_UV')else 0
  r['HEVC_surface_in_camera_mapping']=bool(r['window_mapping_stable']and r.get('HEVC_descriptor_stable')and surface(hy,huv,3840<<12,7<<8)and
   physical_mapped(maps[r['window']][0]['CAP_maps'],hy,3840*2160)and physical_mapped(maps[r['window']][0]['CAP_maps'],huv,3840*1080))
  r['parallel_surface_candidate']=bool(r['tail_interpretable']and r['HEVC_surface_in_camera_mapping']and r['WDMA2_Y']!=r['HEVC_Y'])
 # Normalize full and tail records by probe NAME/address, not ordinal.
 observations=[]
 for r in rows:
  if r['kind']not in [4,9]or not r.get('row_fingerprints'):continue
  d=r['raw_words'];formal=r.get('native_endpoints_matched',False)
  if r['kind']==4:
   transition=bool(int(r['flags'],16)&3==3 and good_native(d)and good_native(d,16)and key(d,16)[0]==key(d)[0]+1 and matched(d)and matched(d,16)and r.get('source_identity_stable'))
   allowed=bool(r.get('window_mapping_stable')and(formal or transition))
  else:transition=r['transition_native_endpoints_matched'];allowed=r['tail_interpretable']
  observations.append((r,formal,transition,allowed))
 changes=[];boundaries=[];returns=[];parallel=[]
 for r in tails:
  if r.get('tail_interpretable')and r.get('within_vector_result_path_advance'):
   returns.append(dict(window=r['window'],request=key(r['raw_words'])[0],vectors=[r['sequence']],bounds_ns=[r['begin_ns'],r['end_ns']],
    observation='within one paired descriptor read',result_status_observed=False,same_buffer_release_proven=False))
 for (p,pf,pt,pa),(r,rf,rt,ra)in zip(observations,observations[1:]):
  if p['window']!=r['window']or not(pa and ra):continue
  x,d=p['raw_words'],r['raw_words']
  if key(x)!=key(d):continue
  pp={q0['name']:q0 for q0 in p['row_fingerprints']if q0['available']}
  for q in r['row_fingerprints']:
   before=pp.get(q['name'])
   if not(before and q['available']and before['address']==q['address']and before['FNV']!=q['FNV']):continue
   event=dict(window=r['window'],request=key(d)[0],probe=q['name'],address=q['address'],bounds_ns=[p['begin_ns'],r['end_ns']],
    vectors=[p['sequence'],r['sequence']],native_before=key(d),native_after=key(d,16),formal_same_request=bool(pf and rf),
    boundary_candidate=bool(pf and rt),EOF_proven=False,unique_writer_proven=False,exact_image_generation_proven=False)
   if pf and rf:changes.append(event)
   elif pf and rt:boundaries.append(event)
  if pf and rf and p.get('HEVC_descriptor_stable')and r.get('HEVC_descriptor_stable')and r['HEVC_result_path_count']-p['HEVC_result_path_count']==1:
   returns.append(dict(window=r['window'],request=key(d)[0],vectors=[p['sequence'],r['sequence']],bounds_ns=[p['begin_ns'],r['end_ns']],
    result_status_observed=False,same_buffer_release_proven=False))
 for r in tails:
  if not r.get('tail_interpretable'):continue
  anchors=[a for a in full if a['window']==r['window']and a.get('qualified_HEVC_completion_wait')and abs(a['begin_ns']-r['begin_ns'])<=25000000]
  before=[a for a in anchors if a['end_ns']<=r['begin_ns']];after=[a for a in anchors if a['begin_ns']>=r['end_ns']]
  pair=None
  if before and after:
   a,b=max(before,key=lambda z:z['end_ns']),min(after,key=lambda z:z['begin_ns'])
   if (a.get('HEVC_Y'),a.get('HEVC_result_path_count'),a.get('FrameData_timestamp_raw'),a.get('metadata_owner'),a.get('HAL'),a.get('worker_tid'),a.get('worker_start_ticks'))==(b.get('HEVC_Y'),b.get('HEVC_result_path_count'),b.get('FrameData_timestamp_raw'),b.get('metadata_owner'),b.get('HAL'),b.get('worker_tid'),b.get('worker_start_ticks')) and a.get('HEVC_Y')==r.get('HEVC_Y')and a.get('HEVC_result_path_count')==r.get('HEVC_result_path_count')and a.get('HAL')==r.get('HAL')and a['raw_words'][225]==b['raw_words'][225]==r['raw_words'][133]:
    pair=[a['sequence'],b['sequence']]
  parallel.append(dict(vector=r['sequence'],window=r['window'],bounds_ns=[r['begin_ns'],r['end_ns']],WDMA2_Y=r.get('WDMA2_Y'),HEVC_Y=r.get('HEVC_Y'),
   surfaces_differ=r.get('parallel_surface_candidate',False),bracketing_same_job_wait_anchors=pair,
   uninterrupted_wait_proven=False,simultaneous_hardware_busy_proven=False,DDR_bandwidth_measured=False,DDR_contention_time_ms=None))
 result['tail_observation']=dict(schema='R11C_TAIL_PARALLEL1',tail_vectors=len(tails),full_anchor_vectors=len(full),
  qualified_tail_vectors=sum(r.get('native_endpoints_matched',False)and r.get('physical_output_mapped',False)for r in tails),
  qualified_transition_vectors=sum(r.get('transition_native_endpoints_matched',False)and r.get('physical_output_mapped',False)for r in tails),
  same_request_tail_changes=changes,boundary_tail_candidates=boundaries,HEVC_return_path_progress=returns,parallel_surface_observations=parallel,
  exact_RSZ2_busy_ms=None,exact_WDMA2_busy_ms=None,DDR_bandwidth_bytes_per_s=None,DDR_contention_time_ms=None,
  interpretation='Dense six-point bottom-row reads bracket visible changes under typed native RSZ2/WDMA2 configuration. Transition candidates stay separate. Only full anchors prove a HEVC wait; no DDR traffic counter is inferred.')
