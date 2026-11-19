#!/usr/bin/env python3
"""Build the independent R11l Linux reader; never generate RTOS code."""
from pathlib import Path
import argparse,subprocess,os,hashlib,json
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',required=True,type=Path);p.add_argument('--cross-runtime',required=True,type=Path);p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
 root=a.root.resolve();zig=a.zig.resolve();cross=a.cross_runtime.resolve();sysroot=cross/'arm-linux-musleabi';out=a.out.resolve();out.parent.mkdir(parents=True,exist_ok=True);obj=out.with_suffix('.o');env=dict(os.environ,TMPDIR=str(out.parent))
 command=[zig,'clang','-target','arm-linux-musleabi','-mcpu=cortex-a9','-mfloat-abi=soft','-marm','-nostdinc','-O2','-fno-vectorize','-fno-slp-vectorize','-fno-stack-protector','-fno-pic','-fno-pie','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-isystem',zig.parent/'lib/include','-isystem',sysroot/'include','-c',root/'core/src/nxl-color-observe.c','-o',obj]
 subprocess.run(list(map(str,command)),check=True,env=env);libgcc=next((cross/'lib/gcc/arm-linux-musleabi').glob('*/libgcc.a'))
 command=[zig,'ld.lld','-m','armelf_linux_eabi','-static','-e','_start','-z','noexecstack','--build-id=none','--strip-all','-o',out,sysroot/'lib/crt1.o',sysroot/'lib/crti.o',obj,'--start-group',sysroot/'lib/libc.a',libgcc,'--end-group',sysroot/'lib/crtn.o']
 subprocess.run(list(map(str,command)),check=True,env=env);print(json.dumps(dict(release='R11l',file=str(out),bytes=out.stat().st_size,sha256=hashlib.sha256(out.read_bytes()).hexdigest(),RTOS_changed=False,physical_camera_tested=False),indent=2))
if __name__=='__main__':main()
