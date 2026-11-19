"""R10x read-only output/HEVC metadata. Address agreement is not EOF timing.

All claims are reconstructed from raw values AND per-scalar read masks.
RTOS and Linux time domains are never subtracted. No stale submission flag
is interpreted as busy; no same-address reuse becomes a returned frame.
"""
import collections,struct
from video_completion import fnv
from video_pipeline1 import active

MAGIC=0x31504c56
FIELDS=['CS0_status','CS0_enable','CS0_bit2_callback','IPC_RDMA0_PA','EP_CORE_A','EP_CORE_B','EP_CORE_mask_A','EP_CORE_mask_B','EP_write_mux_A','EP_write_mux_B',
 'WDMA2_A_Y','WDMA2_A_UV','WDMA2_A_format_stride','WDMA2_A_control0','WDMA2_A_control1','WDMA2_B_Y','WDMA2_B_UV','WDMA2_B_format_stride','WDMA2_B_control0','WDMA2_B_control1',
 'EP_write_mux_A_after','EP_DMA_status0','EP_DMA_status1','EP_error_status','EP_DMA_mask0','EP_DMA_mask1','EP_error_mask','output_CImage_type','output_CImage_Y','output_CImage_raw70','output_CImage_type_after','output_CImage_Y_after',
 'HEVC_root','HEVC_type','HEVC_submit_descriptor','HEVC_output_pointer','HEVC_output_size','HEVC_submit_Y','HEVC_submit_UV','HEVC_result_path_count','HEVC_device_fd','HEVC_submit_parameter_not_busy','HEVC_root_after','HEVC_type_after','HEVC_submit_descriptor_after','HEVC_output_pointer_after','HEVC_output_size_after','HEVC_submit_Y_after','HEVC_submit_UV_after','HEVC_result_path_count_after',
 'camera_root','camera_type','source_root','source_type','source_state','source_callback_count','camera_root_after','source_root_after','source_state_after','source_type_after','CS0_bit3_callback','CS0_bit7_callback','native_RSZ_count_CS0','native_RSZ_count_CS1','EP_debug_selector','EP_guarded_debug_raw','EP_debug_selector_after','camera_type_after','metadata_modules_base',
 'WDMA2_A_Y_after','WDMA2_A_UV_after','WDMA2_A_format_stride_after','WDMA2_A_control0_after','WDMA2_A_control1_after','WDMA2_B_Y_after','WDMA2_B_UV_after','WDMA2_B_format_stride_after','WDMA2_B_control0_after','WDMA2_B_control1_after','metadata_core_base']
def decode_output(data):
    if len(data)<128:raise ValueError('Truncated output header')
    h=struct.unpack_from('<32I',data)
    if(h[:4]!=(MAGIC,2,512,600)or h[4]>600 or len(data)!=128+512*h[4]or
       h[6]&~15 or h[7:10]!=(100,2,24)or h[10]not in [5000,10000,20000]or
       h[11:13]!=(2,512)or h[14]not in [1000,2000,5000]or any(h[21:31])or h[31]!=fnv(data[:124])):
        raise ValueError('Output header integrity/limits failed')
    rows=[];budgets=[]
    for i in range(h[4]):
        raw=data[128+i*512:128+(i+1)*512]
        begin,end,seq,burst,kind,flags=struct.unpack_from('<QQ4I',raw);w=list(struct.unpack_from('<119I',raw,32))
        if(seq!=i or begin>end or kind not in [1,2,3,4,5]or burst>h[5]or
           struct.unpack_from('<I',raw,508)[0]!=(fnv(raw[:508])^0x59a130c7)or
           rows and begin<rows[-1]['begin_ns']):raise ValueError('Output record integrity/order failed')
        row=dict(sequence=i,burst=burst,kind=kind,flags=hex(flags),begin_ns=begin,end_ns=end,raw_words=w)
        if kind in [1,4]:
            a,b=w[:16],w[16:32];v=w[32:];m=w[112]|w[113]<<32|w[114]<<64
            def has(*indices):return all(m>>j&1 for j in indices)
            allowed=sum(1<<j for j in list(range(68))+list(range(69,79))+list(range(83,87)))
            if flags&~16383 or m&~allowed or kind==1 and not burst or kind==4 and (burst or flags&256):raise ValueError('Unknown output sample bits')
            for bit,words in [(1,a),(2,b)]:
                if not flags&bit and words!=[0xffffffff]*16:raise ValueError('Partial failed native header')
            same=flags&3==3 and a[:2]==[0x42544c56,11]and a==b
            derived=(4 if same else 0)|(8 if same and active(a)else 0)
            derived|=16 if same and (a[3]>>28)&15==1 and not a[3]&0x20000 and a[14]==0x801cdf4c else 0
            derived|=32 if has(0)and v[0]&4 else 0
            derived|=64 if has(1)and v[1]&4 else 0
            derived|=128 if has(2)and v[2]==0x801cdf6c else 0
            image=(has(27,28,30,31)and 0x82000000<=a[11]<=0x92ffff8c and not a[11]&3 and
                   v[27]==v[30]==0x80588910 and v[28]==v[31])
            output=(has(8,10,11,12,20,69,70,71)and v[8]==v[20]and v[10:13]==v[69:72]and
                    v[8]>>8&15==7 and v[12]>>12&0x7ffff==3840 and v[10]!=0 and v[11]>v[10]and v[11]-v[10]==3840*2160)
            hevc=(has(*range(32,50))and v[32]==v[42]and v[33]==v[43]==v[68]+0x4c9c8 and
                  v[34]==v[44]and v[35:39]==v[45:49]and v[39]==v[49])
            source=(has(*range(50,60),67)and v[50]==v[56]and v[51]==v[67]==v[68]+0x4c608 and
                    v[52]==v[57]and v[53]==v[59]==v[79]+0x59c28 and v[54]==v[58])
            debug=has(64,65,66)and v[64]==v[66]==2
            derived|=(512 if output else 0)|(1024 if image else 0)|(2048 if hevc else 0)|(4096 if source else 0)|(8192 if debug else 0)
            if flags&~(3|256)!=derived:raise ValueError('False output/source/HEVC flags')
            # Metadata bases are pinned by runtime hashes/maps, not callable pointers.
            if v[68]&3 or v[79]&3 or not 0x10000<=v[68]<0xbff80000 or not 0x10000<=v[79]<0xbff80000:raise ValueError('Invalid library mapping metadata')
            qualified=flags&28==28 and has(3,83,84,85,86)and v[3]==a[10]and v[83:85]==v[85:87]
            recording=bool(source and v[54]==2)
            dma_hal=bool(qualified and recording and output and hevc and v[10:12]==v[37:39]and v[10]!=0)
            image_dma=bool(qualified and image and output and v[28]==v[10]and v[28]!=0)
            row.update(same_request=same,qualified_request=qualified,pending_sequence=(((a[2]+1)&0xffffffff)or 1)if same else None,
                HW_tick=a[5]if same else None,input_descriptor=hex(a[9])if same else None,input_PA=hex(a[10])if same else None,
                output_descriptor=hex(a[11])if same else None,read_mask=hex(m),native_endpoints_matched=False,
                source_stable=bool(source),source_state=v[54]if source else None,recording=recording,
                output_4K_configuration_stable=bool(output),output_CImage_Y_stable=bool(image),HEVC_descriptor_stable=bool(hevc),
                WDMA2_Y=hex(v[10])if has(10)else None,WDMA2_UV=hex(v[11])if has(11)else None,
                output_CImage_Y=hex(v[28])if image else None,
                HEVC_Y=hex(v[37])if hevc else None,HEVC_UV=hex(v[38])if hevc else None,
                HEVC_object=hex(v[32])if hevc else None,HEVC_descriptor=hex(v[34])if hevc else None,
                HEVC_result_path_count=v[39]if hevc else None,
                WDMA2_HEVC_address_match=dma_hal,CImage_WDMA2_Y_match=image_dma,
                three_way_address_match=dma_hal and image_dma,
                guarded_debug_available=bool(debug),completion_pending=bool(derived&32),
                native_cross_domain_frame_identity_proven=False,HEVC_buffer_return_identity_proven=False,
                hardware_busy_duration_proven=False)
            row['raw_fields']={name:dict(raw=hex(v[j]),read_success=bool(m>>j&1))for j,name in enumerate(FIELDS)if j not in [68,79]}
            row['raw_fields'].update({name:dict(raw=hex(v[83+j]),read_success=bool(m>>(83+j)&1))for j,name in enumerate(['driver_before','preset_before','driver_after','preset_after'])})
        elif kind in [2,3]:
            if(not burst or flags&~7 or flags&3 not in [1,2]or w[54]!=kind-2 or w[53]&~15 or any(x!=0xffffffff for x in w[55:])):raise ValueError('Invalid output route record')
            if not flags&4 and w[:16]!=[0xffffffff]*16:raise ValueError('Partial route header')
            row.update(bank=kind-2,final=bool(flags&2),callbacks=[hex(x)for x in w[16:34]],owners=[hex(x)for x in w[34:52]],read_mask=hex(w[52]|w[53]<<32))
        else:
            if burst or flags or begin!=end or any(w[13:])or w[1]not in [200,500,1000,2000,5000]or w[11]not in [5000,10000,20000]or w[12]not in [1000,2000,5000]:raise ValueError('Invalid independent budget record')
            row.update(check=w[0],heavy_ms=w[1],fast_over=w[2],heavy_over=w[3],dense_over=w[4],
                       fast_CPU_ns=w[5]|w[6]<<32,heavy_CPU_ns=w[7]|w[8]<<32,dense_CPU_ns=w[9]|w[10]<<32,dense_ms=w[11],target_ms=w[12])
            budgets.append({k:v for k,v in row.items()if k!='raw_words'})
        rows.append(row)
    if sum(r['kind']==1 for r in rows)!=h[13]or sum(r['kind']==4 for r in rows)!=h[15]:raise ValueError('Output count mismatch')
    bursts=[]
    for burst in range(1,h[5]+1):
        rr=[r for r in rows if r['burst']==burst];ss=[r for r in rr if r['kind']==1]
        if not rr or len(ss)>24:raise ValueError('Missing/unbounded dense window')
        post=0;pending=None;lower=0
        for r in ss:
            flags=int(r['flags'],16)
            if flags&256:
                if pending is None:raise ValueError('Followup without same-request pending')
                post+=1
                if post>2:raise ValueError('Too many pending followups')
                if r['qualified_request']and r['raw_words'][:16]==pending['raw_words'][:16]and flags&224==224:
                    lower=max(lower,(r['begin_ns']-pending['end_ns'])/1e6)
            elif r['qualified_request']and flags&224==224:
                if pending is not None:raise ValueError('Repeated pending baseline')
                pending=r
        bursts.append(dict(burst=burst,samples=len(ss),begin_ns=rr[0]['begin_ns'],end_ns=rr[-1]['end_ns'],
                           done_pending_followups=post,done_pending_lower_bound_ms=max(0,lower),
                           qualified_vectors=sum(r['qualified_request']for r in ss),stages=[]))
    vectors=[r for r in rows if r['kind']in [1,4]]
    # A count increment establishes return-path progress only between snapshots.
    # It does not identify which same-address buffer lifetime was returned.
    progress=[];previous=None
    for r in vectors:
        if not(r['HEVC_descriptor_stable']and r['recording']):previous=None;continue
        if previous and r['HEVC_object']==previous['HEVC_object']:
            delta=(r['HEVC_result_path_count']-previous['HEVC_result_path_count'])&0xffffffff
            if delta<10000:progress.append(dict(first=previous['sequence'],last=r['sequence'],result_path_delta=delta,interval_ns=r['end_ns']-previous['begin_ns'],same_buffer_return_proven=False))
        previous=r
    return dict(schema='NXL_PIPELINE2',record_count=h[4],capacity=600,burst_count=h[5],sample_count=h[13],periodic_samples=h[15],
                flags=hex(h[6]),dense_interval_requested_ms=2,dense_cadence_final_ms=h[10],periodic_cadence_final_ms=h[14],
                dense_CPU_seconds=(h[18]|h[19]<<32)/1e9,maximum_dense_CPU_ms=h[20]/1e6,
                records=rows,bursts=bursts,budgets=budgets,HEVC_result_path_progress=progress,
                qualified_vectors=sum(r['qualified_request']for r in vectors),
                WDMA2_HEVC_address_matches=sum(r['WDMA2_HEVC_address_match']for r in vectors),
                three_way_address_matches=sum(r['three_way_address_match']for r in vectors),
                interpretation='OEM RSZ2->WDMA2 configuration and stable Linux Encode descriptor addresses, source/types and full native headers. Disabled EOF is not a hardware bypass. Configuration is not busy/progress timing. HAL+11c is NOT busy. HAL+174 counts the return path after the result ioctl; its return status is unobserved, never an exact same-buffer return. Same addresses may be reused. Debug E0 is usable only if native selector already2 and unchanged. No cross-clock subtraction or automatic bottleneck verdict.')

def associate_output(pipeline,native):
    keys=collections.defaultdict(list)
    for r in native.get('records',[]):
        if not(r.get('valid_for_statistics')and r.get('native_input_binding_candidate')):continue
        key=(r['sequence'],r['raw_words'][3],r['input_descriptor'],r['input_physical_address'],r['output_descriptor']);keys[key].append(r)
    for r in pipeline['records']:
        if r['kind']not in [1,4]or not r['qualified_request']:continue
        key=(r['pending_sequence'],r['HW_tick'],r['input_descriptor'],r['input_PA'],r['output_descriptor'])
        r['native_endpoints_matched']=len(keys[key])==1
    ss=[r for r in pipeline['records']if r['kind']in [1,4]and r['native_endpoints_matched']]
    pipeline['endpoint_matched_vectors']=len(ss)
    pipeline['endpoint_matched_WDMA2_HEVC_address_matches']=sum(r['WDMA2_HEVC_address_match']for r in ss)
    pipeline['endpoint_matched_three_way_address_matches']=sum(r['three_way_address_match']for r in ss)
    pipeline['endpoint_matched_rising_edges_by_stage']={}
    return pipeline
