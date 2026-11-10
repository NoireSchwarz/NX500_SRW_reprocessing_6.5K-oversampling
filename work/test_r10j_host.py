from pathlib import Path
import ast,json,struct,subprocess,sys,uuid,hashlib,zlib
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';V=R/'validation/current/video-host-tests';V.mkdir(parents=True,exist_ok=True);sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile
text=(W/'test_r10j_fullwidth.py').read_text('utf8');exec(text[:text.index('rows=[]')],globals())
V=R/'validation/current/video-host-tests';records=[]
def build(src,name):
 p=V/(name+'.arm');z=subprocess.run([sys.executable,str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross'),'--source',str(src),'--output',str(p)],capture_output=True,text=True);assert not z.returncode,(z.stdout,z.stderr);return p
def fixture(name):
 f=V/(name+'-'+uuid.uuid4().hex[:6]);(f/'mnt/mmc/NXDEMO51/logs').mkdir(parents=True);(f/'tmp/nx-lut-lite').mkdir(parents=True);(f/'proc/123/task').mkdir(parents=True);(f/'proc/123/fd').mkdir();(f/'proc/123/fdinfo').mkdir();(f/'proc/123/comm').write_text('di-camera-app\n');(f/'proc/123/fd/7').write_bytes(b'FD');(f/'proc/123/fdinfo/7').write_text('flags:\t0100000\n')
 for tid in range(123,131):p=f/f'proc/123/task/{tid}';p.mkdir();(p/'status').write_text('Name:\tcamera\nState:\tS (sleeping)\nTracerPid:\t0\n')
 return f
for source,name in[('test_video_fullwidth.c','transaction-failure-contracts'),('test_video_fullwidth_threads.c','production-UI-quiescence'),('test_video_clock_capture.c','recording-watch-clock-snapshots')]:
 p=build(R/'core/tests'/source,name);f=fixture(name)
 if name=='transaction-failure-contracts':
  snapshots=sorted((W/'r9o-admission-evidence').glob('VIDEO_FULL_*/before-profile.bin'));assert len(snapshots)==9 and all(q.stat().st_size==664 for q in snapshots)
  (f/'field-before-profiles.bin').write_bytes(b''.join(q.read_bytes() for q in snapshots))
 out=PostFileModel(p,[],f).run();assert out['exit']==0,(name,out);records.append(dict(name=name,**out));print('PASS',name,flush=True)
 if name=='transaction-failure-contracts':
  from analyze_video_front import analyze
  raw=(f/'video-observations.bin').read_bytes();assert len(raw)==1392;report=f/'observer-report';report.mkdir();frames=bytearray()
  for seq in range(2):
   payload=raw[seq*696:(seq+1)*696];header=struct.pack('<9I',0x3152584e,19,seq,seq*100000000,0,seq*100000000+1000,0,696,zlib.crc32(payload));frames+=header+struct.pack('<I',zlib.crc32(header))+payload
  (report/'contexts.bin').write_bytes(frames);decoded=analyze(report);changes=decoded['fullwidth_profile_changes'];assert len(changes)==2 and changes[0]['profile19_geometry']==[4096,2436,3856,2176,232,260]and changes[1]['profile19_geometry']==[6608,3680,6496,3644,92,36]and changes[1]['mapping_literal']=='0x8014e858'
  runtime_raw=(f/'video-runtime-observations.bin').read_bytes();assert len(runtime_raw)==256
  for seq in range(2):
   payload=runtime_raw[seq*128:(seq+1)*128];header=struct.pack('<9I',0x3152584e,20,seq,seq*100000000,0,seq*100000000+1000,0,128,zlib.crc32(payload));frames+=header+struct.pack('<I',zlib.crc32(header))+payload
  (report/'contexts.bin').write_bytes(frames);decoded=analyze(report);assert decoded['fullwidth_profile_changes'][1]['UD_power_scene']==8 and decoded['fullwidth_profile_changes'][1]['IPC_setup_function']=='0x801ce78c' and decoded['fullwidth_native_ISR_progress']=={'PP_EOF':True,'PP_WDMA0':True}
  bad=bytearray(frames);bad[-10]^=1;(report/'contexts.bin').write_bytes(bad)
  try:analyze(report)
  except AssertionError:pass
  else:raise AssertionError('corrupt observer record accepted')
  (R/'validation/current/video-observer.json').write_text(json.dumps(dict(passed=True,actual_C_ABI_bytes=696,actual_ARM_samples=2,decoded=decoded,corruption_rejected=True,physical_camera_tested=False),indent=2)+'\n',encoding='utf8');print('PASS kind19/20 C/PC ABI and corrupt-frame rejection',flush=True)
app=W/'firmware/di-camera-app'
def production(name,full=False,qhd=False):
 f=fixture(name);(f/'etc').mkdir();(f/'etc/version.info').write_text('1.12\nNX500\n');dest=f/'usr/apps/com.samsung.di-camera-app/bin/di-camera-app';dest.parent.mkdir(parents=True);dest.write_bytes(app.read_bytes());memory=bytearray(0x600000)
 with app.open('rb')as fd:
  e=ELFFile(fd)
  for seg in e.iter_segments():
   if seg['p_type']=='PT_LOAD':memory[seg['p_vaddr']:seg['p_vaddr']+seg['p_filesz']]=seg.data()
 if qhd:struct.pack_into('<II',memory,0xe4428,0xe3a08e5a,0xe3a0ac0a)
 (f/'proc/123/mem').write_bytes(memory)
 lines=['00008000-00600000 r-xp 00000000 00:00 0 /usr/apps/com.samsung.di-camera-app/bin/di-camera-app']
 libs=['/usr/lib/libcapture-fw-prod.so','/usr/lib/libshell-command.so','/lib/libc-2.13.so','/usr/lib/libudd5.so','/usr/lib/libtint-util.so','/usr/lib/libmm-type.so']
 for i,path in enumerate(libs):start=0x40000000+i*0x100000;lines.append(f'{start:08x}-{start+0x100000:08x} r-xp 00000000 00:00 0 {path}')
 lines.append('50000000-50200000 rw-s 96000000 00:00 0 /dev/mem');(f/'proc/123/maps').write_text('\n'.join(lines)+'\n');(f/'dev').mkdir();(f/'dev/mem').write_bytes(b'DEV')
 replay=Replay()
 if full:ret,r=replay.transaction();assert ret==0
 replay.put(0x8153e6d4,[0x80592640]);replay.put(0x8153e6d4+3364,[7]);replay.put(0x8142d154+3444,[0]);return f,replay
class HostModel(PostFileModel):
 def __init__(self,*args,replay=None,**kw):self.replay=replay;self.device_maps=[];super().__init__(*args,**kw)
 def interrupt(self,u,num,data):
  n=u.reg_read(UC_ARM_REG_R7);a=[u.reg_read(r)for r in[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5]]
  if n==192 and not(a[3]&0x20):
   assert self.fds[a[4]]['path']=='/dev/mem'and a[2]==1,('unexpected device write',a);at=a[5]<<12;size=(a[1]+4095)&~4095;addr=self.map_next;self.map_next+=size+4096;u.mem_map(addr,size);u.mem_write(addr,bytes(self.replay.u.mem_read(at,size)));self.device_maps.append((at,size,a[2]));self.ret(addr);return
  if n==85:
   assert self.string(a[0])=='/proc/123/fd/7';b=b'/mnt/mmc/DCIM/305PHOTO/SAM_0383.MP4';u.mem_write(a[1],b);self.ret(len(b));return
  if n==104:self.ret(0);return
  if n==143:assert a[1]in[1,5,6];self.ret(0);return
  if n==78:
   if a[0]:u.mem_write(a[0],struct.pack('<ii',1700000000,0))
   if a[1]:u.mem_write(a[1],b'\0'*8)
   self.ret(0);return
  return super().interrupt(u,num,data)
binary=R/'base/nx-lut-lite/bin/nxl-video-fullwidth.arm'
for name,verb,full,qhd,want,mutate in[
 ('stock-status','status',False,False,'state=UHD',None),
 ('R10a-width-status','status',True,False,'state=LEGACY',lambda f,r:(r.legacy_blocks(),r.put(CROP+0x1c,[6480]))),
 ('R9z-status','status',True,False,'state=LEGACY',lambda f,r:(r.put(0x801cf0e4,[0xe20770f5]),r.put(0x801cf0fc,[0xe3877030]))),
 ('full-status','status',True,False,'state=FULL',None),
 ('legacy-R9o-scale-status','status',True,False,'state=LEGACY',lambda f,r:(r.put(0x805922bc,[0x801ce78c]),r.put(0x80592294,[0x801cec38,0x801ceda0]),r.put(0x801cd9bc,[0x801cda40]),r.put(0x8010daa4,[0x8142d0d8]))),
 ('legacy-R9o-scale-apply-admitted','apply',True,False,'NATIVE_LIBRARIES_OR_CONSOLE_CONTEXT_CHANGED',lambda f,r:((r.put(0x805922bc,[0x801ce78c]),r.put(0x80592294,[0x801cec38,0x801ceda0]),r.put(0x801cd9bc,[0x801cda40]),r.put(0x8010daa4,[0x8142d0d8])),r.put(0x8100f7b0,[0x800e454c,0]))),
 ('R9v-E005-only-status','status',True,False,'state=LEGACY',lambda f,r:(r.put(0x80592294,[0x801cec38,0x801ceda0]),r.put(0x805922bc,[0x801cf038]),r.put(0x801cd9bc,[0x801cda40]),r.put(0x8010daa4,[0x8142d0d8]))),
 ('R9y-LDC-policy-status','status',True,False,'state=LEGACY',lambda f,r:(r.legacy_blocks(),r.put(0x8010daa4,[0x8142d0d8]))),
 ('R9y-LDC-policy-apply-admitted','apply',True,False,'NATIVE_LIBRARIES_OR_CONSOLE_CONTEXT_CHANGED',lambda f,r:(r.put(0x8010daa4,[0x8142d0d8]),r.put(0x8100f7b0,[0x800e454c,0]))),
 ('foreign-LDC-policy-refused','apply',False,False,'FULLWIDTH_ORIGINAL_CODE_OR_PROFILE_UNREADABLE',lambda f,r:(r.legacy_blocks(),r.put(0x8010daa4,[0x8142d0e0]))),
 ('mixed-scaled-lifecycle-refused','apply',True,False,'FULLWIDTH_PROFILE_OR_NATIVE_BUDGET_UNKNOWN',lambda f,r:(r.legacy_blocks(),r.put(0x80592294,[0x801cf628]))),
 ('QHD-status','status',False,True,'state=2K',None),
 ('full-idempotent','apply',True,False,'FULLWIDTH_ALREADY_ARMED',None),
 ('stock-restore-idempotent','restore',False,False,'NATIVE_CONFIGURATION_ALREADY_RESTORED',None),
 ('field-idle-preview-admitted','apply',False,False,'NATIVE_LIBRARIES_OR_CONSOLE_CONTEXT_CHANGED',lambda f,r:(r.legacy_blocks(),r.put(0x8100f7b0,[0x800e454c,0]))),
 ('recording-rejected','apply',False,False,'CAMERA_MEDIA_FILE_WRITE_BUSY',lambda f,r:(f/'proc/123/fdinfo/7').write_text('flags:\t0100001\n')),
 ('requested-UD-mode-rejected','apply',False,False,'PHOTO_MODE_REQUIRED_MOVIE_PIPELINE_ACTIVE',lambda f,r:(r.legacy_blocks(),r.put(0x8146e438,[23|(13<<8)]))),
 ('applied-crop-mode-rejected','apply',False,False,'PHOTO_MODE_REQUIRED_MOVIE_PIPELINE_ACTIVE',lambda f,r:(r.legacy_blocks(),r.put(0x8146e438,[13|(25<<8)]))),
 ('movie-work-rejected','apply',False,False,'PHOTO_MODE_REQUIRED_MOVIE_PIPELINE_ACTIVE',lambda f,r:(r.legacy_blocks(),r.put(0x81dfc800+0xb0570,[0xa4b40000]))),
 ('movie-source-rejected','apply',False,False,'PHOTO_MODE_REQUIRED_MOVIE_PIPELINE_ACTIVE',lambda f,r:(r.legacy_blocks(),r.put(0x81dfc800+0xb055c,[0xa4b40000]))),
 ('pending-photo-rejected','apply',False,False,'CAMERA_PHOTO_TASK_PENDING_BUSY',lambda f,r:(r.legacy_blocks(),r.put(0x8153e6d4+3204,[1]))),
 ('active-photo-task-rejected','apply',False,False,'CAMERA_PHOTO_TASK_BUSY',lambda f,r:(r.legacy_blocks(),r.put(0x8153e6d4+3364,[3]))),
 ('media-status-unreadable','apply',False,False,'CAMERA_MEDIA_STATUS_UNREADABLE',lambda f,r:(f/'proc/123/fdinfo/7').write_text('pos:\t0\n')),
 ('held-transaction-rejected','apply',True,False,'PREVIOUS_VIDEO_TRANSACTION_UNCONFIRMED_RESTART_REQUIRED',lambda f,r:(f/'tmp/nx-lut-lite/video-fullwidth.pending').write_text('pending')),
 ('QHD-full-apply-refused','apply',False,True,'RESTORE_3840_UHD_OUTPUT_FIRST',None),
 ('NX1-firmware-refused','apply',False,False,'NX500_FIRMWARE_IDENTITY_FAILED',lambda f,r:(f/'etc/version.info').write_text('1.41\nNX1\n')),
 ('unconstructed-QHD-root-refused','apply',False,False,'FULLWIDTH_ORIGINAL_CODE_OR_PROFILE_UNREADABLE',lambda f,r:(r.legacy_blocks(),r.put(0x8153cbec,[0]))),
 ('missing-QHD-instance-refused','apply',False,False,'FULLWIDTH_ORIGINAL_CODE_OR_PROFILE_UNREADABLE',lambda f,r:(r.legacy_blocks(),r.put(0x8153d958,[0]))),
 ('foreign-QHD-instance-refused','apply',False,False,'FULLWIDTH_ORIGINAL_CODE_OR_PROFILE_UNREADABLE',lambda f,r:(r.legacy_blocks(),r.put(0x82300000,[0x80592268]))),
 ('unrecognized-code-refused','status',False,False,'FULLWIDTH_ORIGINAL_CODE_OR_PROFILE_UNREADABLE',lambda f,r:(r.legacy_blocks(),r.put(0x801544b4,[0]))),
 ('mixed-profile-refused','apply',False,False,'FULLWIDTH_PROFILE_OR_NATIVE_BUDGET_UNKNOWN',lambda f,r:(r.legacy_blocks(),r.put(CROP+0x1c,[6480]))),
]:
 f,replay=production(name,full,qhd)
 if mutate:mutate(f,replay)
 if name.startswith(('legacy-R9o','R9v-','R9y-')):replay.put(0x801cf0e4,[0xe20770f5]);replay.put(0x801cf0fc,[0xe3877030])
 before=bytes(replay.u.mem_read(CROP,192));m=HostModel(binary,[verb],f,replay=replay);out=m.run();assert want in out['stdout'],(name,out);expected=0 if name in['R10a-width-status','R9z-status','R9y-LDC-policy-status','stock-status','full-status','R9v-E005-only-status','legacy-R9o-scale-status','QHD-status','full-idempotent','stock-restore-idempotent']else 1;assert out['exit']==expected,(name,out);assert 26 not in out['modeled_syscalls']and 117 not in out['modeled_syscalls']and bytes(replay.u.mem_read(CROP,192))==before
 audit_files=list((f/'mnt/mmc/NXDEMO51').glob('VIDEO_FULL_*/idle-check.json'))
 if name=='NX1-firmware-refused':assert not audit_files;idle=None
 else:assert len(audit_files)==1;idle=json.loads(audit_files[0].read_text('utf8'))
 if name=='field-idle-preview-admitted':assert idle['reason']=='CAMERA_IDLE_VERIFIED' and idle['snapshot_valid']==1 and idle['movie_active_mask']==0 and idle['power_current']==idle['power_video'] and idle['driver_modes']==0x0d0d
 records.append(dict(name=name,**out,device_maps_readonly=True,idle_audit=idle));print('PASS',name,flush=True)
(R/'validation/current/video-host.json').write_text(json.dumps(dict(passed=True,cases=records,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),scope='Actual static ARM helper status/idempotence/refusals over original constructor-derived RAM; actual helper thread methods with modeled ptrace; fifteen-store failure and full allocator partition contracts. Native request/send/release success remains a camera test.',physical_camera_tested=False),indent=2)+'\n',encoding='utf8')
