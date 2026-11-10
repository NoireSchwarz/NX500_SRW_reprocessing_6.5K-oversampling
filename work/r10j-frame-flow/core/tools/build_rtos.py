"""Build our own relocatable ARM block; reject absolute internal relocations."""
from pathlib import Path
import argparse,subprocess,os,json,hashlib,sys
p=argparse.ArgumentParser();p.add_argument('--cold-init-test',action='store_true');p.add_argument('--zig',type=Path,required=True);p.add_argument('--elftools-path',type=Path,required=True);a=p.parse_args()
s=Path(__file__).resolve().parents[1];b=s/('build-init-trial' if a.cold_init_test else 'build');b.mkdir(exist_ok=True);env=dict(os.environ,TMPDIR=str(b.resolve()));zig=str(a.zig.resolve())
sys.path.insert(0,str(a.elftools_path.resolve()));from elftools.elf.elffile import ELFFile
obj=b/'rtos_front.o';elf=b/'rtos_front.elf'
subprocess.run([zig,'clang','-target','armv7-none-eabi','-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-isystem',str(a.zig.resolve().parent/'lib/include'),'-DNXL_FAST_PIPELINE=1',*(['-DNXL_COLD_INIT_TEST=1'] if a.cold_init_test else []),'-ffreestanding','-fpie','-fvisibility=hidden','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-exceptions','-Os','-Wall','-Wextra','-Werror','-I',str(s/'src'),'-c',str(s/'src/rtos_front.c'),'-o',str(obj)],check=True,env=env)
with obj.open('rb') as f:
 e=ELFFile(f)
 for sec in e.iter_sections():
  if sec['sh_type'] in ['SHT_REL','SHT_RELA']:
   for r in sec.iter_relocations():assert r['r_info_type'] in [3,28,29,42,96],(sec.name,r['r_info_type'])
script=b/'owned.ld';script.write_text('ENTRY(nxl_entry)\nSECTIONS { . = 0; .text : { *(.text.entry) *(.text) *(.text.unlikely*) *(.rodata*) } .ARM.exidx : { *(.ARM.exidx*) } ASSERT(. < 0x3000,"primary code too large") . = 0x3000; .context : { *(.context) } . = 0x3100; .param : { *(.text.param*) } ASSERT(. <= 0x4000,"param code reaches result") . = 0x5400; .gamma : { *(.text.gamma*) } ASSERT(. <= 0x6000,"Gamma reaches WB") . = 0x6000; .wb : { *(.text.wb*) } ASSERT(. <= 0x7000,"WB code reaches packet area") . = 0xc000; .extra : { *(.text.cc*) } ASSERT(. <= 0xe000,"extra code reaches batch listeners") . = 0xe800; .stop : { *(.text.stop*) } ASSERT(. <= 0x10000,"stop code reaches Bayer") /DISCARD/ : { *(.comment*) *(.ARM.attributes*) } }')
subprocess.run([zig,'ld.lld','-m','armelf','-static','-T',str(script),'--no-undefined','-o',str(elf),str(obj)],check=True,env=env)
with elf.open('rb') as f:
 e=ELFFile(f);symbols={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()};assert symbols['nxl_entry']==0 and symbols['ctx']==0x3000
 raw=bytearray(max(sec['sh_addr']+sec['sh_size'] for sec in e.iter_sections() if sec['sh_flags']&2))
 assert len(raw)<=0x10000
 for sec in e.iter_sections():
  if sec['sh_flags']&2:
   assert sec['sh_addr']+sec['sh_size']<=len(raw);raw[sec['sh_addr']:sec['sh_addr']+sec['sh_size']]=sec.data()
 assert not any(sec['sh_type'] in ['SHT_REL','SHT_RELA'] for sec in e.iter_sections())
(s/('bin/rtos_front_trial.bin' if a.cold_init_test else 'bin/rtos_front.bin')).write_bytes(raw)
(s/('src/rtos_front_trial_blob.h' if a.cold_init_test else 'src/rtos_front_blob.h')).write_text('/* Generated from our own freestanding ARM code. */\n#define P39_IRQ_OFFSET %du\n#define P39_CODE_BYTES %du\nstatic const unsigned char p39_hook_code[%d]={\n'%(symbols['nxl_irq'],len(raw),len(raw))+''.join(' '+','.join('0x%02x'%x for x in raw[i:i+16])+',\n' for i in range(0,len(raw),16))+'};\n')
(s/('results/rtos-init-build.json' if a.cold_init_test else 'results/rtos-build.json')).write_text(json.dumps(dict(bytes=len(raw),callback_offset=symbols['nxl_irq'],binary_sha256=hashlib.sha256(raw).hexdigest(),source_sha256=hashlib.sha256((s/'src/rtos_front.c').read_bytes()).hexdigest(),hardware_tested=False),indent=2)+'\n');print('Built',len(raw),'own ARM bytes; callback offset',symbols['nxl_irq'])
