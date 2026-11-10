#!/usr/bin/env python3
"""Link front and post stages into one static ARM owner process."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
p=argparse.ArgumentParser();p.add_argument('--cold-init-test',action='store_true');p.add_argument('--zig',type=Path,required=True);p.add_argument('--cross-runtime',type=Path,required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[2];b=r/('core/build-init-pipeline' if a.cold_init_test else 'core/build-pipeline');b.mkdir(exist_ok=True);zig=a.zig.resolve();cross=a.cross_runtime.resolve();sysroot=cross/'arm-linux-musleabi'
env={**os.environ,'TMPDIR':str(b.resolve())};lib=zig.parent/'lib';objects=[];sources=[]
for rel,defines in [('core/src/nxl-native-front.c',['-Dmain=nxl_front_entry','-DNXL_PIPELINE_EMBEDDED=1','-DNXL_FAST_PIPELINE=1']),('post/src/hw-post.c',['-Dmain=nxl_post_entry']),('core/src/nxl-pipeline.c',[])]:
 defines=defines+(['-DNXL_COLD_INIT_TEST=1'] if a.cold_init_test else []);source=(r/rel).resolve();obj=(b/(source.stem+'.o')).resolve();objects.append(obj);sources.append(dict(file=rel,sha256=hashlib.sha256(source.read_bytes()).hexdigest(),defines=defines))
 command=[str(zig),'clang','-target','arm-linux-musleabi','-mcpu=cortex-a9','-mfloat-abi=soft','-marm','-nostdinc','-O2','-fno-vectorize','-fno-slp-vectorize','-fno-stack-protector','-fno-pic','-fno-pie','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-isystem',str(lib/'include'),'-isystem',str(sysroot/'include'),*defines,'-c',str(source),'-o',str(obj)]
 subprocess.run(command,env=env,check=True)
runtime={n:sysroot/'lib'/n for n in ['crt1.o','crti.o','crtn.o','libc.a']};runtime['libgcc.a']=next((cross/'lib/gcc/arm-linux-musleabi').glob('*/libgcc.a'))
target=(r/('base/nx-lut-lite/bin/nxl-init-pipeline.arm' if a.cold_init_test else 'core/bin/nxl-demo-front.arm')).resolve();target.parent.mkdir(exist_ok=True)
subprocess.run([str(zig),'ld.lld','-m','armelf_linux_eabi','-static','-e','_start','-z','noexecstack','--build-id=none','--strip-all','-o',str(target),str(runtime['crt1.o']),str(runtime['crti.o']),*[str(x) for x in objects],'--start-group',str(runtime['libc.a']),str(runtime['libgcc.a']),'--end-group',str(runtime['crtn.o'])],check=True,env=env)
report=dict(format='NX_SINGLE_PROCESS_PIPELINE_BUILD1',sources=sources,bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest(),hardware_tested=False)
(r/'validation/r8e').mkdir(parents=True,exist_ok=True)
(r/('validation/r8e/pipeline-build.json' if a.cold_init_test else 'validation/r8e/pipeline-production-build.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print('Built one-process front/LUT/JPEG entry',report['bytes'],'bytes')
