"""R10j native-event comparison. No camera access; Python standard library.

Times are IRQ-entry observations, not hardware-latched assertion timestamps.
Repeated-event counters saturate at3; only first observed timestamps are kept.
"""
import collections
import statistics
import struct

def float_word(v):
    if v == 0xffffffff:return None
    x=struct.unpack('<f',struct.pack('<I',v))[0]
    return x if .1 <= x <= 1000 else None

def context(s,started_ns):
    w=s.get('flow',[])
    row=dict(sample=s['sequence'],observed_at_s=(s['t']-started_ns)/1e9,
             epoch=s['epoch'],mark=s['mark'],driver_mode=hex(s['rtos'][0]),
             IPC_preset=hex(s['rtos'][2]),source_state=s['source'][5] if s['valid']&16 else None,
             active_backend=hex(s['hevc'][9]) if s['valid']&16384 else None,
             camera_input_fps=float_word(s['camera'][2]) if s['valid']&8 else None,
             camera_output_fps=float_word(s['camera'][3]) if s['valid']&8 else None)
    if not s['valid']&524288:return row
    if len(w)!=32 or w[0]!=1:raise ValueError('Invalid flow context')
    row['raw_words']=w
    if w[1]&1:row.update(encoder_width=w[2],encoder_height=w[3],encoder_fps_rounded=w[4]&65535,
                         intra_period=w[5],codec_mode=w[6],bitrate_kbps=w[7],encode_command_raw=w[8])
    if w[1]&2:row['native_handlers']={name:hex(w[i])for name,i in [('SRAM_load',10),('CS0_start',11),('CS0_done',12),('ID47',13)]}
    if w[1]&4:row['IPC_global_pointer']=hex(w[15]);row['IPC_global_words']=w[16:30]
    return row

def decode_frame_flow(samples,started_ns):
    frames={};live=[];contexts=[];last_seq=None;generation=0;discarded=0
    delta=lambda a,b:((a-b+0x80000000)&0xffffffff)-0x80000000
    for s in samples:
        ctx=context(s,started_ns)
        if s['valid']&524288:contexts.append(ctx)
        if not s['valid']&65536:continue
        w=s['latency']
        if len(w)!=64 or w[:2]!=[0x33544c56,4]:raise ValueError('Invalid frame-flow timing header')
        seq=w[2]
        if last_seq is not None and seq<last_seq:generation+=1
        last_seq=seq
        live.append(dict(observed_at_s=ctx['observed_at_s'],completed_sequence=seq,
                         flags=hex(w[3]),CS0_start=w[4],hardware_start=w[5],final_IRQ=w[6],SRAM_load=w[7]))
        for slot in range(8):
            raw=w[8+slot*7:15+slot*7];n,hw,sram,start,irq,cb,flags=raw
            if not n:continue
            if n>seq or n+7<seq or not flags&1:discarded+=1;continue
            key=(generation,n)
            if key in frames:
                if frames[key]['raw_words']!=raw:raise ValueError('Conflicting committed flow entry')
                continue
            counts=dict(hardware=(flags>>16)&255,final_IRQ=(flags>>8)&255,
                        SRAM_load=(flags>>28)&3,CS0_start=(flags>>26)&3)
            issues=[]
            if flags&16:issues.append('cancelled-before-callback')
            missing=[name for name,bit in [('hardware',2),('final_IRQ',4),('callback',8)]if not flags&bit]
            dt=delta(irq,hw)/26000;post=delta(cb,irq)/26000
            if not missing and (not 0<dt<=2000 or not 0<=post<=2000):issues.append('invalid-time-order')
            if counts['hardware']!=1 or counts['final_IRQ']!=1:issues.append('repeated-or-missing-hardware/final-event')
            row=dict(generation=generation,sequence=n,observed_at_s=ctx['observed_at_s'],recording_epoch=s['epoch'],
                     driver_mode=ctx['driver_mode'],IPC_preset=ctx['IPC_preset'],mark=s['mark'],
                     source_state=ctx['source_state'],active_backend=ctx['active_backend'],
                     raw_words=raw,flags=hex(flags),controller_type=(flags>>24)&3,event_counts=counts,
                     missing_events=missing,problems=issues,coalesced_start_and_final=bool(flags&128),
                     valid_for_statistics=not missing and not issues)
            if flags&6==6:row['hardware_to_IRQ_ms']=dt
            if flags&12==12:row['IRQ_to_callback_ms']=post
            if flags&2 and counts['SRAM_load']:row['hardware_to_SRAM_IRQ_ms']=delta(sram,hw)/26000
            if flags&2 and counts['CS0_start']:row['hardware_to_start_IRQ_ms']=delta(start,hw)/26000
            if counts['SRAM_load'] and counts['CS0_start']:row['SRAM_to_start_IRQ_ms']=delta(start,sram)/26000
            if counts['CS0_start'] and flags&4:row['start_to_final_IRQ_ms']=delta(irq,start)/26000
            if counts['SRAM_load'] and flags&4:row['SRAM_to_final_IRQ_ms']=delta(irq,sram)/26000
            frames[key]=row
    ordered=sorted(frames.values(),key=lambda r:(r['generation'],r['sequence']))
    for a,b in zip(ordered,ordered[1:]):
        if a['generation']==b['generation'] and b['sequence']==a['sequence']+1 and a['recording_epoch']==b['recording_epoch'] and a['driver_mode']==b['driver_mode'] and a['valid_for_statistics'] and b['valid_for_statistics']:
            cycle=delta(b['raw_words'][1],a['raw_words'][1])/26000
            if 0<cycle<=2000:
                a['cycle_ms']=cycle
                a['callback_to_next_hardware_ms']=delta(b['raw_words'][1],a['raw_words'][5])/26000
    stats={}
    for k in ['hardware_to_IRQ_ms','hardware_to_SRAM_IRQ_ms','hardware_to_start_IRQ_ms','SRAM_to_start_IRQ_ms','start_to_final_IRQ_ms','SRAM_to_final_IRQ_ms','IRQ_to_callback_ms','cycle_ms','callback_to_next_hardware_ms']:
        v=sorted(r[k]for r in ordered if r['valid_for_statistics'] and k in r)
        if v:stats[k]=dict(n=len(v),min=min(v),median=statistics.median(v),mean=statistics.mean(v),p95=v[min(len(v)-1,int(.95*len(v)))],max=max(v))
    gaps=sum(max(0,b['sequence']-a['sequence']-1)for a,b in zip(ordered,ordered[1:])if a['generation']==b['generation'])
    return dict(schema='NXL_FRAME_FLOW4',clock_Hz=26000000,retained_frames=len(ordered),valid_frames=sum(r['valid_for_statistics']for r in ordered),
                missed_frame_sequences=gaps,invalid_or_inflight_entries_discarded=discarded,statistics=stats,records=ordered,live_samples=live,
                flow_contexts=contexts,interpretation='Same tracing for driver1717/1818/1919 and OEM presetsE004/E005. HW is after original STR+DSB. SRAM-load/start/final are first IRQ44 observations before unchanged native ACK. Event counts3 mean3-or-more; coalesced bits cannot establish internal event order. Missing start/load events are missing evidence, never zero duration. Eight-slot overwrite gaps are recorder gaps. Exclude at least1second at mode/record transitions. No DDR bandwidth or causal bottleneck claim.')

def compare_flows(frame_timing):
    groups=collections.defaultdict(list)
    for r in frame_timing['records']:groups[(r['recording_epoch'],r['driver_mode'],r['IPC_preset'],r['source_state'])].append(r)
    out=[]
    for key,rows in sorted(groups.items(),key=lambda x:repr(x[0])):
        lo=min(r['observed_at_s']for r in rows)+1;hi=max(r['observed_at_s']for r in rows)-1
        valid=[r for r in rows if lo<=r['observed_at_s']<=hi and r['valid_for_statistics']]
        if not valid:continue
        metrics={}
        for name in ['hardware_to_IRQ_ms','hardware_to_SRAM_IRQ_ms','hardware_to_start_IRQ_ms','SRAM_to_start_IRQ_ms','start_to_final_IRQ_ms','SRAM_to_final_IRQ_ms','IRQ_to_callback_ms','cycle_ms']:
            v=sorted(r[name]for r in valid if name in r)
            if v:metrics[name]=dict(n=len(v),mean=statistics.mean(v),median=statistics.median(v),p95=v[min(len(v)-1,int(.95*len(v)))],max=max(v))
        out.append(dict(epoch=key[0],driver_mode=key[1],IPC_preset=key[2],source_state=key[3],steady_window_s=[lo,hi],frames=len(valid),statistics=metrics,
                        event_count_histograms={name:dict(collections.Counter(str(r['event_counts'][name])for r in valid))for name in ['hardware','SRAM_load','CS0_start','final_IRQ']}))
    return dict(groups=out,transition_trim_s=1,scope='Mode/epoch/source-state grouping, trimmed by1second at both ends. Compare actual frame dimensions, target FPS, geometry and MP4 before attributing a difference to a limiter. Source-state alone does not prove successful writing/encoding.')
