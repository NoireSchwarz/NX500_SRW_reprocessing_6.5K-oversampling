from pathlib import Path
import hashlib,json,os,subprocess,sys
A=Path(__file__).resolve().parent
W=Path(r'D:\NX500模组开发\formal-0.4.0-rc1-work')
R=W.parent/'repository/nx500';SRC=R/'work/r10k-ep-flow'
B=W.parent/'r11q-vlog-work/stage';T=W/'build-tree';S=W/'stage';M=S/'nx-lut-demo/nx-lut-lite'
OLD=Path(r'C:\Users\qwer0\Documents\Codex\2026-11-05\metol-nx500-mod-lut-srw-jpeg-3\outputs\NX_LUT_Lite_0.4.0_demo51_R8f_LUT_Params_Mix_reserve_full.zip')
PY=R/'work/tooling/python';ZIG=PY/'ziglang/zig.exe';CROSS=R/'work/tooling/arm-linux-musleabi-cross'
ENV=dict(os.environ,PYTHONUTF8='1',PYTHONDONTWRITEBYTECODE='1',ZIG_GLOBAL_CACHE_DIR=str(W/'zig-global'),ZIG_LOCAL_CACHE_DIR=str(W/'zig-local'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def put(p,x):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,ensure_ascii=False,indent=2,allow_nan=False)+'\n','utf8',newline='\n')
def patch(p,old,new,count=1):
 s=p.read_text('utf8');assert s.count(old)==count,(str(p),old,s.count(old));p.write_text(s.replace(old,new),'utf8',newline='\n')
def run(label,args):
 p=subprocess.run([sys.executable,'-B',*map(str,args)],env=ENV,capture_output=True,text=True,encoding='utf8')
 (W/'logs'/(label+'.log')).write_text(p.stdout+p.stderr,'utf8');assert p.returncode==0,(label,p.stdout[-1000:],p.stderr[-3000:]);print('PASS',label,flush=True)
def host(label,source,out):run(label,[T/'core/tools/build_front.py','--zig',ZIG,'--cross-runtime',CROSS,'--source',source,'--output',out])
