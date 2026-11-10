#!/usr/bin/env python3
"""Link the small worker against pinned original glibc/UDD, never bundle DSOs."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path,required=True);p.add_argument('--udd',type=Path,required=True);p.add_argument('--libc',type=Path,required=True);a=p.parse_args()
assert hashlib.sha256(a.udd.read_bytes()).hexdigest()=='8c56bea30a591fbfb8e9e86c237504f5e28f464f74c18cffc8bbaa7f717b0ea1'
assert hashlib.sha256(a.libc.read_bytes()).hexdigest()=='198a7b52e68df955de2573557644720e76580aae489e3785bb9afd80ad6f2e9c'
r=Path(__file__).resolve().parents[1];build=r/'build-bitb';build.mkdir(exist_ok=True);z=a.zig.resolve();lib=z.parent/'lib';env={**os.environ,'TMPDIR':str(build),'ZIG_GLOBAL_CACHE_DIR':str(build/'zig-cache')}
cmd=[str(z),'clang','-target','arm-linux-gnueabi','-mcpu=cortex-a9','-mfloat-abi=soft','-marm','-nostdinc','-O2','-DNXL_FAST_PIPELINE=1','-fno-pie','-fno-pic','-fno-stack-protector','-fno-vectorize','-fno-slp-vectorize','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-const-variable','-Wno-misleading-indentation']
for name in ['include','libc/include/arm-linux-gnueabi','libc/include/generic-glibc','libc/include/arm-linux-any','libc/include/any-linux-any']:cmd+=['-I',str(lib/name)]
for name in ['nxl-bitbworker.c','bitb_start.S']:subprocess.run(cmd+['-c',str(r/'src'/name),'-o',str(build/(name+'.o'))],env=env,check=True)
out=r/'bin/nxl-bitbworker.arm';out.parent.mkdir(exist_ok=True)
subprocess.run([str(z),'ld.lld','-m','armelf_linux_eabi','-e','_start','-dynamic-linker','/lib/ld-linux.so.3','--allow-shlib-undefined','--no-as-needed','-z','noexecstack','--build-id=none','--strip-all','-o',str(out),str(build/'bitb_start.S.o'),str(build/'nxl-bitbworker.c.o'),str(a.udd.resolve()),str(a.libc.resolve())],check=True)
b=out.read_bytes();h=2166136261
for c in b:h=((h^c)*16777619)&0xffffffff
(r/'src/bitb_payload.h').write_text('/* SPDX-License-Identifier: AGPL-3.0-only */\n#define BITB_WORKER_BYTES %du\n#define BITB_WORKER_FNV 0x%08xu\n'%(len(b),h))
(build/'worker_manifest.json').write_text(json.dumps({'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'fnv':hex(h),'system_dependencies':['libudd5.so','libc.so.6'],'original_DSOs_bundled':False},indent=2)+'\n')
print('Built original-ABI native JPEG worker',len(b),'bytes')
