"""R11d CPU-matched working-set intervention; logical reads are not bus bytes."""
import collections, itertools, math, statistics
MAGIC=0x31504d44
NAMES={0:'baseline',1:'small_4KiB',2:'large_5ms_plus_small',3:'large_20ms'}
def u64(w,i):return w[i]|w[i+1]<<32
def mode(epoch,seed):
    a=[[0,1,3,2],[1,2,0,3],[2,3,1,0],[3,0,2,1]]
    return a[(epoch//4+(seed&3))%4][3-epoch%4 if seed&4 else epoch%4]
def decode_record(kind,flags,d,begin,end):
    if flags or d[:2]!=[MAGIC,1]or begin!=end:raise ValueError('Memory intervention record identity')
    if kind==10:
        expected=[8388608,4096,20000000,5000000,100,6000,8,32,2000,300]
        if d[4:14]!=expected or d[2]not in range(1,7)or d[3]not in range(1,6)or d[14]>7 or d[15]&~7:
            raise ValueError('Memory intervention policy/constants')
        if d[18]>8 or d[19]not in [0,1]or d[20]not in [0,1,2]or d[23]>2048 or d[25]!=2048 or d[34:37]!=[30000000,96,1]:
            raise ValueError('Memory intervention allocation/cache policy')
        if d[37]not in [0,1,2,3,4,5,64]or d[38]not in [*range(32),0xffffffff]or d[58:90]!=[mode(i,d[14])for i in range(32)]or d[92]not in [0,1]or any(d[101:]):
            raise ValueError('Memory intervention schedule/reserved data')
        if d[20]==1 and (d[23]!=2048 or d[21]==0xffffffff or d[21]>d[22]):raise ValueError('Memory intervention PFN qualification')
        return dict(memory_event=d[2],memory_status=d[3],seed=d[14],private_readonly_flags=d[15],
            initial_MemFree_kB=d[16],exposed_data_unified_cache_bytes=d[17],cache_entries=d[18],cache_inventory_incomplete=bool(d[19]),
            pagemap_status=d[20],physical_min=hex(d[21]),physical_max_page=hex(d[22]),present_pages=d[23],PFN_hash=hex(d[24]),
            physical_samples=[hex(x)for x in d[26:34]],reason=d[37],epoch_index=None if d[38]==0xffffffff else d[38],ticks_total=d[39],
            experiment_start_ns=u64(d,40),experiment_finish_ns=u64(d,42),cumulative_CPU_ns=u64(d,44),
            cumulative_logical_large_bytes=u64(d,46),cumulative_logical_small_bytes=u64(d,48),recorded_groups=d[90],buffer_checksum=hex(d[91]),
            camera_source_key=[hex(x)for x in d[50:58]],live_identity_valid=bool(d[92]),raw_live_identity=[hex(x)for x in d[93:101]],schedule=[mode(i,d[14])for i in range(32)])
    if kind!=11 or not 1<=d[2]<=10 or any(d[4:7])or any(d[7+d[2]*24:]):raise ValueError('Memory intervention tick packing')
    ticks=[]
    for i in range(d[2]):
        w=d[7+i*24:31+i*24];a,b=u64(w,0),u64(w,2);large,small=u64(w,8),u64(w,10)
        if a>b or b>begin or w[4]>=32 or w[5]not in NAMES or w[12]&~127 or w[7]!=b-a or not a:
            raise ValueError('Memory intervention tick time/mode/flags')
        if (large+small)%65536 or bool(w[12]&1)!=bool(large+small)or (large+small)//65536>8192 or not a<=u64(w,20)<=b or w[22]>w[6]:
            raise ValueError('Memory intervention tick work ledger')
        if w[5]==0 and (large or small)or w[5]==1 and large or w[5]==3 and small:
            raise ValueError('Memory intervention workset mismatch')
        if w[12]&10 and (large or small):raise ValueError('Memory intervention skipped work has dose')
        if not large and (u64(w,20)!=a or w[22]):raise ValueError('Memory intervention absent large interval')
        ticks.append(dict(begin_ns=a,end_ns=b,epoch=w[4],condition=w[5],condition_name=NAMES[w[5]],CPU_ns=w[6],wall_ns=w[7],
            logical_large_bytes=large,logical_small_bytes=small,flags=hex(w[12]),native_completed_seq_before=w[13],native_completed_seq_after=w[14],
            CPU0_kHz_cached=w[15],governor_cached=w[16],MemFree_kB_cached=w[17],DDR_A_Hz_cached=w[18],batches=(large+small)//65536,telemetry_age_ms=w[19],
            large_read_end_ns=u64(w,20),large_CPU_ns=w[22],DDR_A_double_read_status=w[23]))
    return dict(memory_ticks=ticks)
def avg(x):return statistics.mean(x)if x else None
def describe(x):
    x=sorted(x)
    return dict(n=len(x),mean=avg(x),median=statistics.median(x),min=x[0],max=x[-1])if x else dict(n=0,mean=None)
def signflip(values):
    if not values:return None
    observed=abs(statistics.mean(values));n=len(values)
    return sum(abs(sum(s*x for s,x in zip(sign,values))/n)>=observed-1e-12 for sign in itertools.product([-1,1],repeat=n))/(2**n)
def finish_experiment(rows,native=None):
    meta=[r for r in rows if r['kind']==10];ticks=[t for r in rows if r['kind']==11 for t in r['memory_ticks']]
    if not meta or meta[0]['memory_event']!=1 or sum(r['memory_event']==1 for r in meta)!=1:raise ValueError('Memory intervention config missing/duplicate')
    config=meta[0];seed=config['seed']
    for r in meta:
        if r['seed']!=seed or r['schedule']!=config['schedule']or r['private_readonly_flags']!=config['private_readonly_flags']:
            raise ValueError('Memory intervention immutable admission identity')
    starts=[r for r in meta if r['memory_event']==2];ends=[r for r in meta if r['memory_event']==4];final=[r for r in meta if r['memory_event']==6]
    aborts=[r for r in meta if r['memory_event']==5]
    if len(starts)>1 or len(ends)>1 or len(final)>1:raise ValueError('Memory intervention duplicate lifetime event')
    start=starts[0]['experiment_start_ns']if starts else None
    if starts and (starts[0]['memory_status']!=2 or not start or starts[0]['begin_ns']!=start or config['memory_status']!=1):
        raise ValueError('Memory intervention invalid start')
    if starts:
        v=[int(x,16)for x in starts[0]['raw_live_identity']]
        k=[int(x,16)for x in starts[0]['camera_source_key']]
        if not starts[0]['live_identity_valid']or not(v[0]==k[0]and v[2]==k[1]and v[4]==2 and v[6]==k[7]and v[7]==v[1]+0x3c0):
            raise ValueError('Memory intervention live source/HAL identity mismatch')
    previous=0
    for t in ticks:
        if not start or t['condition']!=mode(t['epoch'],seed)or t['epoch']!=int((t['begin_ns']-start)//6000000000)or t['begin_ns']<previous:
            raise ValueError('Memory intervention tick epoch/order')
        previous=t['end_ns']
    if ends and (ends[0]['memory_status']!=3 or ends[0]['experiment_finish_ns']<start+192000000000):
        raise ValueError('Memory intervention premature completion')
    if final and (final[0]['ticks_total']!=len(ticks)or final[0]['cumulative_CPU_ns']!=sum(t['CPU_ns']for t in ticks)or
       final[0]['cumulative_logical_large_bytes']!=sum(t['logical_large_bytes']for t in ticks)or
       final[0]['cumulative_logical_small_bytes']!=sum(t['logical_small_bytes']for t in ticks)):
        raise ValueError('Memory intervention final ledger mismatch')
    frames=(native or {}).get('records',[]);ordered=sorted(frames,key=lambda r:(r['generation'],r['sequence_ordinal']))
    cohorts=[]
    for epoch in range(32):
        condition=mode(epoch,seed);lo=start+epoch*6000000000+2000000000 if start else 0;hi=lo+3700000000
        tt=[t for t in ticks if t['epoch']==epoch and lo<=t['begin_ns']<=t['end_ns']<=hi]
        ff=[]
        for a,b in zip(ordered,ordered[1:]):
            if a['generation']!=b['generation']or b['sequence_ordinal']!=a['sequence_ordinal']+1 or not a.get('valid_for_bit6_statistics')or not b.get('valid_for_bit6_statistics'):continue
            if a.get('source_state')!=2 or b.get('source_state')!=2 or a.get('driver_mode')!='0x1717'or a.get('IPC_preset')!='0xe005':continue
            x=a.get('callback_publish_linux_bounds');y=b.get('callback_publish_linux_bounds')
            if not x or not y or x['earliest_linux_ns']is None or y['earliest_linux_ns']is None:continue
            if not(lo<=x['earliest_linux_ns']<=x['latest_linux_ns']<=hi and lo<=y['earliest_linux_ns']<=y['latest_linux_ns']<=hi):continue
            if not 0<a.get('cycle_ms',0)<200 or not 0<a.get('IPC_bit6_to_final_IRQ_ms',0)<200:continue
            ff.append(a)
        qualified=[t for t in tt if int(t['flags'],16)in [0,1]and t['telemetry_age_ms']<=1500 and t['MemFree_kB_cached']>=8192]
        cpu=[t['CPU_ns']/1e6 for t in qualified];large=sum(t['logical_large_bytes']for t in qualified)
        duration=(tt[-1]['end_ns']-tt[0]['begin_ns'])/1e9 if len(tt)>1 else 0
        bymod=collections.defaultdict(list)
        for f in ff:bymod[f['sequence']%10].append(f['IPC_bit6_to_final_IRQ_ms'])
        equal_mod_mean=avg([avg(bymod[k])for k in range(10)])if len(bymod)==10 else None
        cohorts.append(dict(epoch=epoch,block=epoch//4,condition=condition,condition_name=NAMES[condition],
            steady_begin_ns=lo if start else None,steady_end_ns=hi if start else None,
            ticks=len(tt),qualified_ticks=len(qualified),CPU_ms_per_tick=describe(cpu),
            logical_large_MiB_per_s=large/1048576/duration if duration else None,
            CPU0_kHz_histogram=dict(collections.Counter(t['CPU0_kHz_cached']for t in qualified)),
            DDR_A_Hz_histogram=dict(collections.Counter(t['DDR_A_Hz_cached']for t in qualified)),
            governor_histogram=dict(collections.Counter(t['governor_cached']for t in qualified)),
            DDR_A_double_read_status_histogram=dict(collections.Counter(t['DDR_A_double_read_status']for t in qualified)),
            recording_cycles=describe([f['cycle_ms']for f in ff]),bit6_to_bit2_ms=describe([f['IPC_bit6_to_final_IRQ_ms']for f in ff]),
            bit6_to_bit2_equal_mod10_mean_ms=equal_mod_mean,frame_sequences=[f['sequence']for f in ff],
            eligible=bool(start and len(ff)>=50 and len(tt)>=30 and len(qualified)>=.9*len(tt)and equal_mod_mean is not None and
                (condition==0 or cpu and 18<=statistics.mean(cpu)<=22))))
    comparisons=[];matched=[]
    for block in range(8):
        q={c['condition']:c for c in cohorts if c['block']==block}
        if not all(q[k]['eligible']for k in range(4)):continue
        baseline,control,low,high=[q[k]for k in range(4)]
        CPU_match=all(abs(q[k]['CPU_ms_per_tick']['mean']/control['CPU_ms_per_tick']['mean']-1)<=.1 for k in [2,3])
        dominant=lambda h:max(h,key=h.get)if h else None
        cpu_frequency_match=all(dominant(q[k]['CPU0_kHz_histogram'])==dominant(control['CPU0_kHz_histogram'])not in [None,0]for k in [2,3])
        DDR_frequency_match=all(dominant(q[k]['DDR_A_Hz_histogram'])==dominant(control['DDR_A_Hz_histogram'])not in [None,0]for k in [2,3])
        DDR_status_ok=all(q[k]['DDR_A_double_read_status_histogram'].get(1,0)==q[k]['qualified_ticks']for k in [1,2,3])
        governor_match=all(dominant(q[k]['governor_histogram'])==dominant(control['governor_histogram'])not in [None,0]for k in [2,3])
        dose=bool(low['logical_large_MiB_per_s']and high['logical_large_MiB_per_s']>=1.8*low['logical_large_MiB_per_s'])
        value=lambda c:c['bit6_to_bit2_equal_mod10_mean_ms']
        z=dict(block=block,high_minus_control_ms=value(high)-value(control),low_minus_control_ms=value(low)-value(control),
            high_minus_low_ms=value(high)-value(low),control_minus_baseline_ms=value(control)-value(baseline),
            CPU_matched=CPU_match,CPU0_dominant_frequency_matched=cpu_frequency_match,DDR_A_dominant_frequency_matched=DDR_frequency_match,
            governor_matched=governor_match,DDR_double_read_status_qualified=DDR_status_ok,large_workset_dose_distinct=dose)
        comparisons.append(z)
        if CPU_match and cpu_frequency_match and DDR_frequency_match and DDR_status_ok and governor_match and dose:matched.append(z)
    high=[r['high_minus_control_ms']for r in matched];low=[r['low_minus_control_ms']for r in matched]
    completed=bool(starts and ends and final and not aborts and len({t['epoch']for t in ticks})==32)
    evidence_complete=completed and starts[0]['live_identity_valid']and len(matched)==8 and bool((native or {}).get('retained_frames'))and (native or {}).get('missed_frame_sequences',0)==0
    recovery=[]
    finish=ends[0]['experiment_finish_ns']if ends else None
    for a,b in zip(ordered,ordered[1:]):
        if not finish or a['generation']!=b['generation']or b['sequence_ordinal']!=a['sequence_ordinal']+1:continue
        if not(a.get('valid_for_bit6_statistics')and b.get('valid_for_bit6_statistics')and a.get('source_state')==b.get('source_state')==2):continue
        x=a.get('callback_publish_linux_bounds');y=b.get('callback_publish_linux_bounds')
        if not x or not y or x['earliest_linux_ns']is None or y['earliest_linux_ns']is None:continue
        if min(x['earliest_linux_ns'],y['earliest_linux_ns'])<finish+5000000000:continue
        if a.get('cycle_ms')and a.get('IPC_bit6_to_final_IRQ_ms'):recovery.append(a)
    mod=collections.defaultdict(list)
    for a in recovery:mod[a['sequence']%10].append(a['IPC_bit6_to_final_IRQ_ms'])
    recovery_mean=avg([avg(mod[k])for k in range(10)])if len(mod)==10 else None
    baseline_means=[c['bit6_to_bit2_equal_mod10_mean_ms']for c in cohorts if c['condition']==0 and c['eligible']]
    tolerance=max(1.,2*statistics.stdev(baseline_means))if len(baseline_means)>1 else None
    recovered=bool(len(recovery)>=100 and recovery_mean is not None and tolerance is not None and abs(recovery_mean-avg(baseline_means))<=tolerance)
    withdrawal=[]
    for a,b in zip(cohorts,cohorts[1:]):
        if a['eligible']and b['eligible']and a['condition']in [2,3]and b['condition']in [0,1]:
            withdrawal.append(dict(from_epoch=a['epoch'],to_epoch=b['epoch'],from_condition=a['condition'],to_condition=b['condition'],
                bit6_to_bit2_drop_ms=a['bit6_to_bit2_equal_mod10_mean_ms']-b['bit6_to_bit2_equal_mod10_mean_ms']))
    p=signflip(high);positive=bool(evidence_complete and p is not None and p<=.05 and avg(high)>.5 and all(x>0 for x in high)and 0<avg(low)<avg(high)and recovered)
    return dict(schema='R11D_MEMORY_INTERVENTION1',config=config,meta_events=meta,ticks=ticks,epochs=cohorts,block_comparisons=comparisons,
        experiment_completed=completed,matched_blocks=len(matched),working_set_effect_assessment_complete=evidence_complete,
        primary_high_minus_control_ms=describe(high),secondary_low_minus_control_ms=describe(low),
        primary_two_sided_block_signflip_p=p,signflip_assumption='Paired residual sign symmetry; descriptive support, not an exact randomization proof or an original-DDR-root-cause claim.',positive_large_working_set_effect=positive,
        recovery=dict(recording_cycles=len(recovery),bit6_to_bit2_equal_mod10_mean_ms=recovery_mean,
            baseline_epoch_equal_mod10_mean_ms=avg(baseline_means),tolerance_ms=tolerance,returned_to_idle_envelope=recovered),
        memory_withdrawal_transitions=withdrawal,
        verdict='LARGE_WORKING_SET_SENSITIVITY_OBSERVED'if positive else'NO_QUALIFIED_POSITIVE_EFFECT_AT_TESTED_DOSE'if evidence_complete else'INSUFFICIENT_MATCHED_EXPERIMENT',
        DDR_bus_bytes_measured=False,DDR_contention_time_ms=None,baseline_DDR_root_cause_proven=False,
        image_buffer_release_proven=False,unique_pixel_writer_proven=False,
        assignment_policy='Native cycle durations stay in RTOS clock. Frame cohorts use complete Linux publication brackets for adjacent frames, with2s settling and300ms tail guards. No absolute clock subtraction or exact per-frame load-overlap claim.',
        interpretation='Positive result isolates a large-vs-small private working-set effect under CPU-matched occupancy and observed clock controls; caches, shared fabric, memory controllers and scheduling interactions remain candidates. It does not prove original HEVC traffic caused all baseline latency. A negative result is limited to tested dose, address placement and coverage. Logical load bytes are not DDR bandwidth. Exposed cache inventory/PFNs do not establish a physical DDR channel.',
        physical_camera_tested=False)

