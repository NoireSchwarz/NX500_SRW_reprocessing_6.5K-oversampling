"""Independent schema4 certificate checks. Raw syscall numbers stay raw.

The verified original timed-cond-wait site is stronger than this kernel's
potentially stale proc number. It is still firmware/IRQ/scheduling wait,
never an HEVC hardware busy-time counter or an unwound backtrace.
"""
import struct
from video_completion import fnv

WORDS=[0xe3a070f0,0xe1a0200a,0xe22b1080,0xe28d3030,0xe58d0020,0xe1a00009,0xef000000,0xe1a05000]
HASH=fnv(struct.pack('<8I',*WORDS))
REASONS=['library-unverified','task-pair-changed','wrong-PC','wrong-futex-arguments','semaphore-read-incomplete',
 'semaphore-object-changed','semaphore-not-negative-at-both-ends','code-before-unavailable','code-after-unavailable',
 'original-instruction-mismatch','stack-unavailable','encoder-return-PC-missing','wait-return-PC-missing',
 'typed-object-or-device-association-failed','thread-lifetime-or-inventory-binding-failed']
PROBES=['Y_top_left','Y_middle_left','Y_bottom_left','UV_bottom_left','Y_bottom_right','UV_bottom_right']

def signed(v):return v if v<0x80000000 else v-0x100000000
def user(p,n):return 0x10000<=p and p%4==0 and p+n<=0xbfff0000
def names(mask):return [s for i,s in enumerate(REASONS)if mask>>i&1]
def joint_rejection(a,b,base,modules,sa,sb,sm,ca,cb,cm,stack,bytes_,objects):
    # tuple: tid,start,status,raw_nr,args,SP,PC. nr is deliberately independent.
    bad=0
    if not base:bad|=1
    if not(a[0]>1 and a[0]==b[0]and a[1]and a[1]==b[1]and a[2]==b[2]==1 and a[4:]==b[4:]):bad|=2
    if not base or a[6]!=base+0xbd04 or b[6]!=base+0xbd04:bad|=4
    if sm!=63:bad|=16
    if sm==63 and sa[:2]!=sb[:2]:bad|=32
    if sm==63 and(signed(sa[2])>=0 or signed(sb[2])>=0):bad|=64
    if not user(sa[1],80)or a[4][0]!=sa[1]+12 or b[4][0]!=sa[1]+12 or a[4][1]&127 or b[4][1]&127:bad|=8
    if not cm&1:bad|=128
    if not cm&2:bad|=256
    if cm&1 and ca!=WORDS or cm&2 and cb!=WORDS:bad|=512
    if bytes_!=256:bad|=1024
    if modules+0x2cc74 not in stack:bad|=2048
    if modules+0x260d0 not in stack:bad|=4096
    if not objects:bad|=8192
    return bad

def decode_discovery(flags,d):
    if flags&~1 or d[3]not in [0,4]or d[17]not in [0,256]or d[87]&~63 or d[117]&~3 or d[118]&~0x7fff or d[119]!=1 or d[137]&~63 or d[158]&~7 or d[159]not in [0,1]or d[160]!=8:
        raise ValueError('Invalid verified wait certificate extent/policy')
    a=(d[4],d[5]|d[6]<<32,d[7],signed(d[8]),d[9:15],d[15],d[16])
    b=(d[124],d[125]|d[126]<<32,d[127],signed(d[128]),d[129:135],d[135],d[136])
    ia,ib=d[138:146],d[146:154];modules,core=d[82],d[185]
    same_ids=ia[:5]==ib[:5]and ia[6:]==ib[6:]
    objects=(d[137]==63 and same_ids and user(ia[0],0x564)and user(ia[2],0x168)and user(ia[6],0x178)and
        ia[1]==modules+0x4c608 and ia[3]==core+0x59c28 and ia[4]==2 and ia[7]==modules+0x4c9c8 and
        d[161:165]==d[165:169]and d[161]==modules+0x5b148 and d[169:177]==d[177:185]and
        d[83]==ia[6]and d[84]==d[154]==d[155]<4096 and d[86]==d[156]and d[86]in [1,2])
    bad=joint_rejection(a,b,d[96],modules,d[88:92],d[92:96],d[87],d[101:109],d[109:117],d[117],d[18:18+d[17]//4],d[17],objects)
    if a[0]!=d[186]:bad|=16384
    # select() also verifies /proc stat starttime. Its failure is explicitly recorded.
    reported=d[118]
    if reported&~16384!=bad&~16384 or bad&16384 and not reported&16384:raise ValueError('Wait certificate rejection disagrees with raw evidence')
    if d[159]!=(a[3]==b[3]):raise ValueError('Raw syscall-number equality flag changed')
    for bit,at,h in [(1,101,120),(2,109,121)]:
        if d[h]!=(fnv(struct.pack('<8I',*d[at:at+8]))if d[117]&bit else 0):raise ValueError('Instruction FNV disagrees with raw words')
    def site(t):return 0 if not d[96]or t[2]!=1 else {d[96]+0xbd04:1,d[96]+0xb980:2,d[96]+0xee04:3}.get(t[6],0)
    if d[122]!=site(a)or d[123]!=site(b):raise ValueError('Wait-site label disagrees with PC')
    if d[3]!=(0 if reported else 4)or bool(flags)!=bool(d[3]):raise ValueError('False selected verified wait certificate')
    if not d[158]or bool(d[158]&1)!=bool(d[3]):raise ValueError('Invalid discovery retention reason')
    return dict(task_index=d[0],inventory_count=d[1],inventory_status=d[2],proof=d[3],selected=bool(flags),
        tid=a[0],start_ticks=a[1],syscall_status=a[2],syscall=a[3],raw_syscall_after=b[3],raw_syscall_pair_equal=a[3]==b[3],
        inferred_syscall=240 if not reported else None,inference_basis='Verified original PC/instructions + typed HEVC object + paired syscall view + both original return PCs'if not reported else None,
        arguments=[hex(v)for v in a[4]],SP=hex(a[5]),PC=hex(a[6]),stack_bytes=d[17],attempted_tasks=d[85],device_path_kind=d[86],
        semaphore_raw_words=d[87:96],HEVC_semaphore_wrapper=hex(d[88]),HEVC_semaphore_data=hex(d[89]),HAL=hex(d[83]),
        modules_base=hex(modules),core_base=hex(core),pthread_base=hex(d[96]),source=hex(ia[2]),camera=hex(ia[0]),
        runtime_instruction_words_before=[hex(v)for v in d[101:109]]if d[117]&1 else None,
        runtime_instruction_words_after=[hex(v)for v in d[109:117]]if d[117]&2 else None,
        instruction_read_mask=hex(d[117]),rejection_mask=hex(reported),rejection_reasons=names(reported),verified_wait_policy=d[119],
        retained_because=dict(selected=bool(d[158]&1),new_rejection=bool(d[158]&2),inventory_landmark=bool(d[158]&4)),
        task_read_failures=dict(before_stage=d[97],before_errno=d[98],after_stage=d[99],after_errno=d[100]),
        interpretation='Raw proc number retained verbatim, never promoted by itself. Both known ordinary-condwait and write sites are negative candidates.')

def stage_proof(flags,d,identity,hal):
    if d[237]&~0x7fff0f:raise ValueError('Unknown joint wait proof flags')
    cm=(d[237]>>2)&3;reported=d[237]>>8;fdpath=d[237]&3
    a=(d[96],d[97]|d[98]<<32,d[99],signed(d[100]),d[101:107],d[107],d[108])
    b=(d[96],d[97]|d[98]<<32,d[109],signed(d[110]),d[111:117],d[117],d[118])
    # Full instruction words are retained in the required discovery certificate.
    ca=WORDS if cm&1 and d[91]==HASH else [0]*8
    cb=WORDS if cm&2 and d[229]==HASH else [0]*8
    objects=bool(identity and hal and d[59]==2 and d[66:70]==d[70:74]and d[119]<4096 and fdpath in [1,2])
    bad=joint_rejection(a,b,d[246],d[227],[d[235],d[236],d[241],d[243]],[d[238],d[239],d[242],d[244]],d[240],ca,cb,cm,d[120:120+d[184]//4],d[184],objects)
    # Negative task-read/lifetime/fd-after details can be stricter in the C reader.
    if d[185] and(not flags&512 or not flags&1024 or reported or bad or d[185]!=4):raise ValueError('False verified stage wait proof')
    if d[185]not in [0,4]:raise ValueError('Legacy ioctl proof is forbidden in schema4')
    if flags&2048 and(not flags&512 or a[2]!=1 or a[6]!=d[246]+0xbd04 or not cm&1 or d[91]!=HASH or a[4][0]!=d[236]+12 or a[4][1]&127):
        raise ValueError('Stack read outside verified original wait site/object')
    for bit,field in [(1,91),(2,229)]:
        if not cm&bit and d[field]:raise ValueError('Instruction hash without successful read')
    return dict(raw_syscall_after=b[3]if flags&1024 else None,raw_syscall_pair_equal=a[3]==b[3]if flags&1536==1536 else None,
        inferred_syscall=240 if d[185]==4 else None,verified_wait_policy=1,instruction_read_mask=hex(cm),
        instruction_FNV_before=hex(d[91]),instruction_FNV_after=hex(d[229]),wait_rejection_mask=hex(reported),wait_rejection_reasons=names(reported))
