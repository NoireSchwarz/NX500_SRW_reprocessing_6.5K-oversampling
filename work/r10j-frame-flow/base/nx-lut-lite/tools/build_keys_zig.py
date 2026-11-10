#!/usr/bin/env python3
"""Build only the ARM listener; keep menu and native pixel tools untouched."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path,required=True);a=p.parse_args()
zig=a.zig.resolve();lib=zig.parent/'lib';build=R/'build-keys-r4';build.mkdir(exist_ok=True)
os.environ['TMPDIR']=str(build.resolve())
flags=[str(zig),'clang','-target','arm-linux-gnueabi','-mcpu=cortex-a9','-mfloat-abi=soft','-marm','-O0','-fno-builtin','-fno-stack-protector','-fno-pic','-fno-pie','-fomit-frame-pointer','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32','-Wall','-Wextra']
for inc in [lib/'include',R/'vendor']+[lib/'libc/include'/s for s in ['arm-linux-gnueabi','generic-glibc','arm-linux-any','any-linux-any']]:flags+=['-I',str(inc)]
def run(args):subprocess.run([str(x) for x in args],check=True)
def obj(name):
    out=build/(name+'.o');run(flags+['-c',R/'src'/name,'-o',out]);return out
keys=obj('keys.c');crt=obj('crt0.S');division=obj('uidiv.c')
unresolved=subprocess.check_output(['nm','-u',str(keys),str(crt)],text=True)
imports=set(re.findall(r'\bU\s+(\w+)',unresolved)) - {'main','__aeabi_uidiv','__aeabi_idiv','__aeabi_idivmod'}
assert not any(s.startswith('X') for s in imports)
stub=build/'libc_imports.c';stub.write_text('\n'.join('void *stderr;' if s=='stderr' else 'void '+s+'(void) {}' for s in sorted(imports)))
stubobj=build/'libc_imports.o';run(flags+['-fPIC','-c',stub,'-o',stubobj])
cstub=build/'libc.so.6';run([zig,'ld.lld','-shared','-soname','libc.so.6','-o',cstub,stubobj])
output=R/'bin/nxl-keys';output.parent.mkdir(exist_ok=True)
run([zig,'ld.lld','-m','armelf_linux_eabi','-e','_start','--strip-all','--dynamic-linker','/lib/ld-linux.so.3','-z','noexecstack','-o',output,crt,keys,division,cstub])
print(json.dumps({'binary':str(output),'bytes':output.stat().st_size,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'no_menu_rebuild':True}))
