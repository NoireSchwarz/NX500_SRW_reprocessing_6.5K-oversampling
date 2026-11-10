#!/usr/bin/env python3
"""Build the independent static native allocation test driver; no DSOs shipped."""
import argparse
import os
from pathlib import Path
import subprocess
import hashlib
import json
import struct
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',type=Path,required=True);p.add_argument('--runtime-cache',type=Path,help='restricted /proc fallback: existing matching Zig ARM musl runtime cache');a=p.parse_args()
root=Path(__file__).resolve().parents[1];build=root/'build-alloc';build.mkdir(exist_ok=True)
env=dict(os.environ,TMPDIR=str(build),ZIG_GLOBAL_CACHE_DIR=str(build/'zig-cache'))
zig=a.zig.resolve();target=root/'bin/nxl-allocprobe.arm'
if not a.runtime_cache:
 subprocess.run([str(zig),'cc','-target','arm-linux-musleabi','-mcpu=cortex_a9-neon-vfp3','-mfloat-abi=soft','-fno-vectorize','-fno-slp-vectorize','-O2','-static','-no-pie','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wl,--build-id=none',str(root/'src/nxl-allocprobe.c'),'-o',str(target)],env=env,check=True)
else:
 def arm_elf(blob):return blob[:4]==b'\x7fELF' and blob[4]==1 and struct.unpack_from('<H',blob,18)[0]==40 and not struct.unpack_from('<I',blob,36)[0]&0x400
 def matches(path):
  data=path.read_bytes()
  if data.startswith(b'!<arch>\n'):
   pos=8
   while pos+60<=len(data):
    n=int(data[pos+48:pos+58]);blob=data[pos+60:pos+60+n];pos+=60+n+(n%2)
    if blob[:4]==b'\x7fELF':return arm_elf(blob)
   return False
  return arm_elf(data)
 runtime={}
 for name in ['crt1.o','crti.o','crtn.o','libc.a','libcompiler_rt.a']:
  found=[f.resolve() for f in sorted(a.runtime_cache.rglob(name)) if matches(f)]
  if not found:raise ValueError('Missing ARM soft-float runtime '+name)
  runtime[name]=found[0]
 lib=zig.parent/'lib';includes=[lib/'include']+[lib/'libc/include'/n for n in ['arm-linux-musl','generic-musl','arm-linux-any','any-linux-any']]
 obj=build/'allocprobe.o';cmd=[str(zig),'clang','-target','arm-linux-musleabi','-mcpu=cortex-a9','-mfloat-abi=soft','-marm','-nostdinc','-O2','-fno-vectorize','-fno-slp-vectorize','-fno-stack-protector','-fno-pic','-fno-pie','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function']
 for inc in includes:cmd+=['-I',str(inc)]
 subprocess.run(cmd+['-c',str(root/'src/nxl-allocprobe.c'),'-o',str(obj)],env=env,check=True)
 subprocess.run([str(zig),'ld.lld','-m','armelf_linux_eabi','-static','-e','_start','-z','noexecstack','--build-id=none','--strip-all','-o',str(target),str(runtime['crt1.o']),str(runtime['crti.o']),str(obj),'--start-group',str(runtime['libc.a']),str(runtime['libcompiler_rt.a']),'--end-group',str(runtime['crtn.o'])],env=env,check=True)
 (build/'runtime_manifest.json').write_text(json.dumps({name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in runtime.items()},indent=2)+'\n')
print('Built static ARM native allocator experiment; two transient code breakpoints.')
