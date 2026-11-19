"""R10y paired allocator snapshots and cross-request buffer observation.

Only interval observations in the Linux clock domain are reported. Native
headers identify a request, not an encoded image generation. Metadata free
states never silently become physical image-release events.
"""
import collections, hashlib, struct
from video_completion import fnv
from video_pipeline1 import active

CAPACITY=1000
BLOCKS=45056
TABLE_BYTES=BLOCKS*16
SLOT_BYTES=2048+2*TABLE_BYTES
RECORD=1024
MAGIC=0x3156424e

def _partition(raw):
    t=list(struct.iter_unpack('<4I',raw));i=0;free=[];used=[]
    if len(t)!=BLOCKS:raise ValueError('Allocator table length')
    while i<BLOCKS:
        k,n,a,b=t[i];k&=255
        if not k:
            if n:raise ValueError('Free allocator entry with nonzero span')
            if free and free[-1][0]+free[-1][1]==i:free[-1][1]+=1
            else:free.append([i,1])
            i+=1;continue
        if not 0<n<=BLOCKS-i:raise ValueError('Invalid busy allocator span')
        for j in range(1,n):
            kk,nn,_,_=t[i+j];kk&=255
            if not((kk==k and nn==n-j)if kk else nn==0):raise ValueError('Inconsistent allocator partition')
        used.append(dict(start=0x94000000+i*16384,end=0x94000000+(i+n)*16384,kind=hex(k),bytes=n*16384))
        i+=n
    return free,used

def _routes(words,mask,allocs,hevc=None):
    names=[]
    for bank in range(2):
        for port in range(4):names.append((f'IPC_DMA_bank{bank}_port{port}',6))
    for direction in ['RDMA','WDMA']:
        for bank in range(2):
            for port in range(4):names.append((f'EP_{direction}_bank{bank}_port{port}',5))
    if hevc is not None:
        words=words[:128]+hevc
        names.append(('HEVC_submitted_input',2));mask=(mask&((1<<128)-1))|((3 if hevc[0]is not None else 0)<<128)
    at=0;rows=[];unique={}
    for name,n in names:
        raw=words[at:at+n];valid=[bool(mask>>j&1)for j in range(at,at+n)];refs=[]
        for j in range(2):
            pa=raw[j]if valid[j]else None
            match=next((a for a in allocs if pa is not None and a['start']<=pa<a['end']),None)
            if match:unique[match['start']]=match
            refs.append(dict(raw_address=hex(pa)if pa is not None else None,
                             containing_allocation=match,
                             nonzero_address_missing_from_snapshot=bool(pa and 0x94000000<=pa<0xc0000000 and match is None)))
        rows.append(dict(route=name,raw_words=raw,read_success=valid,address_references=refs,
                         exact_active_buffer_length_known=False))
        at+=n
    return dict(routes=rows,unique_referenced_allocations=list(unique.values()),
                unique_referenced_allocation_bytes=sum(x['bytes']for x in unique.values()),
                interpretation='Containing allocations, counted once across all routes. They can hold multiple images or other objects; this is not exact active-image size or measured memory traffic.')

def _image(words):
    prefix=words[:26];buf=words[26:38];typ=buf[0]&255
    labels={0:['top'],2:['R','GR','GB','B'],3:['R','G','B'],5:['Y',None,'Cb','Cr'],6:['Y0','Y1','C0','C1'],7:['Y',None,'Cbr']}
    def addresses(start):
        return {label:hex(buf[start+j])for j,label in enumerate(labels.get(typ,[]))if label is not None}
    return dict(CImage_type=hex(prefix[0]),image_format_raw=hex(prefix[15]),Buffer_type=typ,
                Buffer_type_supported=typ in labels,address_array_1=addresses(1),address_array_2=addresses(5),
                Buffer_raw_words=buf,CImage_raw_words=prefix,physical_address_array_proven=False)

def decode_buffers(path,native=None):
    data=path.read_bytes();h=struct.unpack_from('<32I',data)if len(data)>=128 else ()
    schema=h[1]if len(h)==32 else 0
    shape=bool(len(h)==32 and ((schema==1 and h[7:10]==(4,36,500)and h[10]in [10000,20000])or
                              (schema in [2,3,4,5] and h[7:10]==(2,32,80)and h[10]in [5000,10000,20000])))
    if(len(h)!=32 or h[0]!=MAGIC or schema not in [1,2,3,4,5]or h[2:4]!=(1024,1000)or h[4]>1000 or h[12]>4 or
       not shape or h[11]!=2 or h[13]!=SLOT_BYTES or
       h[6]&~3 or not h[23] or h[24]<=1 or h[27]<=1 or h[14]!=6799488 or h[31]!=fnv(data[:124])or len(data)!=128+h[4]*RECORD+h[12]*SLOT_BYTES):
        raise ValueError('Buffer observation header/size/hash')
    rows=[];images=[];ends=[];pending=None;seen_pending=set();post_counts=collections.Counter()
    for i in range(h[4]):
        raw=data[128+i*RECORD:128+(i+1)*RECORD]
        begin,end,seq,window,kind,flags=struct.unpack_from('<QQ4I',raw);d=list(struct.unpack_from('<247I',raw,32))
        kinds=[1,2,3]if schema==1 else [1,2,3,4,5,6,7]+([8]if schema>=3 else [])+([9]if schema>=5 else [])
        window_ok=0<window<=h[5]or schema in [2,3,4,5] and kind in [6,7,8]and window==0
        if seq!=i or begin>end or not window_ok or kind not in kinds or struct.unpack_from('<I',raw,1020)[0]!=(fnv(raw[:1020])^0x9c28ad51)or rows and begin<rows[-1]['begin_ns']:
            raise ValueError('Buffer vector integrity/order')
        row=dict(sequence=i,window=window,kind=kind,flags=hex(flags),begin_ns=begin,end_ns=end,raw_words=d)
        if kind==1:
            if flags&~65535:raise ValueError('Unknown buffer flags')
            a,b=d[:16],d[16:32]
            same=flags&3==3 and a[:2]==[0x42544c56,11]and a==b
            if bool(flags&4)!=same:raise ValueError('False same-request buffer flag')
            request_key=(window,a[2],a[5],a[9],a[10])
            post_matched=None
            if flags&32768:
                if pending is None:raise ValueError('Pending followup without baseline')
                post_matched=request_key==pending
                if not post_matched:
                    if flags&~32769:raise ValueError('New-request followup read beyond initial header')
                    pending=None
                else:
                    post_counts[pending]+=1
                    if post_counts[pending]>2:raise ValueError('More than two same-request pending followups')
            else:
                pending_hit=same and active(a)and d[244]!=0xffffffff and d[245]!=0xffffffff and d[244]&4 and d[245]&4 and d[246]==0x801cdf6c
                pending=None
                if pending_hit and request_key not in seen_pending:seen_pending.add(request_key);pending=request_key
            ida,idb=d[58:66],d[66:74]
            identity=(flags&96==96 and ida[:5]==idb[:5]and ida[6:]==idb[6:]and
                      ida[1]==d[224]+0x4c608 and ida[3]==d[225]+0x59c28)
            dma=flags&24==24 and d[32:37]==d[37:42]and d[82]==d[83]and d[82]>>8&15==7 and d[34]>>12&0x7ffff==3840 and d[32]!=0 and d[33]-d[32]==3840*2160
            qualified=(same and active(a)and(a[3]>>28)&15==1 and not a[3]&0x20000 and a[14]==0x801cdf4c and d[84:86]==d[86:88]and identity and ida[4]==2)
            hal=(flags&384==384 and identity and ida[7]==d[224]+0x4c9c8 and
                 d[74]==d[78]==d[224]+0x5b148 and d[42:50]==d[50:58]and d[75]==d[79])
            pa,pb=d[88:105],d[105:122];n=d[232];sw=d[230];so=d[231]
            is_que=pa[0]==d[225]+0x593b0
            kind='Que'if is_que else 'Buffer'if pa[0]==d[225]+0x592f8 else 'Yuv'if pa[0]==d[225]+0x59288 else 'unknown'
            cap_at=7 if is_que else 14 if sw==30 else 15
            base_at=8 if is_que else 7
            pool=bool(flags&7680==7680 and 0<n<=64 and
                      ((sw,so,pa[0])==(30,28,d[225]+0x592f8)or(sw,so,pa[0])==(28,26,d[225]+0x59288)or(sw,so,pa[0])==(28,26,d[225]+0x593b0))and
                      n==pa[cap_at]and d[236]==pa[base_at]and all(pa[j]==pb[j]for j in [0,7,8,9,10,cap_at]))
            states=[]
            if pool:
                for at in [122,126]:states.append([(d[at+j//16]>>((j%16)*2))&3 for j in range(n)])
            slot_a,slot_b=d[130:160],d[160:190];ya,yb=d[190:206],d[206:222];idx=d[222]
            size_at=3 if is_que else 5
            unchanged=[j for j in range(sw)if j!=so]
            live_before=bool(pool and kind in ['Buffer','Que']and flags&8192 and idx<n and slot_a[0]==idx and
                             slot_a[size_at]==64 and (not is_que or slot_a[26]in [1,2,3]and slot_a[1]==slot_a[2]!=0))
            payload=bool(live_before and flags&16384 and slot_b[0]==idx and
                         all(slot_a[j]==slot_b[j]for j in unchanged)and ya==yb)
            fingerprint=hashlib.sha256(struct.pack('<%dI'%(len(unchanged)+16),*([slot_a[j]for j in unchanged]+ya))).hexdigest()if live_before else None
            native_key=(a[2]+1)&0xffffffff or 1
            row.update(same_request=bool(same),qualified_request=bool(qualified),pending_sequence=native_key if same else None,HW_tick=a[5]if same else None,
                       source_identity_stable=bool(identity),source_state=ida[4]if identity else None,WDMA2_configuration_stable=bool(dma),
                       WDMA2_Y=hex(d[32])if dma else None,WDMA2_UV=hex(d[33])if dma else None,
                       HEVC_descriptor_stable=bool(hal),HEVC_Y=hex(d[44])if hal else None,HEVC_UV=hex(d[45])if hal else None,
                       HEVC_result_path_before=d[75]if flags&128 else None,HEVC_result_path_after=d[79]if flags&256 else None,
                       pool_structural_identity_stable=pool,pool_object=hex(d[229]),pool_kind=kind,slot_array=hex(d[236]),pool_capacity=n if pool else None,
                       pool_raw_types=[hex(pa[0]),hex(pb[0])],pool_read_failure_stages=[d[233],d[234]],
                       metadata_state_counts=[dict(collections.Counter(x))for x in states],metadata_states=states,
                       watched_slot=idx if idx!=0xffffffff else None,watched_payload_stable=payload,watched_payload_before_valid=live_before,
                       slot_state_before=slot_a[so]if pool and idx<n else None,slot_state_after=slot_b[so]if pool and idx<n else None,
                       slot_payload_Y=hex(ya[11])if live_before else None,slot_payload_UV=hex(ya[13])if live_before else None,
                       metadata_lifetime_fingerprint=fingerprint,
                       HEVC_input_address_available=bool(hal and 0x94000000<=d[44]<d[45]<0xc0000000),
                       same_slot_HEVC_address_candidate=bool(payload and hal and 0x94000000<=ya[11]<ya[13]<0xc0000000 and ya[13]-ya[11]==3840*2160 and ya[11]==d[44]and ya[13]==d[45]),
                       pending_followup_request_matched=post_matched,native_endpoints_matched=False,physical_image_return_proven=False,hardware_busy_time_proven=False)
        elif kind==2:
            if flags&~63:raise ValueError('Unknown CImage flags')
            row['images']=[]
            for j in range(2):
                first=34+j*76
                ok=flags&(3<<(2+2*j))==(3<<(2+2*j))and d[first:first+38]==d[first+38:first+76]and d[first]==0x80588910
                entry=dict(direction='input'if j==0 else'output',pointer=hex(d[32+j]),typed_pair_stable=bool(ok))
                if ok:entry.update(_image(d[first:first+38]))
                row['images'].append(entry)
            row['same_native_header']=bool(flags&3==3 and d[:16]==d[16:32]and d[:2]==[0x42544c56,11]);images.append(row)
        elif kind==3:
            if flags or d[0]not in [1,2,3,4]or d[1]>(36 if schema==1 else 32):raise ValueError('Invalid buffer window termination')
            row.update(reason={1:'vector_limit',2:'CPU_limit',3:'wall_limit',4:'recording_lifetime_changed'}[d[0]],vectors=d[1],CPU_ns=d[2]|d[3]<<32,wall_ns=d[4]);ends.append(row)
        else:
            from video_stage_windows import decode_stage
            row.update(decode_stage(kind,flags,d,schema))
        rows.append(row)
    snapshots=[];base=128+h[4]*RECORD
    for i in range(h[12]):
        raw=data[base+i*SLOT_BYTES:base+(i+1)*SLOT_BYTES];w=struct.unpack_from('<512I',raw)
        if(w[0]!=0x314d424e or w[1]not in [1,2,3,4]or w[2]not in range(1,7)or w[10]!=BLOCKS or w[13]!=0 or w[16]>TABLE_BYTES or w[17]>TABLE_BYTES or w[511]!=fnv(raw[:2044])):raise ValueError('Memory snapshot integrity/header')
        aa,bb=raw[2048:2048+TABLE_BYTES],raw[2048+TABLE_BYTES:]
        if fnv(aa[:w[16]])!=w[11]or fnv(bb[:w[17]])!=w[12]or fnv(aa)!=w[20]or fnv(bb)!=w[21]:raise ValueError('Memory table hash')
        complete=w[2]==2 and w[16]==w[17]==TABLE_BYTES
        equal=complete and aa==bb;heads=[w[32+j*12:44+j*12]for j in range(3)]
        head_ok=all(v[:7]==(0x80574230,0x805742ac,0x81dfc800,0x80574b60,0x80574c48,0x94000000,BLOCKS)and 0x94000000<=v[8]<v[9]<=0xc0000000 for v in heads)
        stable=equal and head_ok and heads[0]==heads[1]==heads[2]
        x=dict(index=i,phase={1:'fullwidth_preview_before',2:'recording_early',3:'recording_late',4:'preview_after'}[w[1]],
               status=w[2],complete=complete,tables_equal=equal,heads_stable=bool(head_ok and heads[0]==heads[1]==heads[2]),
               begin_ns=w[6]|w[9]<<32,end_ns=w[7]|w[8]<<32,read_bytes=[w[16],w[17]],
               final_read_stage=w[14],final_read_cursor=w[15],
               Linux_visible_snapshot_stable=bool(stable),RTOS_cache_publication_proven=False,live_allocatable_capacity_proven=False,
               allocator_heads=[list(v)for v in heads],raw_route_words=[list(w[80:216]),list(w[216:352])])
        if stable:
            try:free,used=_partition(aa)
            except ValueError as e:x['partition_error']=str(e)
            else:
                lo=(heads[2][8]-0x94000000)//16384;hi=(heads[2][9]-0x94000000)//16384
                largest=max([max(0,min(a+n,hi)-max(a,lo))for a,n in free]or[0])
                x.update(partition_valid=True,total_free_bytes=sum(n for a,n in free)*16384,bank2_largest_free_bytes=largest*16384,
                         allocation_extents=used,free_spans=[dict(start=hex(0x94000000+a*16384),bytes=n*16384)for a,n in free])
                routes=[];sidecars=[]
                for j in range(2):
                    at=384+j*48;side=list(w[at:at+48]);ok=side[20]!=0xffffffff and side[20]&2 and side[7]==h[30]+0x4c9c8 and side[8]==h[30]+0x5b148
                    recording=ok and side[4]==2 and w[5]==2
                    sidecars.append(dict(raw_words=side,HEVC_typed_descriptor_available=bool(ok),recording_source=bool(recording),native_header_read=bool(side[20]!=0xffffffff and side[20]&4)))
                    mask=sum(w[352+j*5+k]<<(32*k)for k in range(5));routes.append(_routes(list(w[80+j*136:216+j*136]),mask,used,[side[14],side[15]]if recording else [None,None]))
                x['route_occupancy']=routes
                x['Linux_and_native_bookends']=sidecars
        snapshots.append(x)
    result=dict(schema='NXL_BUFFER_MEMORY'+str(schema),record_count=h[4],capacity=1000,flags=hex(h[6]),window_count=h[5],records=rows,images=images,
                capture_identity=dict(cookie=h[23],pid=h[24],started_ns=h[25]|h[26]<<32,camera_pid=h[27],camera_ticks=h[28]|h[29]<<32,modules_base=h[30]),
                unclosed_windows=sorted(set(range(1,h[5]+1))-{e['window']for e in ends}),
                window_terminations=ends,memory_snapshots=snapshots,observer_CPU_seconds=(h[15]|h[16]<<32)/1e9,
                memory_CPU_seconds=(h[17]|h[18]<<32)/1e9,maximum_vector_ms=h[19]/1e6,
                extra_Linux_RAM_bytes=h[14],extra_RTOS_allocation=0,requested_vector_interval_ms=h[7],
                metadata_free_transitions=[],WDMA2_HEVC_cross_request_candidates=[],physical_image_return_proven=False)
    associate_buffers(result,native)
    if schema in [2,3,4,5]:
        from video_stage_windows import finish_stages
        finish_stages(result,native)
    return result

def associate_buffers(result,native):
    records=[r for r in result['records']if r['kind']==1];native_keys=collections.Counter()
    if native:
        for r in native.get('records',[]):
            if r.get('valid_for_statistics')and r.get('native_input_binding_candidate'):
                native_keys[(r['sequence'],r['raw_words'][3],r['input_descriptor'],r['input_physical_address'],r['output_descriptor'])]+=1
    produced=collections.defaultdict(list);previous={};seen_lifetimes={}
    for r in records:
        d=r['raw_words'];key=(r['pending_sequence'],r['HW_tick'],hex(d[9]),hex(d[10]),hex(d[11]))
        r['native_endpoints_matched']=bool(r['qualified_request']and native_keys[key]==1)
        if r['native_endpoints_matched']and r['WDMA2_configuration_stable']:
            address=(r['window'],r['WDMA2_Y'],r['WDMA2_UV']);birth=(r['pending_sequence'],r['HW_tick'])
            if not produced[address]or produced[address][-1]['birth']!=birth:produced[address].append(dict(birth=birth,record=r))
        if r['native_endpoints_matched']and r['HEVC_descriptor_stable']:
            candidates=[x for x in produced[(r['window'],r['HEVC_Y'],r['HEVC_UV'])]if x['birth'][0]!=r['pending_sequence']]
            if len(candidates)==1:
                p=candidates[0]['record'];marker=(p['sequence'],r['HEVC_Y'],r['HEVC_UV'])
                if marker not in seen_lifetimes:
                    result['WDMA2_HEVC_cross_request_candidates'].append(dict(output_request=p['pending_sequence'],encoder_observed_request=r['pending_sequence'],
                       output_vector=p['sequence'],encoder_vector=r['sequence'],Y=r['HEVC_Y'],UV=r['HEVC_UV'],
                       Linux_observation_span_ms=(r['end_ns']-p['begin_ns'])/1e6,exact_image_generation_proven=False,
                       interpretation='Same programmed surface in a later request; unobserved generations and hardware completion remain unproved.'))
                    seen_lifetimes[marker]=1
        if not(r['pool_structural_identity_stable']and r['source_identity_stable']and r['source_state']==2):previous.clear();continue
        if r['watched_slot']is None:continue
        identity=(r['window'],r['pool_object'],r['slot_array'],r['watched_slot'])
        p=previous.get(identity)
        if r['watched_payload_before_valid']and r['slot_state_before']==3:previous[identity]=r;p=r
        if r['watched_payload_stable']and r['slot_state_after']==3:previous[identity]=r;p=r
        returned=False
        if p and r['pool_kind']=='Que':
            old=p['raw_words'][130:160]
            # OEM Que free deletes owned metadata+4 and clears+4/+8/+c/+60,
            # then writes state0. Preserve the remaining FrameData fields as
            # lifetime evidence; never dereference the cleared/freed pointer.
            for free in [d[130:160],d[160:190]]:
                if free[0]==r['watched_slot']and all(free[j]==0 for j in [1,2,3,24,26])and all(free[j]==old[j]for j in range(28)if j not in [1,2,3,24,26]):returned=True
        elif p and r['slot_state_after']==0:
            returned=r['watched_payload_stable']and r['metadata_lifetime_fingerprint']==p['metadata_lifetime_fingerprint']
        if p and returned:
            result['metadata_free_transitions'].append(dict(pool=r['pool_object'],slot=r['watched_slot'],lifetime_fingerprint=p['metadata_lifetime_fingerprint'],
                pool_kind=r['pool_kind'],processing_vector=p['sequence'],free_vector=r['sequence'],Y=p['slot_payload_Y'],UV=p['slot_payload_UV'],
                Linux_transition_bounds_ns=[p['begin_ns'],r['end_ns']],metadata_state_3_to_0_observed=True,
                HEVC_address_matched_while_processing=p['same_slot_HEVC_address_candidate'],
                unobserved_ABA_not_excluded=True,physical_image_return_proven=False))
            previous.pop(identity,None)
    result['qualified_vectors']=sum(r['qualified_request']for r in records)
    result['endpoint_matched_vectors']=sum(r['native_endpoints_matched']for r in records)
    result['interpretation']='Guarded native requests, typed CImage/Buffer arrays, MMF slot states and allocator/route snapshots. Metadata reuse is not physical image release. Same addresses are cross-request candidates, not unique image identities. Table equality does not establish RTOS cache publication. No bottleneck or frame-rate-fix verdict is inferred automatically.'
    return result
