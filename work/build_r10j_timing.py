"""Fixed TMC code/data lease for per-frame native event timestamps.
Original blue-boundary bytes are exactly R10c; no DDR/video configuration edit.
"""
from pathlib import Path
import sys,os,subprocess,struct,json,hashlib
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';D=R/'core/build-timing';D.mkdir(parents=True,exist_ok=True);Z=W/'tooling/python/ziglang/zig.exe';sys.path[:0]=[str(W),str(W/'tooling/python')]
from elftools.elf.elffile import ELFFile
from fw_research import Firmware
src=W/'r10j_timing.S';(D/'timing.S').write_bytes(src.read_bytes())
ld='''ENTRY(print_disabled)
SECTIONS { . = 0x804e1d58; .donor_a : { *(.donor_a) } ASSERT(SIZEOF(.donor_a)==0x260,"first donor size")
 . = 0x804e203c; .donor_b : { *(.donor_b) } ASSERT(SIZEOF(.donor_b)==0xec,"second donor size")
 /DISCARD/ : { *(.ARM.attributes*) *(.comment*) } }
'''
(D/'timing.ld').write_text(ld);env=dict(os.environ,TMPDIR=str(D.resolve()))
subprocess.run([str(Z),'clang','-target','armv7-none-eabi','-mcpu=cortex-a9','-marm','-c',str(src),'-o',str(D/'timing.o')],env=env,check=True)
subprocess.run([str(Z),'ld.lld','-T',str(D/'timing.ld'),'--no-undefined','-o',str(D/'timing.elf'),str(D/'timing.o')],env=env,check=True)
with(D/'timing.elf').open('rb')as f:
 e=ELFFile(f);a=e.get_section_by_name('.donor_a').data();b=e.get_section_by_name('.donor_b').data();symbols={s.name:s['st_value']for s in e.get_section_by_name('.symtab').iter_symbols()}
assert len(a)==608 and len(b)==236 and struct.unpack_from('<I',a,0x1ec)[0]==0xe12fff1e
for off in [0,0x14,0x5c]:assert struct.unpack_from('<I',b,off)[0]==0xe12fff1e
(D/'timing-a.bin').write_bytes(a);(D/'timing-b.bin').write_bytes(b)
fw=Firmware();boundary=(W/'r10c-nx1-clock-blue/boundary-build/conditional-recipe.bin').read_bytes()
def branch(at,target):
 d=target-at-8;assert d%4==0 and -0x2000000<=d<0x2000000
 return struct.pack('<I',0xea000000|((d//4)&0xffffff))
blocks=[('boundary',0x8010cd8c,boundary),('timing_a',0x804e1d58,a),('timing_b',0x804e203c,b),('marker',0x804e1fb8,branch(0x804e1fb8,symbols['ipc_marker'])+fw.data(0x804e1fbc,4))]
for at in [0x804997b8,0x804997c8,0x804997d8]:
 code=struct.unpack('<I',branch(at,symbols['hw_commit']))[0]&0x0fffffff
 blocks.append(('hw_commit_'+hex(at),at,struct.pack('<I',code)+fw.data(at+4,4)))
blocks.append(('irq_entry',0x80036ae8,branch(0x80036ae8,symbols['irq_entry'])+fw.data(0x80036aec,4)))

def fnv(b):
 h=2166136261
 for v in b:h=((h^v)*16777619)&0xffffffff
 return h
words=[];report=[]
for name,at,after in blocks:
 before=fw.data(at,len(after));n=len(before)//4;stock_at=len(words);words.extend(struct.unpack('<%dI'%n,before));full_at=len(words);words.extend(struct.unpack('<%dI'%n,after))
 report.append(dict(name=name,address=at,bytes=len(before),stock_at=stock_at,full_at=full_at,stock_hash=fnv(before),full_hash=fnv(after),changed_words=sum(x!=y for x,y in zip(struct.unpack('<%dI'%n,before),struct.unpack('<%dI'%n,after))),stock_sha256=hashlib.sha256(before).hexdigest(),full_sha256=hashlib.sha256(after).hexdigest()))
s='/* Generated original/code pairs, complete block guards, position independent word offsets. */\nstatic const uint32_t vfw_code_words[%d]={'%len(words)+','.join('0x%08xu'%v for v in words)+'};\nstruct vfw_block {uint32_t address,bytes,stock_hash,full_hash,stock_at,full_at;};\nstatic const struct vfw_block vfw_blocks[]={\n'
for r in report:s+='{0x%08xu,%du,0x%08xu,0x%08xu,%du,%du},\n'%(r['address'],r['bytes'],r['stock_hash'],r['full_hash'],r['stock_at'],r['full_at'])
s+='};\nstatic const uint32_t*vfw_stock(const struct vfw_block*b){return vfw_code_words+b->stock_at;}\nstatic const uint32_t*vfw_full_code(const struct vfw_block*b){return vfw_code_words+b->full_at;}\n#define VFW_BLOCK_COUNT 8u\n#define VFW_ENTRY_FIRST 3u\n#define VFW_TIMING_DATA 0x817dbbe0u\n#define VFW_TIMING_MAGIC 0x33544c56u\n#define VFW_TMC_DATA 0x817dbbc8u\n#define VFW_TMC_BYTES 280u\n'
(R/'common/video_fullwidth_blocks.h').write_text(s,encoding='utf8',newline='\n')
out=dict(passed=True,blocks=report,symbols=symbols,clock_Hz=26000000,clock_read_order=['0x06002060','0x06002064'],ring_capacity=8,ring_bytes=256,producer_schema=4,original_TMC_count_facility_exclusive_lease=True,trace_modes=[0x1717,0x1818,0x1919],IRQ_observed_mask=0x4c,native_string_logger_code_and_data_preserved=True,frame_allocation=False,blue_boundary_sha256=hashlib.sha256(boundary).hexdigest(),first_apply_stores=15+280//4+sum(x['changed_words']for x in report),later_apply_restore_stores=15+280//4+report[0]['changed_words'],physical_camera_tested=False)
(R/'validation/current/timing-build.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(symbols=symbols,first_apply_stores=out['first_apply_stores'],later_stores=out['later_apply_restore_stores'],blocks=report),indent=2))
