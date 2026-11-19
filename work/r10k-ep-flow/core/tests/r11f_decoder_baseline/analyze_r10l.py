"""Decode the three R10l files; all time domains and inference limits explicit."""
from pathlib import Path
import argparse,collections,json,statistics,struct
from analyze_video_counters import fnv,decode
from video_stage_flow import decode_stage_flow
from video_frame_flow import decode_frame_flow
from video_ep_flow import decode_ep_flow
from video_ep_flow7 import decode_ep_flow7
from video_ep_flow8 import decode_ep_flow8
from video_ldc_flow9 import decode_ldc_flow9
from video_ipc_flow10 import decode_ipc_flow10
from video_ipc_flow11 import decode_ipc_flow11
FAST=struct.Struct('<QQ4I64I2I')
FAST2=struct.Struct('<QQ4I64I4I2I')
HEVC=struct.Struct('<Q10IQ2I')
KINDS={1:'submit_before_ioctl',2:'native_wait_return',3:'result_retrieved_and_mutex_unlocked',
       4:'encoder_input_release_enter',5:'encoder_input_release_return',6:'trace_window_on',7:'trace_window_off'}
def decode_fast(path):
    data=Path(path).read_bytes()
    if len(data)<128 or data[:8]!=b'NXLFAST1':raise ValueError('Invalid fast file header')
    schema,size,capacity,count,period,flags=struct.unpack_from('<6I',data,8)
    last,maxgap,lost,cpu=struct.unpack_from('<4Q',data,32)
    if (schema,size,capacity,period) not in [(1,296,12000,100),(2,312,12000,100),(3,312,12000,100),(4,312,12000,100)] or count>capacity or len(data)!=128+count*size or (schema<4 and any(data[68:128])) or struct.unpack_from('<I',data,64)[0]!=(fnv(data[:64]+bytes(4)+data[68:128])if schema==4 else fnv(data[:64])):raise ValueError('Fast header integrity/size failed')
    samples=[];bounds={};generation=0;prev=None;previous_seq=None;formats=set();available=0;rejected=[];terminals=0;recovered=0;retry_pending=False
    for i in range(count):
        raw=data[128+i*size:128+(i+1)*size];w=(FAST if schema==1 else FAST2).unpack(raw)
        if w[2]!=i or w[-2]!=(fnv(raw[:-8])^0xa7316d29) or w[-1] or w[1]<w[0]:raise ValueError('Fast record integrity failed')
        start,end,seq,driver,preset,valid=w[:6];latency=list(w[6:70])
        if valid&~(3 if schema==1 else 7 if schema==2 else 2047 if schema==4 else 511):raise ValueError('Unknown fast valid bits')
        if valid&1 and latency[:2]not in [[0x35544c56,5],[0x33544c56,4],[0x36544c56,6],[0x37544c56,7],[0x38544c56,8],[0x39544c56,9],[0x41544c56,10],[0x42544c56,11]]:raise ValueError('Bad frame producer metadata')
        if valid&4 and (schema not in (2,3,4) or not valid&1 or latency[:2]!=[0x36544c56,6] or w[73]):raise ValueError('Invalid EP observer metadata')
        if schema==2 and not valid&4 and any(w[70:74]):raise ValueError('Uncommitted EP observer metadata')
        if schema>=3:
            if valid&8 and (not valid&1 or latency[:2]!=[0x42544c56,11]):raise ValueError('Invalid native stage producer')
            if valid&0x1f0 and not valid&8:raise ValueError('Uncommitted native stage flags')
            if not valid&12 and any(w[70:74]):raise ValueError('Uncommitted observer extension')
        if schema==4:
            if valid&512:
                if valid&1024 or valid&3==3 or retry_pending:raise ValueError('Invalid rejected fast attempt')
                rejected.append(dict(sequence=seq,begin_ns=start,end_ns=end,valid=hex(valid),latency=latency))
                retry_pending=True
                continue
            if bool(valid&1024)!=retry_pending:raise ValueError('Unpaired fast retry')
            if retry_pending and start<rejected[-1]['end_ns']:raise ValueError('Reordered fast retry')
            recovered+=int(retry_pending and valid&3==3);retry_pending=False
        terminals+=1
        if valid&3==3:
            available+=1;formats.add(latency[1]);slots,base,width=(7,7,8)if latency[1]==5 else(7,8,8)if latency[1]==6 else(4,16,12)if latency[1]in (10,11) else(8,8,7)
            native=latency[2]
            if previous_seq is not None and native<previous_seq and not(previous_seq>0xf0000000 and native<0x10000000):generation+=1;prev=None
            for j in range(slots):
                n=latency[base+j*width]
                age=((native-n)&0xffffffff)-int(native<n and native!=0 and n!=0)
                if not n or age>=slots:continue
                key=(generation,n)
                if key not in bounds:bounds[key]=dict(earliest_linux_ns=prev[0]if prev and ((n-prev[1])&0xffffffff)<0x80000000 else None,latest_linux_ns=end)
            prev=(start,native);previous_seq=native
        else:prev=None
        samples.append(dict(t=end,start=start,sequence=seq,valid=65536 if valid&3==3 else 0,latency=latency,
                            rtos=[driver,0,preset,0],source=[0]*6,hevc=[0]*10,epoch=generation,mark=0,EP_observer=list(w[70:74]) if valid&4 else None,pipeline_fast=dict(flags=hex(valid),raw_words=list(w[70:74]),full16_header_equal=bool(valid&256),read_mask=hex((valid>>4)&15))if schema>=3 and valid&8 else None))
    if schema==4:
        meta=struct.unpack_from('<15I',data,68)
        if retry_pending or meta[:4]!=(terminals,len(rejected),recovered,terminals-available)or any(meta[4:]):raise ValueError('Fast retry ledger mismatch')
    started=samples[0]['start']if samples else 0
    if len(formats)>1:raise ValueError('Mixed timer formats require separate capture files')
    flow=(decode_ipc_flow11 if formats=={11} else decode_ipc_flow10 if formats=={10} else decode_ldc_flow9 if formats=={9} else decode_ep_flow8 if formats=={8} else decode_ep_flow7 if formats=={7} else decode_ep_flow if formats=={6} else decode_stage_flow if formats=={5} else decode_frame_flow)(samples,started)
    for r in flow['records']:r['callback_publish_linux_bounds']=bounds.get((r['generation'],r['sequence']))
    observer=[dict(sample=s['sequence'],linux_ns=s['t'],current_EP_tick=s['EP_observer'][0],all_callback_count=s['EP_observer'][1],outside_IPC_window_count=s['EP_observer'][2]) for s in samples if s['EP_observer'] is not None]
    return dict(file_schema=schema,pipeline_fast_samples=[dict(sample=s['sequence'],begin_ns=s['start'],end_ns=s['t'],**s['pipeline_fast'])for s in samples if s.get('pipeline_fast')is not None],EP_observer_samples=observer,EP_frame_coverage_complete=bool(formats in [{6},{7},{8},{9},{10},{11}] and flow['records'] and all(r['valid_for_EP_statistics'] for r in flow['records']) and not flow['missed_frame_sequences'] and not lost and available==terminals and not(flags&15)),schema='R10L_FAST_ANALYSIS1',drains=terminals,raw_attempt_records=count,rejected_attempts=rejected,recovered_retry_drains=recovered,period_ms=period,flags=hex(flags),
        maximum_drain_gap_ms=maxgap/1e6,producer_sequences_overwritten=lost,
        producer_schemas=sorted(formats),valid_timer_drains=available,unavailable_timer_drains=terminals-available,
        coverage_complete=count>0 and available==terminals and flow['retained_frames']>0 and not(flags&15) and not lost and not flow['missed_frame_sequences'],
        frames=flow,time_domain='RTOS 26MHz low tick; Linux bounds only bracket publication')
def decode_hevc(path):
    data=Path(path).read_bytes()
    if data[:4]==struct.pack('<I',0x31504c56):
        from video_pipeline import decode_pipeline
        pipeline=decode_pipeline(data)
        return dict(schema='R10X_PIPELINE_EXPORT2'if pipeline['schema']=='NXL_PIPELINE2'else 'R10W_PIPELINE_EXPORT1',reason_flags='0x200',trace_windows=0,traps=0,
                    disabled_for_passive_start_recovery=True,detach_verified=True,events=[],requests=[],
                    pipeline=pipeline,limitations='HEVC breakpoints remain disabled; this slot contains read-only native completion markers.')
    if len(data)<128:raise ValueError('Truncated HEVC header')
    h=struct.unpack_from('<32I',data)
    if h[:4]!=(0x31544248,1,64,5000) or h[4]>5000 or len(data)!=128+64*h[4] or h[31]!=fnv(data[:124]):raise ValueError('HEVC header integrity/size failed')
    rows=[]
    for i in range(h[4]):
        raw=data[128+64*i:192+64*i];v=HEVC.unpack(raw)
        if v[1]!=i or v[-2] or v[-1]!=(fnv(raw[:60])^0x428a9c31) or v[2]not in KINDS:raise ValueError('HEVC record integrity failed')
        ns,seq,kind,request,y,uv,desc,obj,status,pc,flags,resume,_,_=v
        if kind<=5 and (not flags&1 or resume<ns):raise ValueError('HEVC stop/return order failed')
        rows.append(dict(sequence=seq,kind=KINDS[kind],kind_id=kind,request=request,
            linux_ns=ns,resume_linux_ns=resume,observed_stop_us=(resume-ns)/1000 if kind<=5 else None,
            Y_address=hex(y),UV_address=hex(uv),descriptor=hex(desc),object=hex(obj),
            raw_return_word=hex(status),PC=hex(pc),flags=hex(flags)))
    requests=collections.defaultdict(list)
    for r in rows:
        if r['request']:requests[r['request']].append(r)
    rr=[]
    for request,events in sorted(requests.items()):
        keyed=collections.defaultdict(list)
        for e in events:keyed[e['kind_id']].append(e)
        submit=keyed.get(1,[])
        item=dict(request=request,complete=False,events=events)
        if len(submit)==1:
            first=submit[0];item.update(Y_address=first['Y_address'],UV_address=first['UV_address'],submit_linux_ns=first['linux_ns'])
            ok=all(len(keyed.get(k,[]))==1 for k in [1,2,3])
            ok=ok and all(e['Y_address']==first['Y_address']and e['UV_address']==first['UV_address']for k in [1,2,3]for e in keyed[k])
            if ok:
                times=[keyed[k][0]['linux_ns']for k in [1,2,3]];ok=times==sorted(times)
            if ok:
                item.update(complete=True,submit_to_wait_ms=(times[1]-times[0])/1e6,
                    wait_to_result_and_unlock_ms=(times[2]-times[1])/1e6,
                    measured_stop_us=sum(e['observed_stop_us']or 0 for e in events))
                if all(len(keyed.get(k,[]))==1 for k in [4,5]):
                    a,b=keyed[4][0],keyed[5][0]
                    if a['Y_address']==first['Y_address']==b['Y_address'] and a['linux_ns']<=b['linux_ns']:
                        item['encoder_input_release_ms']=(b['linux_ns']-a['linux_ns'])/1e6
        rr.append(item)
    return dict(schema='R10L_HEVC_ANALYSIS1',state=h[5],reason_flags=hex(h[6]),
        hardware_breakpoint_slots=h[9],trace_windows=h[10],traps=h[11],detach_verified=bool(h[12]),
        disabled_for_passive_start_recovery=bool(h[6]&512),
        maximum_observed_stop_ms=(h[22]|h[23]<<32)/1e6,worker_CPU_seconds=(h[20]|h[21]<<32)/1e9,
        events=rows,requests=rr,time_domain='Linux CLOCK_MONOTONIC',
        limitations='Trap wakeup and kernel single-step perturb timing; recorded stop time is only part of observer cost. Native input-port release return is not proof every DMA/reference retired. Empty/unsupported trace is never treated as no encoding.')
def analyze(folder):
    folder=Path(folder);counts,_=decode(folder/'counts.bin');fast=decode_fast(folder/'frame-flow.bin');hevc=decode_hevc(folder/'hevc-flow.bin')
    for q in hevc['requests']:
        if 'Y_address'not in q:continue
        candidates=[]
        for r in fast['frames']['records']:
            b=r.get('callback_publish_linux_bounds')
            if r.get('output_Y_address')!=q['Y_address']or r.get('EP_Y_address_changed')or not b or b['earliest_linux_ns']is None:continue
            if b['earliest_linux_ns']<=q['submit_linux_ns']<=b['latest_linux_ns']:candidates.append([r['generation'],r['sequence']])
        q['RTOS_address_and_publication_window_candidates']=candidates
        q['native_cross_domain_frame_identity_proven']=False
    if fast['frames'].get('schema')=='NXL_LDC_FLOW9':
        from video_ldc_flow9 import associate_routes,context_runs,compare_flows
        started,runs=context_runs(folder/'counts.bin')
        associate_routes(fast['frames'],counts.get('EP_route_evidence'),started,runs)
        fast['LDC_frame_coverage_complete']=bool(fast['coverage_complete'] and fast['frames']['records'] and all(r['valid_for_LDC_statistics'] for r in fast['frames']['records']))
        fast['EP_frame_coverage_complete']=fast['LDC_frame_coverage_complete']
        fast['LDC_flow_comparison']=compare_flows(fast['frames'])
    if fast['frames'].get('schema')=='NXL_IPC_BUFFER_FLOW10':
        from video_ipc_flow10 import associate_routes,context_runs,compare_flows
        started,runs=context_runs(folder/'counts.bin')
        associate_routes(fast['frames'],counts.get('EP_route_evidence'),started,runs,counts.get('IPC_link_evidence'))
        fast['LDC_frame_coverage_complete']=bool(fast['coverage_complete']and fast['frames']['records']and all(r['valid_for_LDC_statistics']for r in fast['frames']['records']))
        fast['EP_frame_coverage_complete']=fast['LDC_frame_coverage_complete']
        fast['IPC_buffer_frame_coverage_complete']=bool(fast['coverage_complete']and fast['frames']['records']and all(r['valid_for_buffer_statistics']for r in fast['frames']['records']))
        fast['IPC_buffer_flow_comparison']=compare_flows(fast['frames'])
    if fast['frames'].get('schema')=='NXL_IPC_BIT6_FLOW11':
        from video_ipc_flow11 import associate_routes,context_runs,compare_flows
        started,runs=context_runs(folder/'counts.bin')
        associate_routes(fast['frames'],counts.get('EP_route_evidence'),started,runs,counts.get('IPC_link_evidence'))
        fast['LDC_frame_coverage_complete']=bool(fast['coverage_complete']and fast['frames']['records']and all(r['valid_for_LDC_statistics']for r in fast['frames']['records']))
        fast['EP_frame_coverage_complete']=fast['LDC_frame_coverage_complete']
        fast['IPC_buffer_frame_coverage_complete']=bool(fast['coverage_complete']and fast['frames']['records']and all(r['valid_for_buffer_statistics']for r in fast['frames']['records']))
        fast['IPC_bit6_frame_coverage_complete']=bool(fast['coverage_complete']and fast['frames']['records']and all(r['valid_for_bit6_statistics']for r in fast['frames']['records']))
        fast['IPC_buffer_flow_comparison']=compare_flows(fast['frames'])
    if 'pipeline'in hevc:
        from video_pipeline import associate_pipeline
        associate_pipeline(hevc['pipeline'],fast['frames'])
    buffers=None
    if (folder/'buffer-flow.bin').exists():
        from video_buffers import decode_buffers
        buffers=decode_buffers(folder/'buffer-flow.bin',fast['frames'])
        from analyze_video_counters import HEADER
        runtime=HEADER.unpack_from((folder/'counts.bin').read_bytes())
        if buffers['capture_identity']!=dict(cookie=runtime[18],pid=runtime[13],started_ns=runtime[7],camera_pid=runtime[14],camera_ticks=runtime[11],modules_base=runtime[15]):
            raise ValueError('Buffer file belongs to another capture/lifetime')
        if counts['revision'].startswith(('R10y','R10z','R11a','R11b','R11c','R11d','R11e')):
            if counts['revision'].startswith('R10z')and buffers['schema']!='NXL_BUFFER_MEMORY2':raise ValueError('R10z stage buffer schema missing')
            if counts['revision'].startswith('R11e'):
                if buffers['schema']!='NXL_BUFFER_MEMORY7':raise ValueError('R11e memory arena schema missing')
                fields={}
                for line in (folder/'ADMISSION').read_text().splitlines()[1:]:
                    if '='not in line:raise ValueError('Admission record malformed')
                    k,v=line.split('=',1)
                    if k in fields:raise ValueError('Duplicate admission field')
                    fields[k]=v
                config=buffers['memory_experiment']['config']
                required=dict(revision='R11e MEMORY_ARENA',pid=str(runtime[13]),cookie=str(runtime[18]),
                    status=str(config['memory_status']),reason=str(config['reason']),
                    initial_MemFree_kB=str(config['initial_MemFree_kB']),working_set_bytes='4194304',
                    extra_allocation_bytes='0',allocation='REUSED_INACTIVE_CAPTURE_ARENA',
                    arena_data_offset='12791808',arena_end_offset='16990208',capture_RAM_bytes='18560000',
                    admission_min_kB='16384',running_min_kB='8192',readonly_flags=str(config['private_readonly_flags']))
                if (folder/'ADMISSION').read_text().splitlines()[0]!='NXLMEMADMISSION2'or any(fields.get(k)!=v for k,v in required.items()):
                    raise ValueError('Admission record identity/policy mismatch')
            if counts['revision'].startswith('R11d')and buffers['schema']!='NXL_BUFFER_MEMORY6':raise ValueError('R11d memory experiment schema missing')
            if counts['revision'].startswith('R11c')and buffers['schema']!='NXL_BUFFER_MEMORY5':raise ValueError('R11c tail schema missing')
            if counts['revision'].startswith('R11b')and buffers['schema']!='NXL_BUFFER_MEMORY4':raise ValueError('R11b verified wait/edge schema missing')
            if counts['revision'].startswith('R11a')and buffers['schema']!='NXL_BUFFER_MEMORY3':raise ValueError('R11a asynchronous map schema missing')
            expected='NXLVREADY1\npid=%u\ncookie=%u\nrecord_bytes=%u\ncapacity=%u\n'%(runtime[13],runtime[18],runtime[3],runtime[4])
            if not(folder/'READY').is_file()or(folder/'READY').read_text()!=expected or not(folder/'DONE').is_file():
                raise ValueError('R10y READY/DONE missing or capture identity differs')
            expected_done='NXLVIDDONE1\nsamples=%u\nrecord_bytes=%u\nbytes=%u\nlast_period_ms=%u\nmax_sample_ns=%u\nreason_flags=%u\n'%(runtime[5],runtime[3],runtime[2]+runtime[5]*runtime[3],runtime[19],runtime[25],runtime[20])
            if(folder/'DONE').read_text()!=expected_done:raise ValueError('R10y DONE receipt differs from saved counters')
    elif counts['revision'].startswith(('R10y','R10z','R11a','R11b','R11c','R11d','R11e')):
        raise ValueError('R10y/R10z buffer-flow.bin missing: four-file export incomplete')
    return dict(schema='R10L_COMBINED1',buffers=buffers,counts=counts,fast=fast,hevc=hevc,pipeline=hevc.get('pipeline'),
        cross_domain_policy='No Linux nanoseconds minus RTOS ticks. Address/window candidates remain candidates; unmatched buffers may indicate another surface or conversion. No automatic DDR/backpressure root-cause verdict.')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--out',type=Path);a=p.parse_args();result=analyze(a.folder)
    if a.out:a.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(fast_frames=result['fast']['frames']['retained_frames'],coverage_complete=result['fast']['coverage_complete'],HEVC_windows=result['hevc']['trace_windows'],HEVC_requests=len(result['hevc']['requests']),HEVC_reason=result['hevc']['reason_flags']),ensure_ascii=False,indent=2))
if __name__=='__main__':main()
