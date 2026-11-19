"""Producer5 stages. EP DMA is an active-window candidate, not WDMA2 proof."""
import statistics,struct
MAGIC=0x35544c56
def decode_stage_flow(samples,started_ns):
    frames={};generation=0;last=None;invalid=0;live=[]
    for s in samples:
        if not s['valid']&65536:continue
        w=s['latency']
        if len(w)!=64 or w[:2]!=[MAGIC,5]:raise ValueError('Invalid producer5 frame header')
        seq=w[2]
        if last is not None and seq<last and not(last>0xf0000000 and seq<0x10000000):generation+=1
        last=seq
        live.append(dict(observed_at_s=(s['t']-started_ns)/1e9,completed_sequence=seq))
        for slot in range(7):
            raw=w[7+slot*8:15+slot*8];n,hw,dma,core,ep,y,ep_raw,flags=raw
            if not n:continue
            if ((seq-n)&0xffffffff)>=7 or not flags&1:invalid+=1;continue
            key=generation,n
            if key in frames:
                if frames[key]['raw_words']!=raw:raise ValueError('Conflicting committed producer5 frame')
                continue
            driver={0:'0x1717',1:'0x1818',2:'0x1919'}.get((flags>>8)&3)
            issues=[]
            if driver is None:issues.append('unknown-driver')
            if flags&32:issues.append('cancelled-before-callback')
            if flags&0x400:issues.append('repeated-core-done-observation')
            missing=[name for name,bit in [('hardware',1),('core_done',4),('callback',16)] if not flags&bit]
            row=dict(generation=generation,sequence=n,observed_at_s=(s['t']-started_ns)/1e9,
                recording_epoch=s.get('epoch',0),driver_mode=driver,IPC_preset=hex(s['rtos'][2]),mark=s.get('mark',0),
                source_state=s['source'][5]if s['valid']&16 else None,active_backend=hex(s['hevc'][9])if s['valid']&16384 else None,
                raw_words=raw,flags=hex(flags),output_Y_address=hex(y),EP_DMA_D0_D4_OR=hex(ep_raw),
                EP_association='active IPC window plus bank0 Y-address check; native EP job identity unproven',
                EP_Y_address_changed=bool(flags&128),missing_events=missing,problems=issues,
                event_counts=dict(hardware=1,final_IRQ=1 if flags&4 else 0,SRAM_load=None,CS0_start=None,
                                  IPC_DMA_bit20=min(255,(flags>>16)&255),EP_DMA=min(255,flags>>24)),
                valid_for_statistics=not missing and not issues)
            def dt(a,b):return((a-b)&0xffffffff)/26000
            if flags&2:row['commit_to_last_IPC_DMA_bit20_ms']=dt(dma,hw)
            if flags&4:row['hardware_to_IRQ_ms']=dt(core,hw)
            if flags&8:row['commit_to_last_EP_DMA_candidate_ms']=dt(ep,hw)
            if flags&12==12:row['core_to_EP_candidate_signed_ms']=(((ep-core+0x80000000)&0xffffffff)-0x80000000)/26000
            if any(v>2000 for k,v in row.items() if k.endswith('_ms') and isinstance(v,(int,float))):row['problems'].append('invalid-time-order');row['valid_for_statistics']=False
            frames[key]=row
    ordered=sorted(frames.values(),key=lambda r:(r['generation'],r['sequence']))
    for a,b in zip(ordered,ordered[1:]):
        if a['generation']==b['generation'] and b['sequence']==a['sequence']+1 and a['driver_mode']==b['driver_mode']:
            ms=((b['raw_words'][1]-a['raw_words'][1])&0xffffffff)/26000
            if 0<ms<=2000:a['cycle_ms']=ms
    gaps=sum(max(0,b['sequence']-a['sequence']-1)for a,b in zip(ordered,ordered[1:])if a['generation']==b['generation'])
    stats={}
    for k in ['hardware_to_IRQ_ms','cycle_ms','commit_to_last_IPC_DMA_bit20_ms','commit_to_last_EP_DMA_candidate_ms']:
        v=sorted(r[k]for r in ordered if r['valid_for_statistics'] and k in r)
        if v:stats[k]=dict(n=len(v),mean=statistics.mean(v),median=statistics.median(v),p95=v[min(len(v)-1,int(.95*len(v)))],max=max(v))
    return dict(schema='NXL_STAGE_FLOW5',clock_Hz=26000000,ring_capacity=7,retained_frames=len(ordered),
        valid_frames=sum(r['valid_for_statistics']for r in ordered),missed_frame_sequences=gaps,
        invalid_or_inflight_entries_discarded=invalid,statistics=stats,records=ordered,live_samples=live,
        interpretation='IRQ-entry observations, not pure hardware busy time. IPC DMA bit20 is raw. EP D0/D4 is not proven WDMA2 completion. No Linux/RTOS tick subtraction. Missing events remain missing.')
