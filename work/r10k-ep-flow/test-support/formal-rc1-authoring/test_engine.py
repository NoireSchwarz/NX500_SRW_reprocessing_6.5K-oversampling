"""Production shell control flow; real ARM table tools, modeled camera state."""
from context import *
import re,shutil,struct
sys.path[:0]=[str(PY),str(SRC/'tools/log_video')]
from test_reload import posix
V=next(W/('tests/engine-'+str(i))for i in range(1,100)if not(W/('tests/engine-'+str(i))).exists());V.mkdir(parents=True)
runtime=V/'runtime';(runtime/'bin').mkdir(parents=True);(runtime/'state').mkdir();shutil.copytree(M/'data',runtime/'data')
state=V/'memory';state.mkdir();rr=V/'run';rr.mkdir();shutil.copytree(S/'looks',V/'looks')
p7=(R/'work/firmware/P7.bin').read_bytes();table=p7[0x380000:0x384000];slot=bytes((i*17+31)&255 for i in range(0x6000))
(state/'table.bin').write_bytes(table);(state/'slot.bin').write_bytes(slot)
stock=bytearray(0x5000);stock[0x200:0x4f00]=(M/'data/clean-log/NXLOG_REF_V1.bin').read_bytes();(state/'stock.bin').write_bytes(stock)
(V/'version.info').write_text('1.12\nNX500\n','ascii',newline='\n')
for n in ['lut.sh','lut-core.sh','gamma-reload.sh','log-lut-policy.sh']:
 s=(M/n).read_text('utf8').replace('/tmp/nx-lut-lite',posix(rr)).replace('/etc/version.info',posix(V/'version.info'))
 (runtime/n).write_text(s,'utf8',newline='\n')
(runtime/'process.sh').write_text('nxl_require_normal_mode(){ [ "${FX_FACTORY:-0}" = 0 ]; }\n','ascii',newline='\n')
adapter=V/'arm_adapter.py'
base=(SRC/'tools/log_video/arm_lut_model.py').read_text('utf8')
base=base.replace("elif physical==0x81119000:","elif physical==0x81115000:assert size==0x5000 and not writable;name='stock.bin'\n   elif physical==0x81119000:")
adapter.write_text(base.replace('if __name__==\'__main__\':cli()',"if __name__=='__main__':\n sys.stdout.reconfigure(newline='\\n');cli()"),'utf8',newline='\n')
endpoint=V/'endpoints.py'
endpoint.write_text(r'''from pathlib import Path
import hashlib,json,os,shutil,struct,subprocess,sys
sys.stdout.reconfigure(newline='\n')
root=Path(os.environ['NXLOG_MODEL_ROOT']);state=root/'memory';cfg=root/'control.json'
c=json.loads(cfg.read_text('utf8'));kind=sys.argv[1];args=sys.argv[2:]
def path(s):return Path(s[1]+':'+s[2:])if len(s)>3 and s[0]=='/'and s[2]=='/'else Path(s)
if kind=='ops':
 if args[0]=='sha256':print(hashlib.sha256(path(args[1]).read_bytes()).hexdigest())
 elif args[0]=='same':sys.exit(0 if path(args[1]).read_bytes()==path(args[2]).read_bytes()else 1)
 else:assert False,args
 sys.exit(0)
with(root/'endpoints.jsonl').open('a',encoding='utf8')as f:f.write(json.dumps([kind,args])+'\n')
if kind=='usb':sys.exit(1 if c.get('usb')else 0)
if kind=='guard':
 if c.get('guard_busy'):sys.exit(75)
 p=subprocess.run([r'C:\Program Files\Git\usr\bin\sh.exe',*args]);sys.exit(p.returncode)
if kind=='idle':
 if not args:print('NXL_LUT_IDLE1 ready='+str(0 if c.get('busy')else 1));sys.exit(1 if c.get('busy')else 0)
 assert args==['--gamma-state'];kind='gamma';args=['status']
if kind=='rw'and args[0]==c.get('fail_rw'):
 c.pop('fail_rw');cfg.write_text(json.dumps(c),'utf8');sys.exit(1)
if kind in ['rw','sw','check']:
 tool={'rw':'nx_regtabwrite','sw':'nx_slotwrite','check':'nxl-tablecheck'}[kind]
 p=subprocess.run([sys.executable,'-B',os.environ['NXLOG_ADAPTER'],tool,*args]);sys.exit(p.returncode)
if kind=='blend':
 sys.path[:0]=[os.environ['FX_TOOLING'],os.environ['FX_TOOLS']]
 from post_file_model import PostFileModel
 n=len(list(root.glob('blend-*')));v=root/('blend-'+str(n));v.mkdir()
 for i,name in enumerate(['base.bin','a.bin','b.bin']):shutil.copyfile(path(args[i]),v/name)
 assert args[-2]=='--output'
 r=PostFileModel(Path(os.environ['NXLOG_BINARY_DIR'])/'nxl-lutblend.arm',['/base.bin','/a.bin','/b.bin',*args[3:-2],'--output','/out.bin'],v).run()
 if not r['exit']:shutil.copyfile(v/'out.bin',path(args[-1]))
 print(r['stdout'],end='');sys.exit(r['exit'])
if kind=='sharp':
 if c.get('fail_sharp'):sys.exit(1)
 assert len(args)==1;c['sharp']=args[0];cfg.write_text(json.dumps(c),'utf8');sys.exit(0)
if kind=='st':
 print('| VARIABLE_PWSHARPNESS | unused | '+('000fd80b'if c.get('bad_sharp_read')else c.get('sharp','000fd80a'))+' |');sys.exit(0)
if kind=='sha':
 special={'/usr/lib/libcapture-fw-prod.so':'a07867292610ac6376e29e3cd64bc79a274452412387fb7853c5a3fb3f96bba5','/usr/lib/libmulticore-bridge.so':'f14a12e4d9172989d73c8dff0fd59580c65ccd1972264c9d94c8d1a736820390'}
 print(special.get(args[0])or hashlib.sha256(path(args[0]).read_bytes()).hexdigest(),args[0]);sys.exit(0)
assert kind=='gamma'
cmd=args[0];rc=0
if cmd=='log':
 assert(root/'run/clean-log.policy').read_text()=='NXL_CLEAN_LOG1 READY\n'
 assert(state/'slot.bin').read_bytes()[0xf00:0x5c00]==(root/'runtime/data/clean-log/NXLOG_REF_V1.bin').read_bytes()
 c.update(mode='LOG',phase='FAILED'if c.get('fail_log')else'NATIVE_QUEUED');rc=1 if c.get('fail_log')else 0
 (root/'run/gamma-r11h.lease').write_text('MODELED_LEASE')
elif cmd=='restore':
 if c.get('fail_restore'):rc=1
 else:c.update(mode='FACTORY',phase='FACTORY_QUEUED')
elif cmd=='lift':c.update(mode='LIFT',phase='NATIVE_QUEUED')
else:assert cmd=='status'
cfg.write_text(json.dumps(c),'utf8')
print('NXL_GAMMA_RELOAD1\nmode='+c['mode']+'\nphase='+c['phase']+'\nreason=MODELED_NATIVE_ENDPOINT\nepoch=1\nqueued_epoch=1\nqualified=0\napplies=0\ninvalidations=0\nqueues=0\nfault=0\nend=NXL_GAMMA_RELOAD_END1')
sys.exit(rc)
''','utf8',newline='\n')
for file,kind in [('nxl-fileops','ops'),('nxl-gamma-reload.arm','gamma'),('nxl-usb-guard.arm','usb'),('nx_regtabwrite.arm','rw'),('nx_slotwrite.arm','sw'),('nxl-tablecheck.arm','check'),('nxl-lutblend.arm','blend'),('nxl-lutidle.arm','idle'),('nxl-sharp.arm','sharp'),('nxl-lutguard.arm','guard'),('st','st'),('sha256sum','sha')]:
 (runtime/'bin'/file).write_text('#!/bin/sh\nexec "$NXLOG_PYTHON" -B "$FX_ENDPOINT" '+kind+' "$@"\n','ascii',newline='\n')
hashes={'OPS_SHA':'bin/nxl-fileops','LUT_IDLE_SHA':'bin/nxl-lutidle.arm','GAMMA_RELOAD_SHA':'bin/nxl-gamma-reload.arm','USB_GUARD_SHA':'bin/nxl-usb-guard.arm','LUT_GUARD_SHA':'bin/nxl-lutguard.arm','BLEND_SHA':'bin/nxl-lutblend.arm','SHARP_SHA':'bin/nxl-sharp.arm','LOG_LUT_POLICY_SHA':'log-lut-policy.sh','LUT_CATALOG_SHA':'data/release-lut-allowlist.tsv','LUT_CORE_SHA':'lut-core.sh','ENGINE_SHA':'lut.sh'}
for n in ['lut-core.sh','lut.sh','gamma-reload.sh']:
 p=runtime/n;s=p.read_text('utf8')
 for key,file in hashes.items():s=re.sub('^'+key+'=[0-9a-f]{64}$',key+'='+sha(runtime/file),s,flags=re.M)
 p.write_text(s,'utf8',newline='\n')
env=dict(ENV,NX_LUT_LOOKS=posix(V/'looks'),NXLOG_REPO=str(R),NXLOG_MODEL_ROOT=str(V),NXLOG_MODEL_STATE=str(state),NXLOG_BINARY_DIR=str(M/'bin'),NXLOG_PYTHON=sys.executable.replace('\\','/'),NXLOG_ADAPTER=str(adapter),FX_ENDPOINT=str(endpoint),FX_TOOLING=str(PY),FX_TOOLS=str(SRC/'tools'),MSYS2_ARG_CONV_EXCL='*')
env['PATH']=str(runtime/'bin')+';'+r'C:\Program Files\Git\usr\bin;'+os.environ.get('PATH','')
cfg=V/'control.json';put(cfg,dict(mode='FACTORY',phase='OFF',sharp='000fd80a'))
records=[]
def control(**kw):c=json.loads(cfg.read_text('utf8'));c.update(kw);put(cfg,c)
def snap():return(state/'table.bin').read_bytes(),(state/'slot.bin').read_bytes()
def call(script,*args,ok=True,contains=None,extra=None):
 preexisting_engine_lock=(rr/'engine.lock').exists()
 p=subprocess.run([r'C:\Program Files\Git\usr\bin\sh.exe',posix(runtime/script),*map(str,args)],env=dict(env,**(extra or{})),capture_output=True,text=True,encoding='utf8',timeout=90)
 text=p.stdout+p.stderr;row=dict(script=script,args=args,exit=p.returncode,output=text);records.append(row);put(V/'commands.json',records)
 assert(p.returncode==0)==ok,row
 if contains:assert contains in text,row
 assert(rr/'engine.lock').exists()==preexisting_engine_lock and not(rr/'clean-log-command.lock').exists(),row
 print('PASS',script,*map(str,args[:2]),'exit='+str(p.returncode),flush=True);return text
def detail(a='ejay_5207_n',pa=(100,0,0,0),b=None,pb=(0,0,0,0),log=False,**kw):return call('lut.sh','log-detail'if log else'detail',a,*pa,b or a,*pb,**kw)
def restored():assert snap()==(table,slot)and not(rr/'clean-log.policy').exists()and json.loads(cfg.read_text())['sharp']=='000fd80a'
def neutral():assert(state/'slot.bin').read_bytes()[0xf00:0x5c00]==(M/'data/clean-log/NXLOG_REF_V1.bin').read_bytes()
if '--handoff'in sys.argv:
 # The supervisor owns run.lock. Its existing worker temporarily releases
 # engine.lock and calls this exact restoration function before native ISP.
 (rr/'engine.lock').mkdir();before=snap();detail(ok=False,contains='backend busy');assert snap()==before;(rr/'engine.lock').rmdir()
 detail('pan_backlight_n');call('lut.sh','off');restored()
 detail('pan_smoky_s');call('lut.sh','off');restored()
 detail('pan_dramatic_s')
 text=(M/'memory-test.sh').read_text('utf8');body=text[text.index('reset_live_lut(){'):text.index('front_grant_ready(){')]
 job=V/'JOB_HANDOFF';job.mkdir();(rr/'engine.lock').mkdir();(V/'supervisor-run.lock').write_text('MODELED_HELD')
 script='''#!/bin/sh
set -eu
RUN="'''+posix(rr)+'''"
ENGINE="'''+posix(runtime/'lut.sh')+'''"
job="'''+posix(job)+'''"
engine_owned=1
fail(){ echo "ERR $*";exit 1; }
lock_release(){ rmdir "$1"; }
lock_take(){ mkdir "$1"; }
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
'''+body+'''reset_live_lut
[ "$engine_owned" = 1 ] && [ -d "$RUN/engine.lock" ]
echo NXL_ORIGINAL_SRW_HANDOFF_OK
'''
 (runtime/'handoff.sh').write_text(script,'utf8',newline='\n');call('handoff.sh',contains='NXL_ORIGINAL_SRW_HANDOFF_OK');(rr/'engine.lock').rmdir();restored()
 call('gamma-reload.sh','log');detail('vlog_pan_dramatic',log=True);detail('pan_dramatic_s',ok=False);call('gamma-reload.sh','restore');restored()
 put(W/'SRW-handoff-shell-validation.json',dict(passed=True,commands=records,actual_production_reset_live_lut_body=True,original_worker_lock_release_and_reacquire=True,
  active_engine_lock_blocks_LUT=True,new_Panasonic_modes_checked=True,actual_ARM_table_tools=True,OS_and_camera_endpoints_modeled=True,
  native_guard_no_second_run_flock_validated_separately=True,hardware_tested=False))
 print('PASS targeted original SRW handoff and new Panasonic modes',flush=True);sys.exit(0)
call('lut.sh','list');detail();detail(pa=(65,3,0,-2),b='ejay_g200_fx_n',pb=(80,-4,0,5));call('lut.sh','off');restored()
detail(pa=(0,0,0,0));restored()
detail(pa=(70,0,4,0),b='ejay_g200_fx_n',pb=(80,0,-2,0));assert json.loads(cfg.read_text())['sharp']!='000fd80a';call('lut.sh','off');restored()
for a,pa,b,pb in [('vlog_v709',(100,0,0,0),None,(0,0,0,0)),('ejay_5207_n',(100,0,0,0),'vlog_v709',(50,0,0,0)),('ejay_5207_n',(3,0,0,0),None,(0,0,0,0)),('ejay_5207_n',(50,11,0,0),None,(0,0,0,0))]:detail(a,pa,b,pb,ok=False);restored()
control(busy=True);detail(ok=False);restored();control(busy=False)
control(guard_busy=True);detail(ok=False);restored();control(guard_busy=False)
f=V/'looks/ejay_5207_n.bin';saved=f.read_bytes();f.write_bytes(saved[:-1]+b'x');detail(ok=False,contains='LUT_SOURCE_IDENTITY_FAILED');restored();f.write_bytes(saved)
control(usb=True);call('gamma-reload.sh','log',ok=False,contains='USB_CONNECTED');restored();control(usb=False)
call('gamma-reload.sh','log',extra={'FX_FACTORY':'1'},ok=False);restored()
detail();call('lut.sh','video-on');call('gamma-reload.sh','log');neutral()
call('gamma-reload.sh','status',contains='mode=LOG');detail('vlog_ejay_5207_n',log=True);creative=snap()
detail(ok=False,contains='RESTORE_LOG_BEFORE_STANDARD_LUT');assert snap()==creative
detail('vlog_ejay_5207_n',b='ejay_5207_n',log=True,ok=False,contains='LOG_SINGLE_LUT_REQUIRED');assert snap()==creative
detail('vlog_ejay_5207_n',b='vlog_v709',pb=(50,0,0,0),log=True,ok=False);assert snap()==creative
detail('vlog_ejay_5207_n',pa=(0,0,0,0),log=True);neutral()
call('lut.sh','off');neutral();call('lut.sh','video-off');neutral()
call('gamma-reload.sh','restore');restored()
control(fail_rw='still-write');call('gamma-reload.sh','log',ok=False);assert(rr/'clean-log.policy').exists();call('gamma-reload.sh','restore');restored()
call('gamma-reload.sh','log');control(fail_restore=True);stable=snap();call('gamma-reload.sh','restore',ok=False);assert snap()==stable;control(fail_restore=False)
control(fail_rw='view-restore');call('gamma-reload.sh','restore',ok=False,contains='GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED');assert(rr/'clean-log.policy').exists()
call('gamma-reload.sh','restore');restored()
detail();call('lut.sh','video-on');detail(ok=False,contains='DISABLE_VIDEO_LUT');call('lut.sh','video-off');call('lut.sh','off');restored()
control(fail_sharp=True);detail(pa=(100,0,2,0),ok=False,contains='SHARPNESS_READBACK_UNCONFIRMED');assert(rr/'session/param-sharp.pending').exists()
detail(ok=False,contains='SHARPNESS_PENDING');control(fail_sharp=False)
# No receipt is discarded. Model a delayed native submission becoming
# visible, then let the original R8f readback/restore reconcile it.
pending=rr/'session/param-sharp.pending';control(sharp=pending.read_text().strip());call('lut.sh','off');restored()
calls=[json.loads(x)for x in(state/'calls.jsonl').read_text().splitlines()]
ranges=[(0x81119f00,0x8111ec00),(0x803837f0,0x80383800),(0x80382c5c,0x80382c74),(0x80382b00,0x80382b30),(0x80383a24,0x80383a84)]
for c in calls:
 for at,n in c['physical_writes']:assert any(lo<=at and at+n<=hi for lo,hi in ranges),(c['tool'],hex(at),n)
put(W/'engine-shell-validation.json',dict(passed=True,cases=len(records),commands=records,actual_ARM_tools_calls=len(calls),
 production_shell_sha256={n:sha(M/n)for n in ['lut.sh','lut-core.sh','gamma-reload.sh','log-lut-policy.sh']},
 modeled_endpoints=['camera ordinary RAM','Gamma lease responses','read-only idle admission','sharpness OEM submission/readback','Linux guard handoff'],
 table_tools_and_M3_blend_actual_ARM=True,original_R11q_restored=True,MMIO_writes=False,hardware_tested=False))
print('PASS',len(records),'production shell cases; real ARM LUT tools, modeled camera',flush=True)
