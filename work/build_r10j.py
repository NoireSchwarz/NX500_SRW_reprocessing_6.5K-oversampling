from pathlib import Path
import sys,subprocess,struct,re,json,hashlib
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';B=W/'r10h-completion-ab';V=R/'validation/current'
p=R/'common/video_counter_profile.h';s=p.read_text('utf8');rom=(W/'firmware/P7.bin').read_bytes()
for a in [0x8049a4b0,0x804b6894,0x804b6b98,0x8003677c,0x80036890,0x80036bc0]:
 if f'0x{a:08x}u'not in s:
  row=' {0x%08xu,{'%a+','.join('0x%08xu'%z for z in struct.unpack_from('<8I',rom,a-0x80000000))+'}},\n'
  pos=s.index('};',s.index('static const struct vp_rom_guard'))
  s=s[:pos]+row+s[pos:]
p.write_text(s,encoding='utf8',newline='\n')
cmd=[sys.executable,'-X','utf8',str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross')]
if sys.argv[1]=='probe':subprocess.run(cmd+['--source',str(R/'core/src/nxl-video-probe.c'),'--output',str(R/'base/nx-lut-lite/bin/nxl-video-probe.arm')],check=True)
elif sys.argv[1]=='fullwidth':subprocess.run(cmd+['--source',str(R/'core/src/nxl-video-fullwidth.c'),'--output',str(R/'base/nx-lut-lite/bin/nxl-video-fullwidth.arm')],check=True)
elif sys.argv[1]=='menu':subprocess.run([sys.executable,'-X','utf8',str(R/'base/nx-lut-lite/tools/build_zig.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--menu-only','--elftools-path',str(W/'tooling/python')],check=True)
else:raise ValueError(sys.argv[1])
