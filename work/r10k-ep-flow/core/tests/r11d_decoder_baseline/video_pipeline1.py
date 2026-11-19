"""Read R10w native stage flags; retain missing, pre-existing and unbound states.

Linux read windows are never subtracted from RTOS ticks. An EOF pulse can be
handled between reads. Zero, masked and already-set flags are not busy times.
"""
from pathlib import Path
import collections,json,struct
from video_completion import fnv

MAGIC=0x31504c56
DEFINITIONS=json.loads(Path(__file__).with_name('video_pipeline_stages.json').read_text('utf8'))
RAW_ADDRESSES=[0x2081040c,0x20810408,0x20810418,0x20810458,0x2081045c,0x20810468,
               0x208210c8,0x208210cc,0x817d8aac,0x817d8ab0,0x20821104,0x2082110c,
               0x20821204,0x2082120c,0x20810908,0x813f09bc]


def active(a):
    return (a[:2]==[0x42544c56,11] and a[3]&15==3 and (a[3]>>24)&3==0
            and not a[3]&0x10000 and bool(a[3]&512))


def decode_pipeline(data):
    if len(data)<128:
        raise ValueError('Truncated pipeline header')
    h=struct.unpack_from('<32I',data)
    if (h[:4]!=(MAGIC,1,256,1200) or h[4]>1200 or len(data)!=128+256*h[4]
            or h[6]&~15 or h[7:13]!=(100,1,50,3000,2,512)
            or any(h[14:16]) or any(h[21:31]) or h[31]!=fnv(data[:124])):
        raise ValueError('Pipeline header integrity/limits failed')
    rows=[]
    for i in range(h[4]):
        raw=data[128+256*i:128+256*(i+1)]
        begin,end,seq,burst,kind,flags=struct.unpack_from('<QQ4I',raw)
        w=list(struct.unpack_from('<55I',raw,32))
        if seq!=i or begin>end or kind not in [1,2,3] or not 1<=burst<=h[5] or struct.unpack_from('<I',raw,252)[0]!=(fnv(raw[:252])^0x59a130c7):
            raise ValueError('Pipeline record integrity/order failed')
        if rows and begin<rows[-1]['begin_ns']:
            raise ValueError('Pipeline monotonic order failed')
        row=dict(sequence=i,burst=burst,kind=kind,flags=hex(flags),begin_ns=begin,end_ns=end,raw_words=w)
        if kind==1:
            a,b=w[:16],w[16:32]
            if flags&~511 or w[48]&~0x7ffff or any(x!=0xffffffff for x in w[52:]):
                raise ValueError('Unknown pipeline sample bits')
            for bit,words in [(1,a),(2,b)]:
                if not flags&bit and words!=[0xffffffff]*16:
                    raise ValueError('Partial failed RTOS header was retained')
            # Failed scalar reads may leave a partial raw word. Retain it with
            # the failed mask; it can never contribute to a status/edge claim.
            same=flags&3==3 and a[:2]==[0x42544c56,11] and a==b
            derived=(4 if same else 0)|(8 if same and active(a)else 0)
            derived|=16 if same and (a[3]>>28)&15==1 and not a[3]&0x20000 and a[14]==0x801cdf4c else 0
            derived|=32 if w[48]>>2&1 and w[34]&4 else 0
            derived|=64 if w[48]>>5&1 and w[37]&4 else 0
            derived|=128 if w[48]>>15&1 and w[47]==0x801cdf6c else 0
            if flags&252!=derived:
                raise ValueError('False same-request/pending flags')
            pending_seq=((a[2]+1)&0xffffffff)or 1
            row.update(same_request=same,qualified_request=flags&28==28 and bool(w[48]&(1<<14))and w[46]==a[10],
                       pending_sequence=pending_seq if same else None,
                       HW_tick=a[5]if same else None,
                       input_descriptor=hex(a[9])if same else None,input_PA=hex(a[10])if same else None,
                       output_descriptor=hex(a[11])if same else None,
                       read_mask=hex(w[48]),raw_registers={hex(a):hex(w[32+n])for n,a in enumerate(RAW_ADDRESSES)if w[48]>>n&1},
                       native_endpoints_matched=False)
        else:
            if flags&~7 or flags&3 not in [1,2] or w[54]!=kind-2 or w[53]&~15:
                raise ValueError('Invalid stage routing record')
            mask=w[52]|(w[53]<<32)
            if not flags&4 and w[:16]!=[0xffffffff]*16:
                raise ValueError('Failed route header not reset')
            # Route scalar failures also retain partial raw values and masks.
            row.update(bank=kind-2,final=bool(flags&2),callbacks=[hex(x)for x in w[16:34]],owners=[hex(x)for x in w[34:52]],read_mask=hex(mask))
        rows.append(row)
    if sum(r['kind']==1 for r in rows)!=h[13]:
        raise ValueError('Pipeline sample count mismatch')
    bursts=[]
    for burst in sorted({r['burst']for r in rows}):
        rr=[r for r in rows if r['burst']==burst]
        samples=[r for r in rr if r['kind']==1]
        if len(samples)>50 or sum(bool(int(r['flags'],16)&256)for r in samples)>2:
            raise ValueError('Unbounded pipeline followups')
        pending=None;lower=0;post=0
        for r in samples:
            flags=int(r['flags'],16)
            if flags&256:
                if pending is None:
                    raise ValueError('Followup without qualified done pending')
                post+=1
                if r['qualified_request']and r['raw_words'][:16]==pending['raw_words'][:16]and flags&224==224:
                    lower=max(lower,(r['begin_ns']-pending['end_ns'])/1e6)
            elif r['qualified_request']and flags&224==224:
                pending=r
        stages=[]
        for definition in DEFINITIONS['IPC_stages']:
            si,ei,bit=definition['status_index'],definition['enable_index'],definition['bit']
            def observe(r):
                w=r['raw_words'];m=w[48]
                cb_index=definition['callback_index'];cb_bit=15 if cb_index==47 else cb_index-33
                if not r['qualified_request']or m&((1<<si)|(1<<ei)|(1<<cb_bit))!=((1<<si)|(1<<ei)|(1<<cb_bit)):
                    return None
                return bool(w[32+si]>>bit&1),bool(w[32+ei]>>bit&1),hex(w[cb_index])==definition['expected_callback'],None
            stage=_stage(definition['name'],samples,observe,owner_identity=False)
            stage['native_mapping_proven']=definition.get('native_mapping_proven',False)
            stages.append(stage)
        for bank in [0,1]:
            route=[r for r in rr if r['kind']==bank+2]
            before=next((r for r in route if not r['final']),None)
            after=next((r for r in route if r['final']),None)
            for definition in DEFINITIONS['EP_stages']:
                event=definition['event'];expected=definition['banks'][bank]['callback']
                route_ok=False;owner=None
                if before and after:
                    a,b=before['raw_words'],after['raw_words']
                    mask=(1<<event)|(1<<(18+event))
                    have=lambda w:(w[52]|w[53]<<32)&mask==mask
                    owner=a[34+event]
                    route_ok=(have(a)and have(b)and a[16+event]==b[16+event]==int(expected,16)
                              and owner==b[34+event]and 0x81006000<=owner<0x94000000 and owner&3==0)
                def observe(r,event=event,bank=bank,route_ok=route_ok,owner=owner):
                    w=r['raw_words'];si=6+bank;ei=8+bank
                    if not r['qualified_request']or w[48]&((1<<si)|(1<<ei))!=((1<<si)|(1<<ei)):
                        return None
                    return bool(w[32+si]>>event&1),bool(w[32+ei]>>event&1),route_ok,hex(owner)if owner is not None else None
                stage=_stage(f"EP_CS{bank}_"+definition['name'],samples,observe,owner_identity=False)
                stage['native_mapping_proven']=True
                stages.append(stage)
        bursts.append(dict(burst=burst,samples=len(samples),qualified_samples=sum(r['qualified_request']for r in samples),
                           done_pending_followups=post,done_pending_lower_bound_ms=max(0,lower),stages=stages))
    return dict(schema='NXL_PIPELINE1',record_count=len(rows),burst_count=h[5],sample_count=h[13],
                flags=hex(h[6]),fast_period_ms=100,requested_burst_interval_ms=1,maximum_samples_per_burst=50,
                burst_cadence_ms=3000,maximum_post_pending_reads=2,observer_CPU_s=(h[18]|h[19]<<32)/1e9,
                maximum_burst_CPU_ms=h[20]/1e6,records=rows,bursts=bursts,
                stage_definitions=DEFINITIONS,
                limitations=DEFINITIONS['limitations']+[
                    'Same-request full16 headers bracket each hardware vector; raw vectors are not simultaneous snapshots.',
                    'Only observed0-to1 transitions within one unchanged request form EOF windows. Already-set/masked/missing flags never become durations.',
                    'Linux scheduling controls actual sample spacing; requested1ms is not a hardware timing resolution guarantee.',
                    'EP notification owner agreement is not current-image buffer identity; stages/branches may overlap.',
                    'No zero-status, missing edge or stage configuration is automatically interpreted as a bottleneck.'])


def _stage(name,samples,observe,owner_identity):
    values=[];transitions=[];previous=None
    for r in samples:
        value=observe(r)
        if value is None:
            previous=None;continue
        state,enabled,route,owner=value
        values.append(dict(sample=r['sequence'],set=state,enabled=enabled,route_verified=route,owner=owner))
        key=r['raw_words'][:16]
        if (previous is not None and key==previous[0]and not previous[2][0]and state
                and enabled and route and previous[2][1]and previous[2][2]):
            transitions.append(dict(pending_sequence=r['pending_sequence'],HW_tick=r['HW_tick'],
                                    earliest_linux_ns=previous[1]['begin_ns'],latest_linux_ns=r['end_ns'],
                                    width_ms=(r['end_ns']-previous[1]['begin_ns'])/1e6,
                                    native_endpoints_matched=False,hardware_latched_image_identity_proven=False))
        previous=(key,r,value)
    return dict(name=name,observations=values,observed_rising_edges=transitions,
                initially_set=bool(values and values[0]['set']),enabled_observations=sum(v['enabled']for v in values),
                route_verified_observations=sum(v['route_verified']for v in values),
                hardware_latched_image_identity_proven=owner_identity)


def associate_pipeline(pipeline,flow):
    index=collections.defaultdict(list)
    for r in flow['records']:
        if not r.get('valid_for_statistics')or not r.get('native_input_binding_candidate'):
            continue
        key=(r['sequence'],r['raw_words'][3],r['input_descriptor'],r['input_physical_address'],r['output_descriptor'])
        index[key].append(r)
    for r in pipeline['records']:
        if r['kind']!=1 or not r['qualified_request']:
            continue
        key=(r['pending_sequence'],r['HW_tick'],r['input_descriptor'],r['input_PA'],r['output_descriptor'])
        r['native_endpoints_matched']=len(index.get(key,[]))==1
    summary=collections.Counter()
    for burst in pipeline['bursts']:
        for stage in burst['stages']:
            for edge in stage['observed_rising_edges']:
                matched=[r for r in pipeline['records']if r['kind']==1 and r['burst']==burst['burst']and r.get('pending_sequence')==edge['pending_sequence']and r.get('HW_tick')==edge['HW_tick']]
                edge['native_endpoints_matched']=bool(matched)and all(r['native_endpoints_matched']for r in matched)
                if edge['native_endpoints_matched'] and stage['native_mapping_proven']:
                    summary[stage['name']]+=1
    pipeline['endpoint_matched_rising_edges_by_stage']=dict(summary)
