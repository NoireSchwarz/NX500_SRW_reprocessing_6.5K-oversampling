"""Decode bounded registered-owner pins and the latest rejected partial attempt.

Context/source verification bits attest guarded production checks. All pointer,
type, callback, member, timer-hash and vector-stability flags are recomputed.
"""
import struct
from video_completion import fnv

FIELDS=['owner','callback','selected_strategy','View_member','Still_member','LDC_parent_type','LDC_state',
        'IPC_backend_pointer','IPC_backend_type','IPC_parent_type','owner_primary_type','owner_notify_type']
STAGES={0:'none',1:'sample-context',2:'timer-before-read',3:'timer-after-read',4:'timer-header-changed-or-invalid',
 5:'object-vector-changed',10:'registered-owner-read',11:'callback-slot-read',12:'selected-slot-read',
 13:'View-member-read',14:'Still-member-read',15:'LDC-parent-read',16:'LDC-state-read',17:'IPC-backend-pointer-read',
 18:'IPC-parent-read',20:'registered-owner-range',21:'registered-owner-type-read',22:'IPC-backend-range',
 23:'IPC-backend-type-read',24:'selected-pointer-range',25:'selected-pointer-differs-from-owner',
 30:'registered-owner-type-pair',31:'callback-slot-value',32:'canonical-member-mismatch',33:'LDC-parent-type',
 34:'IPC-backend-context',35:'LDC-not-running',40:'driver-read',41:'preset-read',42:'mode-changed',43:'source-type',44:'source-root',45:'source-state'}
FLAG_NAMES=['sample-context','timer-coherence','required-reads','object-stability','callback','LDC-parent',
 'IPC-backend','registered-owner-range-and-equality','registered-owner-type-pair','canonical-member','source-context',
 'selected-slot-read','selected-pointer-RAM','selected-matches-owner','LDC-running-state']
REQUIRED=0xffb
BIND_REQUIRED=2047|16384
def ram(p,n):return not p&3 and p>=0x81006000 and p+n<=0x94000000
def owner_ram(p):return p>=12 and ram(p-12,16)
def type_pair(p,n):return(p,n)in [(0x80582c38,0x80582c98),(0x80582d10,0x80582d70),(0x80582da8,0x80582e08)]

def decode_record(raw,slot,total_attempts):
 w=struct.unpack('<48I',raw);a=w[12:24];b=w[24:36];ma,mb=w[8:10]
 if w[0]!=slot or w[47]!=(fnv(raw[:188])^0x6a31d5c8):raise ValueError('Invalid owner diagnostic record commit')
 if (ma|mb)&~0x1fff or w[6]&~0x7fff or not 0<w[11]<=total_attempts:raise ValueError('Invalid owner diagnostic masks/attempt')
 code=w[7]&255;read_pass=(w[7]>>8)&3;blocking=(w[7]>>16)&255;blocking_pass=(w[7]>>24)&3
 if w[7]&~0x03ff03ff or code not in STAGES or blocking not in STAGES or(not code and read_pass)or(not blocking and blocking_pass):raise ValueError('Invalid owner diagnostic stage')
 for v,m in [(a,ma),(b,mb)]:
  if any(v[i]!=0xffffffff for i in range(12)if not m&(1<<i)):raise ValueError('Unread owner value invented')
  if bool(m&1024)!=bool(m&2048):raise ValueError('Partial owner type block')
  if m&0xc00 and(not m&1 or not owner_ram(v[0])):raise ValueError('Unsafe owner type read admitted')
  if m&256 and(not m&128 or not ram(v[7],4)):raise ValueError('Unsafe backend type read admitted')
 if not ma&4096 and w[44]or not mb&4096 and w[45]:raise ValueError('Unread timer hash invented')
 timer=bool(ma&mb&4096 and (w[36],w[37])==(w[39],w[40])and(w[36],w[37])in [(0x39544c56,9),(0x41544c56,10),(0x42544c56,11)] and w[38]==w[41]and w[44]==w[45])
 changed=sum(1<<i for i in range(12)if(ma^mb)&(1<<i)or a[i]!=b[i])|(0 if timer else 4096)
 if changed!=w[10]:raise ValueError('Owner diagnostic change mask disagrees')
 f=w[6]&(1|1024)
 if f&1024 and not f&1:raise ValueError('Source valid without sample context')
 if f&1 and(w[3]not in [0x1717,0x1818,0x1919]or w[4]not in [0xe004,0xe005]):raise ValueError('Invalid video context claim')
 if timer:f|=2
 if ma&REQUIRED==REQUIRED and mb&REQUIRED==REQUIRED:f|=4
 if f&4 and not changed&REQUIRED:f|=8
 if a[1]==b[1]==0x801ca2f0:f|=16
 if a[5]==b[5]==0x80582ba8:f|=32
 if a[9]==b[9]==0x80592110 and a[7]==b[7]and ram(a[7],4)and a[8]==b[8]==w[42]and a[8]in [0x80592268,0x80592328]:f|=64
 if a[0]==b[0]and owner_ram(a[0]):f|=128
 if type_pair(a[10],a[11])and a[10]==b[10]and a[11]==b[11]:f|=256
 member=3 if a[10]==0x80582d10 else 4 if a[10]==0x80582da8 else 2
 if f&128 and ma&mb&(1<<member)and a[member]==b[member]==a[0]-12:f|=512
 if a[6]==b[6]==3:f|=16384
 if ma&mb&4:
  f|=2048
  if ram(a[2],16)and ram(b[2],16):f|=4096
  if f&128 and a[2]==b[2]==a[0]-12:f|=8192
 if f!=w[6]:raise ValueError('Owner diagnostic flags disagree with raw values')
 verified=f&BIND_REQUIRED==BIND_REQUIRED
 if(not code and f!=0x7fff)or(verified and code not in [0,12,24,25]):raise ValueError('Owner diagnostic stage contradicts qualification')
 if not verified and not code:raise ValueError('Rejected attempt missing diagnostic stage')
 expected_block=0;expected_pass=0
 if not f&1:expected_block=1
 elif not timer:expected_block=2 if not ma&4096 else 3 if not mb&4096 else 4
 elif ma&mb&1 and(not owner_ram(a[0])or not owner_ram(b[0])):expected_block=20;expected_pass=1 if not owner_ram(a[0])else 2
 elif ma&mb&128 and(not ram(a[7],4)or not ram(b[7],4)):expected_block=22;expected_pass=1 if not ram(a[7],4)else 2
 elif not f&4:
  stages=[10,11,12,13,14,15,16,17,23,18,21,21]
  i=next(i for i in range(12)if REQUIRED&(1<<i)and(not ma&(1<<i)or not mb&(1<<i)))
  expected_block=stages[i];expected_pass=1 if not ma&(1<<i)else 2
 elif not f&8:expected_block=5
 elif not f&16:expected_block=31
 elif not f&32:expected_block=33
 elif not f&16384:expected_block=35
 elif not f&64:expected_block=34
 elif not f&128:expected_block=20
 elif not f&256:expected_block=30
 elif not f&512:expected_block=32
 elif not f&1024:
  if blocking not in range(40,46)or blocking_pass!=3:raise ValueError('Invalid source rejection stage')
  expected_block=blocking;expected_pass=3
 if(blocking,blocking_pass)!=(expected_block,expected_pass):raise ValueError('Rejection stage disagrees with failed checks')
 def vector(v,m):return{key:dict(raw=hex(v[i]),read_success=bool(m&(1<<i)))for i,key in enumerate(FIELDS)}
 return dict(sequence=slot,generation=w[2],elapsed_s=w[1]/1000,driver=hex(w[3]),preset=hex(w[4]),source_state=w[5],
  source_pointer=hex(w[43]),flags=hex(f),attempt_number=w[11],diagnostic_stage=STAGES[code],diagnostic_stage_code=code,
  diagnostic_read_pass=read_pass,rejection_stage=STAGES[blocking],rejection_stage_code=blocking,rejection_read_pass=blocking_pass,
  failed_checks=[name for i,name in enumerate(FLAG_NAMES)if(i<11 or i==14)and not f&(1<<i)],
  raw_before=vector(a,ma),raw_after=vector(b,mb),read_mask_before=hex(ma),read_mask_after=hex(mb),change_mask=hex(changed),
  LDC_owner=hex(a[0]),LDC_owner_after=hex(b[0]),LDC_operation=hex(a[0]-12)if owner_ram(a[0])else None,
  LDC_primary_type=hex(a[5]),LDC_operation_type=hex(a[10]),LDC_notify_type=hex(a[11]),
  selected_strategy_before=hex(a[2]),selected_strategy_after=hex(b[2]),
  selected_strategy_read=bool(f&2048),selected_strategy_RAM=bool(f&4096),selected_strategy_matches_owner=bool(f&8192),
  View_member_before=hex(a[3]),View_member_after=hex(b[3]),Still_member_before=hex(a[4]),Still_member_after=hex(b[4]),
  callback10=hex(a[1]),callback10_after=hex(b[1]),LDC_state=a[6],LDC_state_after=b[6],
  backend_pointer=hex(a[7]),backend=hex(a[8]),parent_type=hex(a[9]),expected_backend=hex(w[42]),
  timer_schema=w[37]if ma&4096 else None,timer_schema_after=w[40]if mb&4096 else None,
  native_sequence=w[38]if ma&4096 else None,native_sequence_after=w[41]if mb&4096 else None,
  timer_hash_before=hex(w[44]),timer_hash_after=hex(w[45]),read_span_us=w[46],
  registered_owner_type_verified=f&(4|8|16|32|128|256|512)==(4|8|16|32|128|256|512),
  object_binding_valid=verified,association_basis='native-registered-notification-owner-and-canonical-member',
  operation_pointer_source='registered-owner-minus12',native_buffer_identity_proven=False)

def decode_ldc_routes4(data):
 o=15552;magic,s,size,cap,count,overflow,failed=struct.unpack_from('<8s6I',data,o)
 if(magic,s,size,cap)!=(b'NXLERTE1',4,192,4)or count>3 or overflow>1 or failed>6000:raise ValueError('Invalid registered-owner header')
 if any(data[o+32+count*192:o+32+3*192]):raise ValueError('Uncommitted verified-owner tail')
 pins=[];generations=set();last_attempt=0
 for i in range(count):
  p=decode_record(data[o+32+i*192:o+32+(i+1)*192],i,count+failed)
  if not p['object_binding_valid']or p['generation']in generations or p['attempt_number']<=last_attempt:raise ValueError('Invalid verified-owner pin')
  generations.add(p['generation']);last_attempt=p['attempt_number'];pins.append(p)
 raw=data[o+32+3*192:o+800];rejected=None
 if failed:
  rejected=decode_record(raw,3,count+failed)
  if rejected['object_binding_valid']:raise ValueError('Verified owner placed in rejected slot')
 elif any(raw):raise ValueError('Uncommitted rejected-owner slot')
 diagnostics=sorted(pins+([rejected]if rejected else[]),key=lambda r:r['attempt_number'])
 return dict(schema='NXL_LDC_ROUTE4',count=count,capacity=cap,verified_capacity=3,overflow=bool(overflow),
  failed_attempts=failed,records=pins,latest_rejected_attempt=rejected,diagnostic_records=diagnostics,
  overwritten_rejected_attempts=max(0,failed-1),
  interpretation='Three verified pins plus the latest rejected partial attempt; earlier rejections are counted but overwritten. Raw read masks distinguish unavailable values from null/invalid values. Selected strategy is diagnostic; View/Still owner identity uses exact notification types and matching canonical member. Context/source checks and native timer coherence are required for statistics. This is not selected-operation, uninterrupted-lifetime or DMA-buffer identity proof.')
