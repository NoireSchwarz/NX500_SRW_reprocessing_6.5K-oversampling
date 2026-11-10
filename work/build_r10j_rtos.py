from pathlib import Path
import os,subprocess,hashlib,json,sys
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';B=R/'core/build-video';B.mkdir(parents=True,exist_ok=True)
Z=W/'tooling/python/ziglang/zig.exe';sys.path.insert(0,str(W/'tooling/python'))
from elftools.elf.elffile import ELFFile
env=dict(os.environ,TMPDIR=str(B.resolve()));obj=B/'video.o';elf=B/'video.elf';src=R/'core/src/rtos_video_fullwidth.c'
cmd=[str(Z),'clang','-target','armv7-none-eabi','-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-isystem',str(Z.parent/'lib/include'),'-DVFW_BARE=1','-ffreestanding','-fpie','-fvisibility=hidden','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-exceptions','-Os','-Wall','-Wextra','-Werror','-Wno-unused-function','-c',str(src),'-o',str(obj)]
subprocess.run(cmd,env=env,check=True)
with obj.open('rb')as f:
 e=ELFFile(f)
 for sec in e.iter_sections():
  if sec['sh_type']in['SHT_REL','SHT_RELA']:
   for rel in sec.iter_relocations():assert rel['r_info_type']in[3,28,29,42,96],(sec.name,rel['r_info_type'])
ld=B/'video.ld';ld.write_text('ENTRY(nxl_video_entry)\nSECTIONS { . = 0; .text : { *(.text.entry) *(.text*) *(.rodata*) } .ARM.exidx : { *(.ARM.exidx*) } ASSERT(. < 0x4000,"code reaches command") . = 0x4000; .context : { *(.context) } ASSERT(. == 0x4040,"context size") /DISCARD/ : { *(.comment*) *(.ARM.attributes*) } }\n')
subprocess.run([str(Z),'ld.lld','-m','armelf','-static','-T',str(ld),'--no-undefined','-o',str(elf),str(obj)],env=env,check=True)
with elf.open('rb')as f:
 e=ELFFile(f);symbols={s.name:s['st_value']for s in e.get_section_by_name('.symtab').iter_symbols()};assert symbols['nxl_video_entry']==0 and symbols['ctx']==0x4000
 raw=bytearray(0x4040)
 for sec in e.iter_sections():
  if sec['sh_flags']&2:raw[sec['sh_addr']:sec['sh_addr']+sec['sh_size']]=sec.data()
 assert not any(sec['sh_type']in['SHT_REL','SHT_RELA']for sec in e.iter_sections())
(R/'core/bin/rtos_video_fullwidth.bin').write_bytes(raw)
(R/'core/src/rtos_video_fullwidth_blob.h').write_text('/* Generated from our freestanding ARM block. */\nstatic const unsigned char vfw_code[%d]={\n'%len(raw)+''.join(' '+','.join('0x%02x'%x for x in raw[i:i+16])+',\n'for i in range(0,len(raw),16))+'};\n',encoding='utf8')
report=dict(passed=True,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),headers_sha256={n:hashlib.sha256((R/'common'/n).read_bytes()).hexdigest()for n in ['video_fullwidth_contract.h','video_fullwidth_blocks.h']},relocatable=True,guarded_replacement_words=sum(x['changed_words']for x in json.loads((R/'validation/current/timing-build.json').read_text())['blocks']),legacy_LCAC_instruction_words=2,boot_resident_fixed_timing_code=True,physical_camera_tested=False)
(R/'validation/current/video-rtos-build.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(report)
