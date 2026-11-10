"""Read R7g passive traces; preserve verified prefixes after a power loss.

No files are executed. Sampling, a matching RAM value or a static function
name alone does not establish native call ownership or hardware activation.
"""
from pathlib import Path
import argparse,collections,json,re,struct,zlib
MAGIC=0x3152584e
def frames(path):
 result=[];status={'verified_frames':0,'verified_bytes':0,'tail_bytes':0,'error':None}
 if not path.is_file():status['error']='MISSING_FILE';return result,status
 size=path.stat().st_size
 if size>70*1024*1024:raise ValueError('trace exceeds producer bound: '+path.name)
 with path.open('rb') as f:
  while f.tell()<size:
   start=f.tell();raw=f.read(40)
   if len(raw)!=40:status.update(tail_bytes=size-start,error='INCOMPLETE_HEADER');break
   h=struct.unpack('<10I',raw);kind,sequence=h[1:3];begin=h[3]|h[4]<<32;end=h[5]|h[6]<<32
   if h[0]!=MAGIC or kind not in range(1,7) or h[7]>1024*1024 or zlib.crc32(raw[:36])!=h[9] or end<begin:
    status.update(tail_bytes=size-start,error='INVALID_HEADER');break
   body=f.read(h[7])
   if len(body)!=h[7]:status.update(tail_bytes=size-start,error='INCOMPLETE_PAYLOAD');break
   if zlib.crc32(body)!=h[8]:status.update(tail_bytes=size-start,error='INVALID_PAYLOAD_CRC');break
   result.append((kind,sequence,begin,end,body));status['verified_frames']+=1;status['verified_bytes']=f.tell()
 return result,status
def largest_span(raw):
 if len(raw)!=45056*16:return None
 w=struct.unpack('<%dI'%(len(raw)//4),raw);covered=run=largest=0
 for i in range(45056):
  kind=w[i*4]&255;span=w[i*4+1]
  if kind:
   if not span or span>45056-i:return None
   covered=max(covered,i+span)
  if i>=covered and not kind and not span:run+=1;largest=max(largest,run)
  else:run=0
 return largest*16384
def audit_file(path):
 b=path.read_bytes();out=[]
 if len(b)!=480:raise ValueError('lifecycle record extent')
 stages=['before_stop','before_config','after_config','before_start','after_completion','after_cleanup']
 for i in range(6):
  w=list(struct.unpack_from('<20I',b,i*80));h=2166136261
  for x in b[i*80+4:i*80+72]:h=((h^x)*16777619)&0xffffffff
  valid=w[0]==0x31414c53 and w[1]==1 and w[3]==i+1 and w[18]==h and w[19]==(w[2]^w[3]^h^0x31414c53)
  out.append(dict(stage=stages[i],transport_valid=valid,nonce=w[2],task_state=w[4]&255,global_request=hex(w[5]),
   semaphores=[dict(id_before=w[k],return_code=struct.unpack('<i',struct.pack('<I',w[k+1]))[0],info_words=w[k+2:k+5],
    count=w[k+4] if valid and w[k+1]==0 else None,id_after=w[16+j]) for j,k in enumerate([6,11])]))
 return out
def analyze(root):
 root=Path(root);schema=json.loads((root/'schema.json').read_text());assert schema['format']=='NXINITBROAD1'
 addresses=schema['hot_addresses'];keys=schema['repeat_key_addresses'];assert len(addresses)+len(keys)<=96
 streams={};hot=[];allocator=[];linux=[];typed=[];stacks=[];contexts=collections.Counter();previous=-1;last_key=None;represented=0;gaps=[];raced=0
 labels={0x8153f3f8:'still_state',0x81540f38:'request_kind',0x8142de78:'ep_state',0x867555c4:'kernel_selector',0x86755238:'acc_mask',0x867554c4:'kernel_error',0x81eac910:'free_blocks'}
 for leaf in ['hot.bin','contexts.bin','allocator.bin','linux-context.bin']:
  rows,streams[leaf]=frames(root/leaf)
  for kind,seq,begin,end,body in rows:
   if leaf=='hot.bin':
    if kind!=1 or len(body)!=(4+len(addresses)+6)*4:raise ValueError('hot typed extent')
    w=struct.unpack('<%dI'%(len(body)//4),body);skip=w[0];mask=w[1:4];values=w[4:]
    if seq-previous!=skip+1:raise ValueError('hot skipped-sample chain')
    represented+=skip+1;previous=seq
    def value(index):return None if mask[index//32]&(1<<(index%32)) else values[index]
    decoded={name:value(addresses.index(a)) for a,name in labels.items() if a in addresses}
    for name in ['still_state','ep_state']:
     if decoded.get(name) is not None:decoded[name]&=255
    race=any(value(addresses.index(a)) is not None and value(len(addresses)+j) is not None and value(addresses.index(a))!=value(len(addresses)+j) for j,a in enumerate(keys))
    raced+=race;key=tuple(decoded.values())
    if key!=last_key:hot.append(dict(sequence=seq,begin_mono_ns=begin,end_mono_ns=end,raced=race,**decoded));last_key=key
   elif leaf=='contexts.bin':
    if kind==2:
     if len(body)!=4104:raise ValueError('context typed extent')
     address,available=struct.unpack_from('<II',body);contexts[hex(address)]+=1
    elif kind==5:
     if len(body)<8 or len(body)%4:raise ValueError('P36 typed extent')
     w=struct.unpack('<%dI'%(len(body)//4),body);rc=struct.unpack('<i',body[:4])[0]
     if w[1]>4096 or len(w)!=2+w[1]:raise ValueError('P36 word count')
     typed.append(dict(begin_mono_ns=begin,end_mono_ns=end,return_code=rc,words=w[1],
      still_vtable=hex(w[2]) if len(w)>2 else None,still_state=w[3]&255 if rc==1 and len(w)>3 and w[2]==0x80592640 else None,
      note='Full typed prefix retained in contexts.bin; schema is the supplied passive_profile C structs.'))
    else:raise ValueError('context stream kind')
   elif leaf=='allocator.bin':
    if kind!=3 or len(body)!=16+45056*16:raise ValueError('allocator typed extent')
    before,after,available,changed=struct.unpack_from('<4I',body);span=largest_span(body[16:]) if available else None
    allocator.append(dict(begin_mono_ns=begin,end_mono_ns=end,free_before=before,free_after=after,readable=bool(available),head_changed=bool(changed),
     atomic=False,largest_observed_free_span_bytes=span,metadata_consistent=span is not None,
     observed_span_meets_mod_backend_192MiB=span>=201326592 if span is not None and not changed else None))
   elif leaf=='linux-context.bin':
    if kind==6:
     if len(body)<24:raise ValueError('stack prefix')
     tid,nr,sp,pc,requested,actual=struct.unpack_from('<6I',body)
     if requested!=2048 or actual>requested or len(body)!=24+actual:raise ValueError('stack bounded extent')
     stacks.append(dict(begin_mono_ns=begin,end_mono_ns=end,tid=tid,syscall=nr,sp=hex(sp),pc=hex(pc),captured_bytes=actual,
      note='Raw stack, not an unwound backtrace; interpret with the captured process maps.'));continue
    if kind!=4 or len(body)!=83*4:raise ValueError('Linux context typed extent')
    w=struct.unpack('<83I',body);base,valid=w[:2]
    linux.append(dict(begin_mono_ns=begin,end_mono_ns=end,udd_base=hex(base),validity_mask=hex(valid),
     ep_fd=w[2+(0x5556c-0x55560)//4] if valid&1 else None,sma_fd=w[2+(0x55574-0x55560)//4] if valid&1 else None,
     open_count=w[2+36+16+(0x5708c-0x57080)//4] if valid&4 else None,mutex_words=list(w[2+36+2:2+36+8]) if valid&2 else None,
     app_words=[w[78+i] if valid&(1<<(i+4)) else None for i in range(4)]))
 ticks=[];waits=[];ended=False
 p=root/'context.jsonl'
 if p.is_file():
  with p.open(encoding='utf8',errors='replace') as f:
   for line in f:
    try:v=json.loads(line)
    except json.JSONDecodeError:continue
    if v.get('event')=='sampling_tick':ticks.append(v)
    if v.get('event')=='ui_wait':waits.append(v)
    if v.get('event')=='end':ended=True
 hits=[];pattern=re.compile(r'(Ipc_Initialize|Ipc_Finalize|d5_ep_open|decodeJpeg|resizeYcc|encodeYcc|mm_photo_|runFromCapture|captureSaveDone|assert|panic|alloc.*fail|out.of.memory|semaphore)',re.I)
 for leaf in ['camera-dlog.txt','rtos-console.txt']:
  p=root/leaf
  if p.is_file():
   with p.open(encoding='utf8',errors='replace') as f:
    for i,line in enumerate(f,1):
     if len(hits)<2000 and pattern.search(line):hits.append(dict(file=leaf,line=i,text=line[:1000].rstrip()))
 return dict(format='NXINITBROAD_ANALYSIS1',transport=streams,observer_end_seen=ended,hot_represented_samples=represented,
  hot_context_races_observed=raced,hot_state_timeline=hot,context_page_frames=dict(contexts),typed_metadata=typed,allocator=allocator,linux_context=linux,user_stacks=stacks,
  sampling_ticks=ticks,ui_waits=waits,log_hits=hits,
  limits=['Snapshots are sequential and do not acquire ownership.','Skipping equal samples is change suppression; it is not missing-sample reconstruction.',
   'Sampling gaps and uncommitted tails are retained; a power loss need not invalidate a verified prefix.',
   'Allocator head stability does not prove an atomic allocator snapshot.','Log-drain times are not native event timestamps.',
   'Static API names and RAM correspondence do not alone prove calls or hardware adoption.'])
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('trace',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 result=analyze(a.trace);output=a.output or a.trace/'PC_ANALYSIS.json';output.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
 print(json.dumps(dict(output=str(output),hot_samples=result['hot_represented_samples'],stream_status=result['transport']),ensure_ascii=True))
