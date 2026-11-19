from context import *
# Use the existing production-shell/real ARM table fixture; add only the new
# IRQ and OEM command endpoints as modeled devices. No historical mutation script.
s=(A.parent/'formal-rc1-authoring/test_engine.py').read_text('utf8')
s=s[:s.index("if '--handoff'in sys.argv:")]
s=s.replace("['lut.sh','lut-core.sh','gamma-reload.sh','log-lut-policy.sh']", "['lut.sh','lut-core.sh','gamma-reload.sh','log-lut-policy.sh','video-mode.sh']")
s=s.replace("if kind=='usb':", """if kind=='load':
 if args==['source']:print('0x81115200');sys.exit(0)
 assert args==['current']or(len(args)==2 and args[0]=='once' and int(args[1],0)in [0x810fd100,0x81101e00,0x81106b00,0x81115200,0x81119f00,0x8111ec00])
 if c.get('fail_load'):sys.exit(6)
 print('OK DMA_RD_FINISH and processing mode verified');sys.exit(0)
if kind=='native':
 if c.get('fail_native'):sys.exit(1)
 assert args[0]in ['video24','video30','photo'];print('NXL_NATIVE_REFRESH1 ok=1 submitted=1 reason=NATIVE_RESOLUTION_COMMAND_AND_SETTINGS_VERIFIED');sys.exit(0)
if kind in ['vmode','full']:
 if c.get('fail_mode'):sys.exit(1)
 print('NXL_VIDEO_MODE1\\\\nstate=UHD\\\\nwidth=3840\\\\nheight=2160\\\\nrecording_or_capture=0\\\\nreason=MODELED_NATIVE_MODE\\\\nend=NXL_VIDEO_MODE_END1');sys.exit(0)
if kind=='usb':""")
s=s.replace("[('nxl-fileops','ops')", "[('nxl-lutload.arm','load'),('nxl-native-refresh.arm','native'),('nxl-video-mode.arm','vmode'),('nxl-video-fullwidth.arm','full'),('nxl-fileops','ops')")
s=s.replace("hashes={'OPS_SHA'", "hashes={'LUT_LOAD_SHA':'bin/nxl-lutload.arm','NATIVE_REFRESH_SHA':'bin/nxl-native-refresh.arm','VIDEO_MODE_SHA':'bin/nxl-video-mode.arm','VIDEO_FULLWIDTH_SHA':'bin/nxl-video-fullwidth.arm','OPS_SHA'")
s=s.replace("for n in ['lut-core.sh','lut.sh','gamma-reload.sh']:","for n in ['lut-core.sh','lut.sh','gamma-reload.sh','video-mode.sh']:")
s=s.replace("else:c.update(mode='FACTORY',phase='FACTORY_QUEUED')", "else:c.update(mode='FACTORY',phase='WAIT_FACTORY_NATIVE'if c.get('delay_restore')else'FACTORY_QUEUED')")
s=s.replace("else:assert cmd=='status'", """else:
 assert cmd=='status'
 if c.get('delay_restore')and c.get('phase')=='WAIT_FACTORY_NATIVE':
  c['polls']=c.get('polls',0)+1
  if c['polls']>=2:c.update(phase='FACTORY_QUEUED',delay_restore=False)
""")
exec(compile(s,str(A/'test_shell.py'),'exec'))
if '--gamma-only'in sys.argv:
 previous=json.loads((W/'engine-shell-validation.json').read_text('utf8'));put(W/'engine-shell-before-queue-order.json',previous)
 call('gamma-reload.sh','log',contains='phase=NATIVE_QUEUED');control(delay_restore=True)
 call('gamma-reload.sh','restore',contains='phase=FACTORY_QUEUED');restored();assert json.loads(cfg.read_text())['polls']>=2
 call('gamma-reload.sh','log');p=runtime/'lut.sh';saved=p.read_bytes();p.write_bytes(saved+b'# changed\n')
 call('gamma-reload.sh','restore',ok=False,contains='LOOK_CLEANUP_REQUIRED');assert json.loads(cfg.read_text())['mode']=='FACTORY'
 p.write_bytes(saved);call('gamma-reload.sh','restore');restored()
 put(W/'engine-shell-validation.json',dict(passed=True,cases=previous['cases']+len(records),commands=records,previous_full_suite='engine-shell-before-queue-order.json',change_since_full_suite='Gamma wait before factory LUT cleanup; integer sleep for stock shell',delayed_native_restore_tested=True,recovery_precedes_optional_engine_identity=True,SRW_handoff_no_EP_load=True,failed_DMA_never_emits_applied_receipt=True,video_native_refresh_before_and_after_patch=True,production_shell=True,real_ARM_table_and_M3_tools=True,MMIO_IRQ_and_native_command_endpoints_modeled=True,hardware_tested=False));print('PASS delayed native queue and damaged optional-engine recovery',flush=True);sys.exit(0)
detail('fuji_400h',pa=(65,2,0,-1),b='fuji_astia100f',pb=(40,-1,0,1));assert not(rr/'session/reload.pending').exists()
call('lut.sh','video-on');detail('fuji_400h',pa=(55,0,0,0));assert(rr/'session/video_enabled').exists()
call('gamma-reload.sh','log',contains='phase=NATIVE_QUEUED');neutral();assert not(rr/'session/reload.pending').exists()
detail('vlog_v709',log=True);call('lut.sh','off',contains='NX_LUT_BASELINE_APPLIED3 mode=VLOG');neutral()
call('gamma-reload.sh','restore',contains='phase=FACTORY_QUEUED');restored();assert not(rr/'session/reload.pending').exists()
control(fail_load=True);detail('fuji_astia100f',ok=False,contains='LUT_DMA_RELOAD_UNCONFIRMED');assert(rr/'session/reload.pending').exists()and not(rr/'session/detail-parameters').exists()
control(fail_load=False);detail('fuji_astia100f');assert not(rr/'session/reload.pending').exists()
# The production SRW handoff must never touch the loader/EP, even if it fails.
control(fail_load=True);before=len((V/'endpoints.jsonl').read_text().splitlines())
body=(M/'memory-test.sh').read_text('utf8');body=body[body.index('reset_live_lut(){'):body.index('front_grant_ready(){')]
job=V/'JOB_HANDOFF';job.mkdir();(rr/'engine.lock').mkdir()
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
after=[json.loads(x)for x in(V/'endpoints.jsonl').read_text().splitlines()[before:]];assert not any(x[0]=='load'for x in after)
control(fail_load=False,busy=True);stable=snap();detail('fuji_400h',ok=False);assert snap()==stable;control(busy=False)
for verb in ['full','2k','uhd']:
 start=len((V/'endpoints.jsonl').read_text().splitlines());call('video-mode.sh',verb)
 calls=[json.loads(x)for x in(V/'endpoints.jsonl').read_text().splitlines()[start:]]
 native=[x for x in calls if x[0]=='native'];assert len(native)==2 and all(x[1]==['video24'if verb=='full'else'video30']for x in native)
 assert next(i for i,x in enumerate(calls)if x[0]=='native')<next(i for i,x in enumerate(calls)if x[0]in ['vmode','full'])
control(fail_native=True);start=len((V/'endpoints.jsonl').read_text().splitlines());call('video-mode.sh','full',ok=False,contains='VIDEO_NATIVE_RELOAD_UNCONFIRMED')
calls=[json.loads(x)for x in(V/'endpoints.jsonl').read_text().splitlines()[start:]];assert not any(x[0]in ['vmode','full']for x in calls)
put(W/'engine-shell-validation.json',dict(passed=True,cases=len(records),commands=records,production_shell=True,real_ARM_table_and_M3_tools=True,MMIO_IRQ_and_native_command_endpoints_modeled=True,SRW_handoff_no_EP_load=True,failed_DMA_never_emits_applied_receipt=True,video_native_refresh_before_and_after_patch=True,hardware_tested=False))
print('PASS RC4 shell',len(records),'cases',flush=True)
