"""Build the isolated cold-configuration trial and integrated passive recorder.
Requires the exact original firmware inputs for guard extraction; none is
distributed with this source. Does not build/replace production binaries.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--zig',required=True,type=Path);p.add_argument('--cross-runtime',required=True,type=Path);p.add_argument('--firmware-dir',required=True,type=Path);p.add_argument('--elftools-path',required=True,type=Path);a=p.parse_args()
R=Path(__file__).resolve().parents[1];b=R/'build-trial-helpers';b.mkdir(exist_ok=True)
zig=a.zig.resolve();cross=a.cross_runtime.resolve();sysroot=cross/'arm-linux-musleabi';env={**os.environ,'TMPDIR':str(b.resolve())}
kernel=(a.firmware_dir/'kernel.Image').read_bytes();udd=(a.firmware_dir/'libudd5.so').read_bytes()
assert hashlib.sha256(kernel).hexdigest()=='25bdca5cde4a5c10710d02dbaa904c62cd77b0b4562b8bf4d73c675f50bdb587'
def words(blob,at):return '{'+','.join('0x%08xu'%x for x in struct.unpack_from('<8I',blob,at))+'}'
header='/* Generated original ARM identity guards, never executed/copied as a stub. */\nconst uint32_t owner_selector_guards[3][8]={'+','.join(words(kernel,x-0xc0008000)for x in [0xc02cd37c,0xc02cdb18,0xc02cdb38])+'};\nstatic const struct alloc_guard owner_ioctl_guard[]={{0x87f4u,'+words(udd,0x87f4)+'}};\n'
(R/'experiments/owned-ep-guards.h').write_text(header,encoding='utf8',newline='\n')
runtime={n:sysroot/'lib'/n for n in ['crt1.o','crti.o','crtn.o','libc.a']};runtime['libgcc.a']=next((cross/'lib/gcc/arm-linux-musleabi').glob('*/libgcc.a'));reports=[]
for rel,name in [('experiments/owned-ep.c','nxl-owned-ep.arm'),('lens/nxl-handoff-watch.c','nxl-lens-watch.arm')]:
 source=R/rel;obj=b/(name+'.o');target=R/'base/nx-lut-lite/bin'/name
 command=[str(zig),'clang','-target','arm-linux-musleabi','-mcpu=cortex-a9','-mfloat-abi=soft','-marm','-nostdinc','-O2','-fno-vectorize','-fno-slp-vectorize','-fno-stack-protector','-fno-pic','-fno-pie','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-isystem',str(zig.parent/'lib/include'),'-isystem',str(sysroot/'include'),'-c',str(source.resolve()),'-o',str(obj.resolve())]
 subprocess.run(command,env=env,check=True)
 subprocess.run([str(zig),'ld.lld','-m','armelf_linux_eabi','-static','-e','_start','-z','noexecstack','--build-id=none','--strip-all','-o',str(target.resolve()),str(runtime['crt1.o']),str(runtime['crti.o']),str(obj.resolve()),'--start-group',str(runtime['libc.a']),str(runtime['libgcc.a']),'--end-group',str(runtime['crtn.o'])],env=env,check=True)
 reports.append(dict(source=rel,binary=name,bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
 print('Built',name,target.stat().st_size,'bytes',flush=True)
for tool in ['build_rtos.py','build_pipeline.py']:
 args=['--zig',str(zig),'--cold-init-test']
 args+=['--elftools-path',str(a.elftools_path.resolve())] if tool=='build_rtos.py' else ['--cross-runtime',str(cross)]
 subprocess.run([sys.executable,'-X','utf8',str(R/'core/tools'/tool),*args],check=True)
(R/'validation/r8e/trial-build.json').write_text(json.dumps(dict(passed=True,hardware_tested=False,helpers=reports),indent=2)+'\n',encoding='utf8',newline='\n')
