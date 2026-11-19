"""R10z stage windows: preserve unknowns, address domains and timing scope.

No unchanged pixel hash proves a stall. No row fingerprint proves EOF.
No semaphore-wait span becomes pure HEVC/DDR busy time.
"""
import collections
import json
from pathlib import Path


def signed(v):
    return v if v < 0x80000000 else v-0x100000000


def task_proof(nr,args,sp,pc,stack,modules,hal,fd,sem,pthread):
    if nr == 54 and args[0] == fd and args[2] == hal+0x5c:
        return {0xc10c7408:1,0xc10c740a:2}.get(args[1],0)
    if nr == 240 and sem and pthread and args[0] == sem+12 and args[1]&127 == 0 and pc == pthread+0xbd04:
        if modules+0x2cc74 in stack and modules+0x260d0 in stack:
            return 4
    return 0


def decode_stage(kind,flags,d,schema=2):
    if kind == 9:
        if schema not in [5,6]:raise ValueError('Tail vector requires schema5')
        from video_tail_windows import decode_tail
        return decode_tail(flags,d)
    if kind == 8:
        if schema not in [3,4,5,6] or flags or d[0]not in [1,2,3,4]or d[1]not in [1,2,3]or d[2]not in [0,1,2]or d[9]>8:
            raise ValueError('Invalid asynchronous maps progress')
        return dict(scan_disposition={1:'progress',2:'complete',3:'failed',4:'cancelled'}[d[0]],
                    scanner_state=d[1],mapping_status=d[2],lines_processed=d[3],bytes_read=d[4],failure_stage=d[5],read_errno=d[6],
                    pthread_mappings=d[7],pthread_base=hex(d[8]),CAP_map_count=d[9],scan_begin_ns=d[10]|d[11]<<32,
                    scan_end_ns=d[12]|d[13]<<32,scan_epoch=d[14],cumulative_metadata_CPU_ns=d[15]|d[16]<<32)
    if kind == 5:
        if flags not in [1,2] or d[0]>8 or d[1]not in [0,1,2] or d[39]not in [0,1] or d[45]&~31 or d[46]not in [0,1]:
            raise ValueError('Invalid stage mapping bookend')
        maps=[dict(lo=d[4+i*4],hi=d[5+i*4],pa=d[6+i*4],permissions=d[7+i*4])for i in range(d[0])]
        valid=d[1]==1 and bool(maps) and all(m['permissions']==1 and 0x10000<=m['lo']<m['hi']<=0xbfff0000 and 0x94000000<=m['pa'] and m['pa']+m['hi']-m['lo']<=0xc0000000 for m in maps)
        extra={}
        if schema>=3:
            begin,end=d[48]|d[49]<<32,d[50]|d[51]<<32
            if d[56]not in [0,1]or bool(d[56])!=(d[1]==1)or begin>end or not d[57]or d[52]>4097 or d[53]>266240:
                raise ValueError('Invalid asynchronous mapping interval')
            if d[56]and(d[54]or d[55]or not begin or not end):raise ValueError('Incomplete scan marked complete')
            if d[56]and(not d[52]or not d[53]or d[52]<d[0]or d[58]>d[52]):raise ValueError('Impossible complete scan counters')
            if any(max(a['lo'],b['lo'])<min(a['hi'],b['hi'])for i,a in enumerate(maps)for b in maps[i+1:]):
                valid=False
            extra=dict(scan_begin_ns=begin,scan_end_ns=end,lines_processed=d[52],bytes_read=d[53],scan_failure_stage=d[54],scan_errno=d[55],
                       scan_complete=bool(d[56]),scan_epoch=d[57],pthread_mapping_count=d[58],
                       timing_scope='Full incremental scan interval; record timestamp is not an instantaneous maps capture.')
        return dict(side='before'if flags==1 else'after',mapping_status=d[1],mapping_valid=bool(valid),CAP_maps=maps,
                    full_maps_FNV=hex(d[2]),worker_tid=d[36],worker_start_ticks=d[37]|d[38]<<32,worker_identity_verified=bool(d[39]),
                    EOF_callback_owner_words=d[40:45],EOF_route_read_mask=hex(d[45]),
                    original_pthread_hash_verified=bool(d[46]),pthread_base=hex(d[47]),**extra)
    if kind == 6:
        if schema>=4:
            from video_hevc_wait_site import decode_discovery
            return decode_discovery(flags,d)
        if flags&~1 or d[17]not in [0,256] or d[3]not in [0,1,2,4] or d[87]&~63:
            raise ValueError('Invalid stage task discovery')
        sem=d[89]if d[87]==63 and d[88:90]==d[92:94]and signed(d[90])<0 and signed(d[94])<0 else 0
        proof=task_proof(signed(d[8]),d[9:15],d[15],d[16],d[18:18+d[17]//4],d[82],d[83],d[84],sem,d[96])
        if d[3] and(d[7]!=1 or d[86]not in [1,2] or d[3]!=proof):raise ValueError('False HEVC worker discovery proof')
        if flags and(not d[3] or d[4]<=1 or not(d[5]|d[6]<<32)):raise ValueError('Unproved selected HEVC worker')
        return dict(task_index=d[0],inventory_count=d[1],inventory_status=d[2],proof=d[3],selected=bool(flags),
                    tid=d[4],start_ticks=d[5]|d[6]<<32,syscall_status=d[7],syscall=signed(d[8]),
                    arguments=[hex(v)for v in d[9:15]],SP=hex(d[15]),PC=hex(d[16]),stack_bytes=d[17],
                    attempted_tasks=d[85],device_path_kind=d[86],semaphore_raw_words=d[87:96],
                    task_read_failures=dict(before_stage=d[97],before_errno=d[98],after_stage=d[99],after_errno=d[100]),
                    interpretation='Only selected candidates and every16th/final attempt retained. Stack PCs plus exact original cond futex/PC, or the actual HAL ioctl tuple.')
    if kind == 7:
        if flags or d[1]not in [5000,10000,20000]:raise ValueError('Invalid independent stage CPU ledger')
        return dict(check=d[0],stage_cadence_ms=d[1],stage_over=d[2],memory_over=d[3],
                    stage_CPU_ns=d[4]|d[5]<<32,memory_CPU_ns=d[6]|d[7]<<32,heavy_cadence_ms=d[8],cold_cadence_ms=d[9],
                    **(dict(metadata_CPU_ns=d[10]|d[11]<<32,metadata_over=d[12])if schema>=3 else{}))
    if kind != 4 or flags&~0x1ffff or d[245]&~0xfff:
        raise ValueError('Invalid stage vector flags/read mask')
    a,b=d[:16],d[16:32]
    same=flags&3==3 and a[:2]==[0x42544c56,11]and a==b
    if bool(flags&4)!=same:raise ValueError('False same-request stage flag')
    short=bool(flags&65536 and not flags&2)
    if short:
        if flags&~65537 or d[245]or d[246]or any(v!=0xffffffff for v in d[16:245]):raise ValueError('New-request post read exceeded initial header')
        return dict(short_rejected_pending_followup=True,qualified_request=False,native_endpoints_matched=False,pending_sequence=(a[2]+1)&0xffffffff or 1,HW_tick=a[5])
    if not flags&2:
        if flags&~1 or d[245]or d[246]or any(v!=0xffffffff for v in d[16:245]):raise ValueError('Inactive stage read exceeded initial header')
        return dict(short_inactive_native_header=True,qualified_request=False,native_endpoints_matched=False,
                    pending_sequence=((a[2]+1)&0xffffffff or 1)if a[:2]==[0x42544c56,11]else None,HW_tick=a[5])
    if d[83]&~(63 if schema>=4 else 15) or d[184]not in [0,256] or bool(flags&2048)!=(d[184]==256) or d[185]not in [0,1,2,4] or d[240]&~63:
        raise ValueError('Invalid stage payload/proof extent')
    if schema<4 and flags&2048 and not(flags&512 and d[99]==1 and signed(d[100])==240):raise ValueError('Unbound task stack read')
    identity=flags&96==96 and d[58:62]==d[62:66]and d[58]==d[228]+0x59c28 and d[59]in [1,2]and ((d[60]!=0 and d[61]==d[227]+0x4c9c8)or(d[59]==1 and d[60]==d[61]==0))
    driver=d[245]&0x660==0x660 and d[79:81]==d[88:90]
    qualified=bool(same and a[3]&512 and(a[3]>>28)&15==1 and not a[3]&0x20000 and a[14]==0x801cdf4c and identity and driver)
    dma=bool(flags&24==24 and d[32:37]==d[37:42]and d[245]&0x880==0x880 and d[81]==d[90]and d[81]>>8&15==7 and d[34]>>12&0x7ffff==3840 and d[32]>=0x94000000 and d[33]-d[32]==3840*2160 and d[33]+3840*1080<=0xc0000000)
    addresses=[d[32]+16,d[32]+1080*3840+16,d[32]+2159*3840+16,d[33]+1079*3840+16]
    actual=d[92:96];hashes=d[84:88]
    if schema>=4:
        addresses.extend([d[32]+2159*3840+3824,d[33]+1079*3840+3824]);actual.extend([d[230],d[232]]);hashes.extend([d[231],d[233]])
    if d[83]:
        valid_before=bool(flags&8 and d[245]&128 and d[81]>>8&15==7 and d[34]>>12&0x7ffff==3840 and d[32]>=0x94000000 and d[33]-d[32]==3840*2160 and d[33]+3840*1080<=0xc0000000)
        if not valid_before or any(d[83]>>i&1 and actual[i]!=a for i,a in enumerate(addresses)):
            raise ValueError('Output-row probe outside verified surface')
    hal=bool(flags&384==384 and identity and d[61]==d[227]+0x4c9c8 and d[66]==d[70]==d[227]+0x5b148 and d[67]==d[71]and d[42:50]==d[50:58])
    ta,tb=(d[99],signed(d[100]),d[101:107],d[107],d[108]),(d[109],signed(d[110]),d[111:117],d[117],d[118])
    task_same=flags&1536==1536 and (ta==tb if schema<4 else ta[0]==tb[0]and ta[2:]==tb[2:])and ta[0]==1 and d[96]>1 and(d[97]|d[98]<<32)>0
    sem=d[236]if d[240]==63 and d[235]==d[238]and d[236]==d[239]and signed(d[241])<0 and signed(d[242])<0 else 0
    proof=task_proof(ta[1],ta[2],ta[3],ta[4],d[120:120+d[184]//4],d[227],d[60],d[119],sem,d[246])if task_same else 0
    extra={}
    if schema>=4:
        from video_hevc_wait_site import stage_proof
        extra=stage_proof(flags,d,identity,hal)
    elif d[185]and(d[237]not in [1,2]or d[185]!=proof):raise ValueError('False HEVC wait/ioctl proof')
    qa,qb=d[186:194],d[194:202];ya,yb=d[202:211],d[211:220]
    q=bool(flags&12288==12288 and d[223]==d[228]+0x593b0 and 0<d[220]<=64 and d[234]<d[220]and qa[0]==qb[0]==d[234]and qa[7]<=3 and qb[7]<=3)
    before=bool(q and flags&16384 and qa[7]==3 and qa[1]==qa[2]!=0 and qa[3]==64 and ya[:4]==[3840,2160,3840,2160])
    payload_stable=bool(before and flags&32768 and qb[7]==3 and qa[:7]==qb[:7]and ya==yb)
    returned=bool(before and qb[7]==0 and all(qb[j]==0 for j in [1,2,3,6])and qa[4:6]==qb[4:6])
    return dict(qualified_request=qualified,same_request=bool(same),pending_sequence=(a[2]+1)&0xffffffff or 1,HW_tick=a[5],
                native_endpoints_matched=False,source_identity_stable=bool(identity),source_state=d[59]if identity else None,
                driver_mode=hex(d[79]),IPC_preset=hex(d[80]),
                WDMA2_configuration_stable=dma,WDMA2_Y=hex(d[32])if dma else None,WDMA2_UV=hex(d[33])if dma else None,
                HEVC_descriptor_stable=hal,HEVC_Y=hex(d[44])if hal else None,HEVC_UV=hex(d[45])if hal else None,
                HEVC_result_path_count=d[67]if hal else None,HAL=hex(d[60])if identity else None,
                pixel_read_mask=hex(d[83]),row_fingerprints=[dict(address=hex(actual[i]),FNV=hex(hashes[i]),available=bool(d[83]>>i&1),
                    **(dict(name=['Y_top_left','Y_middle_left','Y_bottom_left','UV_bottom_left','Y_bottom_right','UV_bottom_right'][i])if schema>=4 else{}))for i in range(len(actual))],
                CORE_A_raw=hex(d[74]),CORE_A_mask_raw=hex(d[75]),MMIO_read_mask=hex(d[245]),
                worker_tid=d[96],worker_start_ticks=d[97]|d[98]<<32,worker_syscall_pair_stable=bool(task_same),worker_proof=d[185],
                syscall=signed(d[100])if flags&512 else None,syscall_arguments=[hex(v)for v in d[101:107]]if flags&512 else None,
                pthread_base=hex(d[246]),HEVC_semaphore_data=hex(sem)if sem else None,
                task_read_failures=None if flags&2048 else dict(before_stage=d[120],before_errno=d[121],after_stage=d[122],after_errno=d[123]),
                metadata_slot=d[234]if q else None,pool=hex(d[222]),slot_array=hex(d[221]),Que_payload_before_valid=before,Que_payload_stable=payload_stable,
                FrameData_timestamp_raw=qa[4:6]if before else None,metadata_owner=hex(qa[1])if before else None,
                payload_Y_virtual=hex(ya[4])if before else None,payload_UV_virtual=hex(ya[5])if before else None,
                partial_Que_clearing_candidate=returned,physical_image_return_proven=False,
                pure_hardware_busy_time_proven=False,DDR_contention_proven=False,**extra)


def translate(maps,address):
    if address is None:return None
    va=int(address,16);matches={m['pa']+va-m['lo']for m in maps if m['lo']<=va and va+16<=m['hi']}
    return next(iter(matches))if len(matches)==1 else None


def finish_stages(result,native):
    rows=result['records'];vectors=[r for r in rows if r['kind']==4]
    maps=[r for r in rows if r['kind']==5];events=[];wait_spans=[];EOF=[];windows=[];partial_returns=[];publications=[]
    key_count=collections.Counter();verified=result['schema']in ['NXL_BUFFER_MEMORY4','NXL_BUFFER_MEMORY5','NXL_BUFFER_MEMORY6']
    certificates=[r for r in rows if r['kind']==6 and r.get('selected')and r.get('proof')==4 and r.get('verified_wait_policy')==1]
    if native:
        for r in native.get('records',[]):
            if r.get('valid_for_statistics')and r.get('native_input_binding_candidate'):
                key_count[(r['sequence'],r['raw_words'][3],r['input_descriptor'],r['input_physical_address'],r['output_descriptor'])]+=1
    stage_spec=json.loads((Path(__file__).parent/'video_pipeline_stages.json').read_text('utf8'))['EP_stages']
    expected={r['event']:int(r['banks'][0]['callback'],16)for r in stage_spec}
    for w in range(1,result['window_count']+1):
        rr=[r for r in vectors if r['window']==w];mm=[r for r in maps if r['window']==w]
        observations=[r for r in rows if r['window']==w and r['kind']in [4,9]]
        before=[m for m in mm if m['side']=='before'];after=[m for m in mm if m['side']=='after']
        map_ok=len(before)==len(after)==1 and before[0]['mapping_valid']and after[0]['mapping_valid']and before[0]['CAP_maps']==after[0]['CAP_maps']
        scan_brackets=None
        if result['schema']in ['NXL_BUFFER_MEMORY3','NXL_BUFFER_MEMORY4','NXL_BUFFER_MEMORY5','NXL_BUFFER_MEMORY6']:
            scan_brackets=bool(observations and len(before)==len(after)==1 and before[0]['scan_complete']and after[0]['scan_complete']and
                before[0]['scan_end_ns']<=observations[0]['begin_ns']and before[0]['scan_end_ns']+500000000>=observations[0]['begin_ns']and
                after[0]['scan_begin_ns']>=observations[-1]['end_ns']and after[0]['scan_begin_ns']<=observations[-1]['end_ns']+500000000 and
                after[0]['scan_epoch']>before[0]['scan_epoch']and all(0<=m['scan_end_ns']-m['scan_begin_ns']<=16000000000 and m['scan_end_ns']<=m['begin_ns'] for m in [before[0],after[0]]))
            map_ok=bool(map_ok and scan_brackets)
        worker_ok=bool(map_ok and before[0]['worker_identity_verified']and after[0]['worker_identity_verified']and before[0]['worker_tid']==after[0]['worker_tid']and before[0]['worker_start_ticks']==after[0]['worker_start_ticks'])
        routed={}
        for event,at in [(7,0),(15,2)]:
            routed[event]=bool(len(before)==len(after)==1 and all(int(m['EOF_route_read_mask'],16)&31==31 for m in [before[0],after[0]])and
                 before[0]['EOF_callback_owner_words']==after[0]['EOF_callback_owner_words']and before[0]['EOF_callback_owner_words'][at]==expected[event]and before[0]['EOF_callback_owner_words'][at+1]!=0)
        endings=[r for r in rows if r['kind']==3 and r['window']==w]
        if len(endings)!=1 or endings[0]['vectors']!=len(observations):raise ValueError('Stage window end/count mismatch')
        windows.append(dict(window=w,vectors=len(observations),CAP_mapping_bookends_stable=bool(map_ok),worker_bookends_stable=worker_ok,
                            EOF_route_verified={str(e):ok for e,ok in routed.items()},reason=endings[0]['reason'],
                            **(dict(full_scan_intervals_enclose_hot_vectors=scan_brackets)if scan_brackets is not None else{})))
        for r in (x for x in rows if x['kind']==1 and x['window']==w):
            y=translate(before[0]['CAP_maps'],r.get('slot_payload_Y'))if map_ok else None
            uv=translate(before[0]['CAP_maps'],r.get('slot_payload_UV'))if map_ok else None
            r['Que_HEVC_match_under_window_mapping']=bool(r.get('watched_payload_before_valid')and r.get('HEVC_descriptor_stable')and
                y is not None and uv is not None and y==int(r['HEVC_Y'],16)and uv==int(r['HEVC_UV'],16))
        previous=None;wait_first=None;wait_last=None;last_job=None;pending=None;pending_posts=0;seen_pending=set()
        def close_wait():
            nonlocal wait_first,wait_last,last_job
            if wait_first and wait_last and wait_first is not wait_last:
                wait_spans.append(dict(window=w,first_vector=wait_first['sequence'],last_vector=wait_last['sequence'],
                    begin_ns=wait_first['begin_ns'],end_ns=wait_last['end_ns'],
                    observed_same_job_span_ms=(wait_last['end_ns']-wait_first['begin_ns'])/1e6,
                    HEVC_Y=wait_first['HEVC_Y'],FrameData_timestamp_raw=wait_first['FrameData_timestamp_raw'],
                    interpretation='Same image generation observed in the original completion-wait chain. Includes firmware/IRQ/scheduling; not pure codec busy or proven uninterrupted blocked time.'))
            wait_first=wait_last=last_job=None
        for r in rr:
            d=r['raw_words'];key=(r.get('pending_sequence'),r.get('HW_tick'),hex(d[9]),hex(d[10]),hex(d[11]))
            pending_key=(d[2],d[5],d[9],d[10],d[11])
            post=bool(int(r['flags'],16)&65536)
            if post:
                if pending is None:raise ValueError('Unpaired stage pending followup')
                if r.get('short_rejected_pending_followup'):pending=None
                else:
                    if pending_key!=pending:raise ValueError('Stage followup read outside same request tuple')
                    pending_posts+=1
                    if pending_posts>2:raise ValueError('More than two same-request stage pending reads')
                    if pending_posts==2:pending=None
            if not r.get('short_rejected_pending_followup'):
                hit=bool(r.get('same_request')and d[3]&512 and d[76]!=0xffffffff and d[77]!=0xffffffff and d[76]&4 and d[77]&4 and d[78]==0x801cdf6c)
                if not post:
                    pending=None
                    if hit and pending_key not in seen_pending:pending=pending_key;pending_posts=0;seen_pending.add(pending_key)
                elif not hit:pending=None
            r['native_endpoints_matched']=bool(r['qualified_request']and key_count[key]==1)
            y=translate(before[0]['CAP_maps'],r.get('payload_Y_virtual'))if map_ok else None
            uv=translate(before[0]['CAP_maps'],r.get('payload_UV_virtual'))if map_ok else None
            matched=bool(r.get('source_state')==2 and r.get('Que_payload_before_valid')and r.get('HEVC_descriptor_stable')and y==int(r['HEVC_Y'],16)and uv==int(r['HEVC_UV'],16))
            r.update(window_mapping_stable=bool(map_ok),payload_Y_physical=hex(y)if y is not None else None,payload_UV_physical=hex(uv)if uv is not None else None,mapped_Que_HEVC_match=matched)
            certificate=next((q for q in certificates if q['end_ns']<=r['begin_ns']and q['tid']==r.get('worker_tid')and q['start_ticks']==r.get('worker_start_ticks')and
                q['pthread_base']==r.get('pthread_base')and q['HAL']==r.get('HAL')and q['source']==hex(d[225])and
                q['HEVC_semaphore_wrapper']==hex(d[235])and q['HEVC_semaphore_data']==r.get('HEVC_semaphore_data')),None)if verified else None
            if verified:r['verified_wait_certificate_sequence']=certificate['sequence']if certificate else None
            wait=bool((not verified or certificate is not None)and worker_ok and r['native_endpoints_matched']and r.get('worker_proof')==4 and r.get('Que_payload_stable')and matched and
                      r['worker_tid']==before[0]['worker_tid']and r['worker_start_ticks']==before[0]['worker_start_ticks']and
                      before[0]['original_pthread_hash_verified']and after[0]['original_pthread_hash_verified']and
                      before[0]['pthread_base']==after[0]['pthread_base']==r.get('pthread_base'))
            r['qualified_HEVC_completion_wait']=wait
            job=(r.get('HAL'),r.get('HEVC_Y'),tuple(r.get('FrameData_timestamp_raw')or()),r.get('metadata_slot'),r.get('metadata_owner'),r.get('HEVC_result_path_count'),r.get('worker_tid'))
            if wait:
                if wait_first is None or job!=last_job or r['begin_ns']-wait_last['end_ns']>8000000:
                    close_wait();wait_first=r
                wait_last=r;last_job=job
            else:close_wait()
            if r.get('partial_Que_clearing_candidate')and matched and r['native_endpoints_matched']:
                partial_returns.append(dict(window=w,vector=r['sequence'],slot=r['metadata_slot'],FrameData_timestamp_raw=r['FrameData_timestamp_raw'],
                    HEVC_Y=r['HEVC_Y'],bounds_ns=[r['begin_ns'],r['end_ns']],full_Que_tombstone_captured=False,physical_image_release_proven=False))
            compatible=bool(previous and r['native_endpoints_matched']and previous['native_endpoints_matched']and
                            (r['pending_sequence'],r['HW_tick'])==(previous['pending_sequence'],previous['HW_tick']))
            if previous and r['native_endpoints_matched']and previous['native_endpoints_matched']and r.get('source_state')==previous.get('source_state')and r['pending_sequence']==previous['pending_sequence']+1:
                publications.append(dict(window=w,request=r['pending_sequence'],HW_tick=r['HW_tick'],source_state=r.get('source_state'),
                    bounds_ns=[previous['begin_ns'],r['end_ns']],interpretation='Observed active native-header publication boundary, not a direct hardware-start timestamp.'))
            if compatible and r.get('WDMA2_configuration_stable')and previous.get('WDMA2_configuration_stable')and r['WDMA2_Y']==previous['WDMA2_Y']:
                for i,(p,q)in enumerate(zip(previous['row_fingerprints'],r['row_fingerprints'])):
                    if p['available']and q['available']and p['address']==q['address']and p['FNV']!=q['FNV']:
                        events.append(dict(window=w,request=r['pending_sequence'],HW_tick=r['HW_tick'],row_probe=i,
                            source_state=r.get('source_state'),
                            physical_address=q['address'],first_vector=previous['sequence'],last_vector=r['sequence'],
                            bounds_ns=[previous['begin_ns'],r['end_ns']],HEVC_wait_observed_at_both_ends=bool(previous.get('qualified_HEVC_completion_wait')and wait),
                            EOF_proven=False,unique_writer_proven=False,**(dict(probe_name=q['name'])if verified else{})))
            if compatible:
                for event in [7,15]:
                    bit=1<<event
                    if routed[event]and all(int(v.get('MMIO_read_mask','0'),16)&3==3 and int(v['CORE_A_mask_raw'],16)&bit for v in [previous,r]):
                        if not int(previous['CORE_A_raw'],16)&bit and int(r['CORE_A_raw'],16)&bit:
                            EOF.append(dict(window=w,request=r['pending_sequence'],event=event,
                                source_state=r.get('source_state'),
                                name='WDMA2_EOF'if event==7 else'RSZ_OUT2_EOF',bounds_ns=[previous['begin_ns'],r['end_ns']],unique_image_generation_proven=False))
            previous=r
        close_wait()
    pub={(p['window'],p['request']):p for p in publications}
    for e in events+EOF:
        p=pub.get((e['window'],e['request']))
        if p:
            e['elapsed_from_active_header_publication_bounds_ms']=[(e['bounds_ns'][0]-p['bounds_ns'][1])/1e6,(e['bounds_ns'][1]-p['bounds_ns'][0])/1e6]
            e['reference_scope']='Linux observation bounds only; no absolute Linux/RTOS clock subtraction.'
    for c in result['WDMA2_HEVC_cross_request_candidates']:
        c['request_distance']=(c['encoder_observed_request']-c['output_request'])&0xffffffff
        c['unobserved_intermediate_generations_possible']=c['request_distance']!=1
        c['exact_handoff_latency_proven']=False
    byid={r['sequence']:r for r in rows}
    for q in result['metadata_free_transitions']:
        q['HEVC_match_under_window_mapping']=byid[q['processing_vector']].get('Que_HEVC_match_under_window_mapping',False)
    out=dict(schema='R11B_VERIFIED_WAIT_EDGE1'if verified else'R10Z_STAGE_WINDOWS1',vector_count=len(vectors),windows=windows,
             qualified_vectors=sum(r.get('qualified_request',False)for r in vectors),
             endpoint_matched_vectors=sum(r.get('native_endpoints_matched',False)for r in vectors),
             mapped_Que_HEVC_matches=sum(r.get('mapped_Que_HEVC_match',False)for r in vectors),
             qualified_HEVC_wait_vectors=sum(r.get('qualified_HEVC_completion_wait',False)for r in vectors),
             HEVC_ioctl_observations=[dict(window=r['window'],vector=r['sequence'],kind='submit'if r['worker_proof']==1 else'result',
                 bounds_ns=[r['begin_ns'],r['end_ns']],HAL=r.get('HAL'),mapped_image_matched=r.get('mapped_Que_HEVC_match',False))
                 for r in vectors if r.get('worker_proof')in [1,2]and r.get('qualified_request')],
             same_job_HEVC_wait_observation_spans=wait_spans,row_change_intervals=events,qualified_EOF_rising_edges=EOF,
             active_header_publication_intervals=publications,
             partial_Que_clearing_candidates=partial_returns,task_discovery=[r for r in rows if r['kind']==6],
             independent_CPU_ledgers=[r for r in rows if r['kind']==7],
             metadata_scan_records=[r for r in rows if r['kind']==8],
             RSZ2_exact_busy_time_ms=None,WDMA2_exact_busy_time_ms=None,HEVC_pure_hardware_busy_time_ms=None,DDR_contention_time_ms=None,
             limitations=['RSZ2/WDMA2 EOF only when native route and enable mask verified; a masked zero is unavailable.',
                 'Changed output-row fingerprints bracket visible writes; unchanged rows never prove idle or a stall, and no probe proves full-frame EOF.',
                 'HEVC original completion-wait observations include firmware, IRQ and scheduling, and are not a pure codec-duration counter.',
                 'No verified DDR traffic/arbiter-stall counter is enabled or read. Correlated activity alone never proves DDR contention.',
                 'Window bookends verify mappings; schema4 also verifies original instruction words twice and task lifetime before positive proof. No pixel writes, IRQ/ACK/selector changes, new RTOS hooks or codec-driver calls.'])
    result['stage_windows']=out
    if result['schema']in ['NXL_BUFFER_MEMORY5','NXL_BUFFER_MEMORY6']:
        from video_tail_windows import finish_tail
        finish_tail(result,native)
