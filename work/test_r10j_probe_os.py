"""Actual production ARM collector; only Linux FS/memory/time endpoints modeled.
No camera invocation and no claims about measured hardware CPU overhead.
"""
from pathlib import Path,PurePosixPath
import hashlib,json,os,stat,struct,sys,uuid
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';V=R/'validation/current/probe-os';V.mkdir(parents=True,exist_ok=True)
sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
from unicorn.arm_const import *
BIN=R/'base/nx-lut-lite/bin/nxl-video-probe.arm';ROOT='/tmp/nx-lut-lite';RAM='/dev/shm/nx-lut-lite';REPORT='/mnt/mmc/NXDEMO51/VIDEO_STAGE_test';USB='/sys/class/extcon/USB/state';CAM='/usr/apps/com.samsung.di-camera-app/bin/di-camera-app'
def put(f,n,b):p=f/n.lstrip('/');p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b if isinstance(b,bytes)else b.encode());return p
def procstat(pid,ticks=5678):return f'{pid} (di-camera-app) S '+' '.join(['0']*18+[str(ticks)]+['0']*8)+'\n'
def fixture():
 f=V/uuid.uuid4().hex[:10];f.mkdir()
 for n in [ROOT,RAM,REPORT,'/proc/247/fd','/proc/123/fd']: (f/n.lstrip('/')).mkdir(parents=True,exist_ok=True)
 put(f,'/etc/version.info','1.12\nNX500\n');put(f,USB,'0\n');put(f,'/proc/247/stat',procstat(247));put(f,'/proc/123/stat',procstat(123,9012))
 maps=''
 for i,n in enumerate(['libmmfmodules.so','libmmfcore.so','libmmfosal.so']):
  put(f,'/usr/lib/'+n,(W/'r9v-framedrop-evidence/firmware'/n).read_bytes());lo=0x10000000+i*0x1000000;maps+=f'{lo:08x}-{lo+0x70000:08x} r-xp 00000000 00:00 1 /usr/lib/{n}\n'
 put(f,'/proc/247/maps',maps);put(f,'/proc/247/mem',b'');put(f,'/dev/mem',b'');return f
def header(f):
 text=(f/(RAM+'/video-probe.current').lstrip('/')).read_text();p=text.splitlines()[3];return p,bytearray((f/p.lstrip('/')).read_bytes())
class Model(PostFileModel):
 generation=0
 def __init__(self,f,args,action='stop',after=5,**opts):
  self.maps={};self.sleeps=0;self.action=action;self.after=after;self.wall=10_000_000_000;self.live=opts.get('live',False);self.lease_busy=opts.get('lease_busy',False);self.fail_write=opts.get('fail_write','');self.fail_sync=opts.get('fail_sync','');self.ramfs=opts.get('ramfs',True);self.busy_camera=opts.get('busy_camera',False);self.target_opens=[];self.sd_writes=[];self.hot_sd=[];self.did_stop=False;self.cpufraction=opts.get('cpufraction',0.0001);self.fail_ready=opts.get('fail_ready',False);self.flip_usb_on_publish=opts.get('flip_usb_on_publish',False)
  super().__init__(BIN,args,f);Model.generation+=1;self.wall+=Model.generation*100_000_000_000
  if self.busy_camera:put(f,'/proc/247/fd/9',b'');put(f,'/proc/247/fdinfo/9','flags:\t0100001\n')
 def atpath(self,fd,p):
  if p.startswith('/'):return p
  assert '..'not in PurePosixPath(p).parts;return self.fds[fd]['path'].rstrip('/')+'/'+p
 def syncmap(self,p=None):
  for at,m in self.maps.items():
   if m['path'].startswith(RAM+'/')and m['prot']&2 and(p is None or m['path']==p):
    q=self.fixture_path(m['path']);raw=bytes(self.uc.mem_read(at,m['length']));old=q.read_bytes();q.write_bytes(old[:m['off']]+raw+old[m['off']+len(raw):])
 def model_stat(self,p):
  q=self.fixture_path(p)
  if not q.exists():return None
  s=q.stat();b=bytearray(104);mode=(stat.S_IFDIR|0o700)if q.is_dir()else(stat.S_IFREG|0o600)
  struct.pack_into('<Q',b,0,1);struct.pack_into('<I',b,12,1);struct.pack_into('<IIII',b,16,mode,1,0,0);struct.pack_into('<q',b,48,s.st_size);struct.pack_into('<I',b,56,4096);struct.pack_into('<Q',b,96,1);return bytes(b)
 def openfile(self,p,flags):
  q=self.fixture_path(p);access=flags&3
  if p in ['/dev/mem','/proc/247/mem']:
   assert access==0 and not flags&0x200;self.target_opens.append((p,flags))
  if access:assert p.startswith(ROOT+'/')or p.startswith(RAM+'/')or p.startswith('/mnt/mmc/NXDEMO51/VIDEO_STAGE_')or p=='/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor',('unexpected target write',p)
  if p.endswith('/READY')and self.fail_ready:return-28
  if q.exists()and flags&0x80:return-17
  if not q.exists():
   if not flags&0x40:return-2
   q.write_bytes(b'')
  if flags&0x200:q.write_bytes(b'')
  self.serial+=1;self.fds[self.serial]=dict(path=p,host=q,data=q.read_bytes()if q.is_file()else b'',at=0,flags=flags);return self.serial
 def event(self):
  self.sleeps+=1
  if self.sleeps!=self.after:return
  if self.action=='stop':
   for at,m in self.maps.items():
    if m['path'].startswith(RAM+'/')and m['prot']&2:
     cookie=struct.unpack('<I',self.uc.mem_read(at+100,4))[0];self.uc.mem_write(at+112,struct.pack('<I',cookie^0xa18c39f7));self.uc.mem_write(at+120,struct.pack('<I',cookie^0x75e38b91));self.did_stop=True;break
  elif self.action=='usb':put(self.fixture,USB,'1\n')
  elif self.action=='unknown_usb':put(self.fixture,USB,'INVALID\n')
  elif self.action=='camera_gone':put(self.fixture,'/proc/247/stat',procstat(247,9999))
  elif self.action=='limit':
   for at,m in self.maps.items():
    if m['path'].startswith(RAM+'/')and m['length']>4096:self.uc.mem_write(at+24,struct.pack('<I',5999))
   self.action='usb';self.after+=2
 def interrupt(self,uc,num,data):
  n=uc.reg_read(UC_ARM_REG_R7);a=[uc.reg_read(x)for x in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5]]
  own=n in [3,4,5,6,39,60,85,93,99,118,143,146,148,180,192,194,195,196,197,266,263,265,322,327,328,329,332,91,217,369]
  if not own:return super().interrupt(uc,num,data)
  self.calls[n]=self.calls.get(n,0)+1
  try:
   if n==369:
    assert a[1]==7 and not a[2];uc.mem_write(a[3],struct.pack('<QQ',1024,1024));self.ret(0)
   elif n==60:self.ret(0)
   elif n in [5,322]:self.ret(self.openfile(self.string(a[0])if n==5 else self.atpath(a[0],self.string(a[1])),a[1]if n==5 else a[2]))
   elif n==6:
    if a[0]not in self.fds:self.ret(-9)
    else:self.fds.pop(a[0]);self.ret(0)
   elif n in [195,196,197,327]:
    p=self.fds[a[0]]['path']if n==197 else self.atpath(a[0],self.string(a[1]))if n==327 else self.string(a[0]);b=self.model_stat(p);ptr=a[2]if n==327 else a[1]
    if b is None:self.ret(-2)
    else:uc.mem_write(ptr,b);self.ret(0)
   elif n in [99,266]:
    p=self.string(a[0]);assert p=='/dev/shm';b=bytearray(84 if n==266 else 64);struct.pack_into('<I',b,0,0x01021994 if self.ramfs else 0xef53);uc.mem_write(a[2]if n==266 else a[1],bytes(b));self.ret(0)
   elif n==39:self.fixture_path(self.string(a[0])).mkdir();self.ret(0)
   elif n in [93,194]:
    f=self.fds[a[0]];assert f['path'].startswith(RAM+'/');size=a[2]if n==194 else a[1];assert size==6928384,(n,a);f['host'].write_bytes(bytes(size));f['data']=bytes(size);self.ret(0)
   elif n==3:
    f=self.fds[a[0]];b=f['host'].read_bytes()[f['at']:f['at']+a[2]];f['at']+=len(b)
    if b:uc.mem_write(a[1],b)
    self.ret(len(b))
   elif n in [85,332]:
    p=self.string(a[0])if n==85 else self.atpath(a[0],self.string(a[1]));target=CAM if p=='/proc/247/exe'else'/opt/usr/nx-lut-lite/bin/nxl-video-probe.arm'if p in ['/proc/123/exe','/proc/self/exe']else'/mnt/mmc/DCIM/100PHOTO/SAM_0001.MP4'if self.busy_camera and p=='/proc/247/fd/9'else None
    if target is None:self.ret(-2)
    else:raw=target.encode()[:a[2]if n==85 else a[3]];uc.mem_write(a[1]if n==85 else a[2],raw);self.ret(len(raw))
   elif n==143:
    p=self.fds[a[0]]['path'];assert p==ROOT+'/lens-evidence.fd'or p.startswith(RAM+'/');self.ret(-11 if p.endswith('/lens-evidence.fd')and(self.live or self.lease_busy)else 0)
   elif n==192:
    if a[3]&0x20:
     self.calls[n]-=1;return super().interrupt(uc,num,data)
    f=self.fds[a[4]];p=f['path'];length=(a[1]+4095)&~4095;at=self.map_next;self.map_next+=length+4096;uc.mem_map(at,length);offset=a[5]*4096
    if p=='/dev/mem':
     assert a[2]==1 and a[3]&1
     raw=bytearray(length)
     import re
     for addr,body in __import__('re').findall(r'\{0x([0-9a-f]+)u,\{([^}]+)\}',(R/'common/video_counter_profile.h').read_text()):
      addr=int(addr,16)
      if offset<=addr and addr+32<=offset+length:raw[addr-offset:addr-offset+32]=struct.pack('<8I',*[int(v.rstrip('u'),16)for v in body.split(',')])
     uc.mem_write(at,bytes(raw))
    else:
     assert p.startswith(RAM+'/');self.syncmap(p);raw=f['host'].read_bytes()[offset:offset+a[1]];uc.mem_write(at,raw)
    self.maps[at]=dict(path=p,off=offset,length=a[1],prot=a[2]);self.ret(at)
   elif n==91:
    if a[0]in self.maps:self.syncmap(self.maps[a[0]]['path']);self.maps.pop(a[0]);uc.mem_unmap(a[0],(a[1]+4095)&~4095)
    self.ret(0)
   elif n==180:
    f=self.fds[a[0]];assert f['path']=='/proc/247/mem';uc.mem_write(a[1],bytes(a[2]));self.ret(a[2])
   elif n==263:
    value=int((self.wall-10_000_000_000)*self.cpufraction)if a[0]==2 else self.wall;uc.mem_write(a[1],struct.pack('<ii',value//10**9,value%10**9));self.wall+=10000;self.ret(0)
   elif n==265:
    sec,ns=struct.unpack('<ii',uc.mem_read(a[2],8));self.wall=max(self.wall,sec*10**9+ns)if a[1]&1 else self.wall+sec*10**9+ns;self.event();self.ret(0)
   elif n in [4,146]:
    if a[0]not in self.fds:
     self.calls[n]-=1;return super().interrupt(uc,num,data)
    f=self.fds[a[0]];p=f['path'];chunks=[bytes(uc.mem_read(a[1],a[2]))]if n==4 else[bytes(uc.mem_read(ptr,size))for ptr,size in[struct.unpack('<II',uc.mem_read(a[1]+i*8,8))for i in range(a[2])]];raw=b''.join(chunks)
    if p.startswith('/mnt/mmc/'):
     self.sd_writes.append((p,len(raw),self.sleeps,self.did_stop))
     if self.sleeps and not self.did_stop:self.hot_sd.append(p)
    if self.fail_write and p.endswith(self.fail_write):self.ret(-28);return
    if p=='/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor':
     assert raw in [b'performance\n',b'interactive\n',b'ondemand\n',b'powersave\n',b'conservative\n'];f['host'].write_bytes(raw);f['at']+=len(raw)
    else:
     with f['host'].open('r+b')as out:out.seek(f['at']);out.write(raw);f['at']+=len(raw)
    self.ret(len(raw))
   elif n in [118,148]:self.ret(-5 if self.fail_sync and self.fail_sync in self.fds[a[0]]['path']else 0)
   elif n==328:self.fixture_path(self.atpath(a[0],self.string(a[1]))).unlink();self.ret(0)
   elif n==329:
    p=self.atpath(a[0],self.string(a[1]));q=self.atpath(a[2],self.string(a[3]));os.replace(self.fixture_path(p),self.fixture_path(q));self.ret(0)
   elif n==217:
    self.calls[n]-=1;return super().interrupt(uc,num,data)
  except FileNotFoundError:self.ret(-2)
  except FileExistsError:self.ret(-17)
 def run(self):
  result=super().run();self.syncmap();assert not self.hot_sd,self.hot_sd;assert not self.fds,self.fds;assert not any(n in self.calls for n in [26,37,117,181]),self.calls;return result
records=[]
def run(f,name,args,exit=0,reason=None,**kw):
 m=Model(f,args,**kw);result=m.run();assert result['exit']==exit,(name,result)
 if reason:assert reason in result['stdout'],(name,result)
 records.append(dict(name=name,**result,SD_writes=m.sd_writes,target_opens=m.target_opens));print('PASS',name,flush=True);return m,result
if __name__=='__main__':
 f=fixture();run(f,'normal-ready',['--ready'],reason='TRACE_READY_TO_ARM')
 m,_=run(f,'collect-stop-idle-publish',['--run',REPORT]);p,b=header(f);assert struct.unpack_from('<II',b,24)==(5,6);out=(f/(REPORT+'/counts.bin').lstrip('/')).read_bytes();assert len(out)==16384+5*1152 and(f/(REPORT+'/DONE').lstrip('/')).is_file();assert len(m.target_opens)==2
 run(f,'idempotent-saved-stop',['--stop'],reason='TRACE_SAVED');run(f,'saved-off-status',['--status'],reason='state=OFF')
 report2=REPORT+'2';(f/report2.lstrip('/')).mkdir();run(f,'repeat-start-after-save',['--run',report2]);p2,_=header(f);assert p!=p2 and not(f/p.lstrip('/')).exists()
 for why in ['usb','unknown_usb','camera_gone']:
  f=fixture();m,_=run(f,why+'-retains-RAM',['--run',REPORT],action=why);assert not(f/(REPORT+'/counts.bin').lstrip('/')).exists();p,b=header(f);assert struct.unpack_from('<I',b,24)[0]>0
  run(f,why+'-pending-status',['--status'],reason='state=PENDING');run(f,why+'-new-start-refused',['--run',REPORT+'2'],exit=1)
  if why=='usb':m,_=run(f,'USB-connected-pending-save-refused',['--stop'],exit=1,reason='TRACE_SAVE_PENDING_STOP_VIDEO');assert not m.sd_writes
  put(f,USB,'0\n');put(f,'/proc/247/stat',procstat(247,9999));run(f,why+'-recover-on-idle-current-camera',['--stop'],reason='TRACE_SAVED')
 f=fixture();run(f,'non-RAM-devshm-refused',['--run',REPORT],exit=1,ramfs=False);assert not list((f/RAM.lstrip('/')).glob('video-buffer-*'))
 f=fixture();run(f,'broad-recorder-lease-refused',['--run',REPORT],exit=1,lease_busy=True);assert not list((f/RAM.lstrip('/')).glob('video-buffer-*'))
 f=fixture();run(f,'READY-failure-no-stuck-registry',['--run',REPORT],exit=1,fail_ready=True);assert not(f/(RAM+'/video-probe.current').lstrip('/')).exists()and not list((f/RAM.lstrip('/')).glob('video-buffer-*'));run(f,'retry-after-READY-failure',['--run',REPORT])
 f=fixture();run(f,'export-DONE-sync-failure-retains-RAM',['--run',REPORT],exit=1,fail_sync='/DONE');assert not(f/(REPORT+'/DONE').lstrip('/')).exists();run(f,'retry-existing-counts-after-DONE-failure',['--stop'],reason='TRACE_SAVED')
 f=fixture();run(f,'CPU-budget-throttle-two-steps',['--run',REPORT],after=90,cpufraction=0.01);_,b=header(f);assert struct.unpack_from('<I',b,104)[0]==1000
 f=fixture();run(f,'capacity-halts-sampling-retains-RAM',['--run',REPORT],action='limit');_,b=header(f);assert struct.unpack_from('<I',b,24)[0]==6000
 f=fixture();run(f,'pending-for-live-controller',['--run',REPORT],action='usb');put(f,USB,'0\n');put(f,ROOT+'/lens-evidence.fd',b'');run(f,'live-controller-mark',['--mark'],live=True,reason='TRACE_NOT_RUNNING',exit=3)
 f=fixture();put(f,RAM+'/video-probe.current','CORRUPT\n');run(f,'invalid-registry-refused',['--status'],exit=1,reason='TRACE_REGISTRY_INVALID');run(f,'invalid-registry-not-overwritten',['--run',REPORT],exit=1)
 f=fixture();run(f,'recording-prevents-arming',['--run',REPORT],exit=1,busy_camera=True);assert not list((f/RAM.lstrip('/')).glob('video-buffer-*'))
 f=fixture();run(f,'pending-for-control-requests',['--run',REPORT],action='usb');put(f,USB,'0\n');path,b=header(f);struct.pack_into('<I',b,28,1);(f/path.lstrip('/')).write_bytes(b);run(f,'live-mark-request-queued-without-false-applied-claim',['--mark'],live=True,reason='TRACE_COMPARE_REQUESTED');_,b=header(f);assert struct.unpack_from('<I',b,116)[0]==1;run(f,'live-stop-request-committed',['--stop'],live=True,reason='TRACE_SAVE_REQUESTED');_,b=header(f);assert struct.unpack_from('<I',b,112)[0]==(struct.unpack_from('<I',b,100)[0]^0xa18c39f7)
 f=fixture();run(f,'counts-write-error-preserves-RAM',['--run',REPORT],exit=1,fail_write='.part');assert not(f/(REPORT+'/counts.bin').lstrip('/')).exists();run(f,'counts-write-error-retry',['--stop'],reason='TRACE_SAVED')
 f=fixture();run(f,'pending-before-simulated-power-cycle',['--run',REPORT],action='usb');put(f,USB,'0\n');put(f,ROOT+'/video-probe.current','STALE_PERSISTENT_TMP_FROM_OLD_BOOT\n')
 for p in(f/RAM.lstrip('/')).iterdir():assert p.is_file()and p.resolve().is_relative_to(V.resolve());p.unlink()
 run(f,'RAM-reset-ignores-persistent-tmp-registry',['--status'],reason='TRACE_OFF');(f/(REPORT+'2').lstrip('/')).mkdir();run(f,'fresh-start-after-RAM-reset',['--run',REPORT+'2'])
 f=fixture();put(f,'/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq','1000000\n');put(f,'/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor','performance\n');run(f,'kernel-CPU-frequency-and-governor-readonly',['--run',REPORT]);_,b=header(f);assert struct.unpack_from('<II',b,16384+500)==(1000000,2)
 f=fixture();put(f,'/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq','BAD\n');put(f,'/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor','unknown\n');run(f,'unknown-CPU-frequency-keeps-counters-usable',['--run',REPORT]);_,b=header(f);assert struct.unpack_from('<II',b,16384+500)==(0,0)
 report=dict(passed=True,binary_sha256=hashlib.sha256(BIN.read_bytes()).hexdigest(),source_sha256=hashlib.sha256((R/'core/src/nxl-video-probe.c').read_bytes()).hexdigest(),cases=records,actual_ARM=True,modeled_OS_only=True,hardware_tested=False)
 (R/'validation/current/video-probe-os.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
