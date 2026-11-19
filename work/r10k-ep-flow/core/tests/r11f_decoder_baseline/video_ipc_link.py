"""Independent sparse typed CImage/RDMA0 observations, including failed reads."""
from video_completion import fnv
import struct
FIELDS=['parent_type','parent_state','backend_pointer','backend_type','driver','preset','input_descriptor','input_type','input_physical_address','RDMA0_programmed_address','output_descriptor','output_type']
def ram(p,n=4):return not p&3 and p>=0x81006000 and p+n<=0x94000000
def typed(v):return v[0]==0x80592110 and v[1]&255==3 and v[7]==v[11]==0x80588910 and ram(v[6],0x70)and ram(v[10])and v[6]!=v[10]
def decode_ipc_links1(samples,started_ns):
 rows=[]
 for s in samples:
  if not s['valid']&2097152:continue
  w=s['ep_readback'];a=w[4:16];b=w[16:28]
  if len(w)!=32 or w[0]!=0x31555049:raise ValueError('Invalid IPC link extension')
  f=w[1];ma=f&4095;mb=(f>>12)&4095
  for vals,mask in [(a,ma),(b,mb)]:
   if any(v!=0xffffffff for i,v in enumerate(vals)if not mask&(1<<i)):raise ValueError('Unread IPC value invented')
   if mask&8 and(not mask&4 or not ram(vals[2])):raise ValueError('Unsafe IPC backend type read')
   if mask&0x180 and(not mask&64 or not ram(vals[6],0x70)):raise ValueError('Unsafe IPC input dereference')
   if mask&0x800 and(not mask&0x400 or not ram(vals[10])):raise ValueError('Unsafe IPC output dereference')
  if not f&(1<<25)and(w[3]!=0xffffffff or w[28]!=0xffffffff or w[30]):raise ValueError('Unread IPC timer-before invented')
  if not f&(1<<26)and(w[29]!=0xffffffff or w[31]):raise ValueError('Unread IPC timer-after invented')
  if f&(1<<27)and(f&0x6000000!=0x6000000 or w[28]!=w[29]or w[30]!=w[31]):raise ValueError('IPC timer coherence disagreement')
  expected=f&0x0fffffff
  if ma==mb==4095 and a==b:expected|=1<<28
  if ma==mb==4095 and typed(a)and typed(b):expected|=1<<29
  if ma&mb&8 and a[3]==b[3]==s['hevc'][9]and a[3]in [0x80592268,0x80592328]:expected|=1<<30
  if expected==0x7fffffff and a[4]==s['rtos'][0]and a[5]==s['rtos'][2]and a[8]and a[8]==a[9]:expected|=1<<31
  if f!=expected:raise ValueError('IPC link derived flags disagree')
  if not f&0x1000000 or a[4]not in [0x1717,0xffffffff]or a[5]not in [0xe005,0xffffffff]:raise ValueError('Invalid IPC link sample context')
  raw=lambda vals,mask:{name:dict(raw=hex(vals[i]),read_success=bool(mask&(1<<i)))for i,name in enumerate(FIELDS)}
  pending=(w[28]+1)&0xffffffff
  if not pending:pending=1
  rows.append(dict(sample=s['sequence'],observed_at_s=(s['t']-started_ns)/1e9,flags=hex(f),qualified=bool(f&0x80000000),read_span_us=w[2],HW_tick=w[3]if f&(1<<25)else None,completed_native_sequence=w[28]if f&(1<<25)else None,pending_native_sequence=pending if f&(1<<25)else None,timer_hash_before=hex(w[30]),timer_hash_after=hex(w[31]),raw_before=raw(a,ma),raw_after=raw(b,mb),driver=hex(a[4]),preset=hex(a[5]),backend=hex(a[3]),source_pointer=hex(s['source'][0]),source_state=s['source'][5],input_descriptor=hex(a[6]),input_physical_address=hex(a[8]),RDMA0_programmed_address=hex(a[9]),output_descriptor=hex(a[10]),output_DMA_address_observed=False))
 return dict(schema='NXL_IPC_LINK1',samples=len(rows),qualified=sum(r['qualified']for r in rows),records=rows,interpretation='Independent double-read native IPC parent, active backend, CImage types, input physical address and controller0 RDMA0 programmed address, bracketed by timer10. A programmed address is not hardware-latched buffer identity or DMA retirement. The output is a typed descriptor, not an encoder/DMA surface. Failed raw reads and masks are retained.')

def decode_ipc_links(samples,started_ns):
 selected=[s for s in samples if s['valid']&2097152]
 if not any(s['ep_readback'][0]==0x32555049 for s in selected):
  return decode_ipc_links1(samples,started_ns)
 rows=[]
 for s in selected:
  w=s['ep_readback'];pair=s['progress']
  if not s['valid']&4194304 or s['valid']&262144 or len(w)!=32 or len(pair)!=32 or w[0]!=0x32555049:
   raise ValueError('Missing or conflicting IPC paired-header extension')
  f=w[1];ma=f&4095;mb=(f>>12)&4095;a=w[4:16];b=w[16:28];ha=pair[:16];hb=pair[16:]
  ar=bool(f&(1<<25));br=bool(f&(1<<26));route_reads=w[31]
  if route_reads&~15:raise ValueError('Invalid slot6 read mask')
  for bit,index in [(1,28),(2,3),(4,29),(8,30)]:
   if not route_reads&bit and w[index]!=0xffffffff:raise ValueError('Unread slot6 value invented')
  for vals,mask in [(a,ma),(b,mb)]:
   if any(v!=0xffffffff for i,v in enumerate(vals)if not mask&(1<<i)):raise ValueError('Unread IPC value invented')
   if mask&8 and(not mask&4 or not ram(vals[2])):raise ValueError('Unsafe IPC backend type read')
   if mask&0x180 and(not mask&64 or not ram(vals[6],0x70)):raise ValueError('Unsafe IPC input dereference')
   if mask&0x800 and(not mask&0x400 or not ram(vals[10])):raise ValueError('Unsafe IPC output dereference')
  for h,ok in [(ha,ar),(hb,br)]:
   if not ok and h!=[0xffffffff]*16:raise ValueError('Unread RTOS header invented')
  recognized=ar and ha[:2]==[0x42544c56,11]
  coherent=recognized and br and ha==hb
  inflight=bool(coherent and ha[3]&15==3 and not ha[3]&0x10000 and ((ha[3]>>24)&3)==0)
  stable=ma==mb==4095 and a==b
  expected=f&0x07ffffff
  if coherent:expected|=1<<27
  if stable:expected|=1<<28
  if ma==mb==4095 and typed(a)and typed(b):expected|=1<<29
  if ma&mb&8 and a[3]==b[3]==s['hevc'][9]and a[3]in [0x80592268,0x80592328]:expected|=1<<30
  if expected==0x7fffffff and inflight and ha[3]&512 and a[4]==s['rtos'][0]and a[5]==s['rtos'][2]and a[8]and a[8]==a[9]and ha[9:12]==[a[6],a[8],a[10]]:
   expected|=1<<31
  if f!=expected:raise ValueError('IPC paired-link derived flags disagree')
  if not f&0x1000000 or a[4]not in [0x1717,0xffffffff]or a[5]not in [0xe005,0xffffffff]:raise ValueError('Invalid paired IPC context')
  raw=lambda vals,mask:{name:dict(raw=hex(vals[i]),read_success=bool(mask&(1<<i)))for i,name in enumerate(FIELDS)}
  header=lambda h,ok:dict(read_success=ok,raw_words=[hex(x)for x in h],recognized_schema11=bool(ok and h[:2]==[0x42544c56,11]),completed_sequence=h[2]if ok else None,flags=hex(h[3])if ok else None,input_descriptor=hex(h[9])if ok else None,input_physical_address=hex(h[10])if ok else None,output_descriptor=hex(h[11])if ok else None,first_bit6_tick=h[12]if ok else None,latest_binding_error_raw=hex(h[13])if ok else None,last_bit6_callback=hex(h[14])if ok else None,total_accepted_bit6=h[15]if ok else None)
  mismatches=[]
  if inflight and stable:
   for index,j,name in [(9,6,'input-descriptor'),(10,8,'input-physical-address'),(11,10,'output-descriptor'),(10,9,'RTOS-input-PA-versus-RDMA0')]:
    if ha[index]!=a[j]:mismatches.append(name)
  slot6=route_reads==15 and w[28]==w[29]==0x801cdf4c and w[3]==w[30]and bool(w[3]&64)
  native_count=ha[3]>>28 if inflight else None
  pending=(ha[2]+1)&0xffffffff if inflight else None
  if pending==0:pending=1
  rows.append(dict(sample=s['sequence'],observed_at_s=(s['t']-started_ns)/1e9,flags=hex(f),qualified=bool(f&0x80000000),read_span_us=w[2],HW_tick=ha[5]if recognized else None,completed_native_sequence=ha[2]if recognized else None,pending_native_sequence=pending,native_header_coherent=bool(coherent),native_request_inflight=inflight,RTOS_before=header(ha,ar),RTOS_after=header(hb,br),raw_before=raw(a,ma),raw_after=raw(b,mb),slot6_read_mask=hex(route_reads),slot6_callback_before=dict(raw=hex(w[28]),read_success=bool(route_reads&1)),slot6_callback_after=dict(raw=hex(w[29]),read_success=bool(route_reads&4)),IRQ44_enable_before=dict(raw=hex(w[3]),read_success=bool(route_reads&2)),IRQ44_enable_after=dict(raw=hex(w[30]),read_success=bool(route_reads&8)),Linux_slot6_verified=slot6,native_bit6_callback_verified=bool(native_count and ha[14]==0x801cdf4c and not ha[3]&0x20000),native_bit6_count_min=native_count,native_bit6_count_saturated=native_count==15 if native_count is not None else None,native_Linux_address_mismatches=mismatches,cross_view_discrepancy=bool(mismatches),driver=hex(a[4]),preset=hex(a[5]),backend=hex(a[3]),source_pointer=hex(s['source'][0]),source_state=s['source'][5],input_descriptor=hex(a[6]),input_physical_address=hex(a[8]),RDMA0_programmed_address=hex(a[9]),output_descriptor=hex(a[10]),output_DMA_address_observed=False))
 return dict(schema='NXL_IPC_LINK2',samples=len(rows),qualified=sum(r['qualified']for r in rows),slot6_verified=sum(r['Linux_slot6_verified']for r in rows),same_request_address_discrepancies=sum(r['cross_view_discrepancy']for r in rows),records=rows,interpretation='Two full16-word RTOS headers bracket independent Linux typed RAM/MMIO vectors and actual slot6/enable reads. Equal active headers identify the same pending native request. Cross-view discrepancies are retained and excluded, not repaired by cache writes or relaxed predicates. Slot6 verification and address agreement are separate. This is not hardware-latched image/HEVC identity or proof of cache root cause.')
