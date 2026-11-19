from context import *
s=(A.parent/'formal-rc4-authoring/test_shell.py').read_text('utf8')
s=s[:s.index("if '--gamma-only'in sys.argv:")]
exec(compile(s,str(A/'test_shell.py'),'exec'))
detail('fuji_400h',pa=(65,2,0,-1),b='fuji_astia100f',pb=(40,-1,0,1));assert(rr/'session/video_enabled').exists()
call('lut.sh','video-off');assert not(rr/'session/video_enabled').exists()
detail('fuji_400h',pa=(55,0,0,0));assert(rr/'session/video_enabled').exists()
call('gamma-reload.sh','toggle',contains='mode=LOG');neutral();assert(rr/'session/video_enabled').exists()
detail('vlog_v709',log=True);assert(rr/'session/video_enabled').exists()
call('gamma-reload.sh','toggle',contains='mode=FACTORY');restored();assert not(rr/'session/video_enabled').exists()
call('gamma-reload.sh','toggle',contains='mode=LOG');neutral()
control(busy=True);before=snap();call('gamma-reload.sh','toggle',ok=False);assert snap()==before and(rr/'clean-log.policy').exists();control(busy=False)
call('gamma-reload.sh','toggle',contains='mode=FACTORY');restored()
detail('fuji_400h',pa=(0,0,0,0));restored();assert not(rr/'session/video_enabled').exists()
# Keep the old SRW handoff exclusion exact; the default video route can still
# be explicitly switched off in the menu before entering the existing SRW flow.
handoff=lambda p:p.read_text('utf8').split('reset_live_lut(){',1)[1].split('front_grant_ready(){',1)[0]
assert handoff(M/'memory-test.sh')==handoff(B/'nx-lut-demo/nx-lut-lite/memory-test.sh')
control(busy=True);before=snap();detail('fuji_400h',ok=False);assert snap()==before;control(busy=False)
put(W/'engine-shell-validation.json',dict(passed=True,cases=len(records),commands=records,ordinary_video_default_on=True,explicit_off_then_new_apply_on=True,VLog_toggle_roundtrip=True,zero_strength_factory_baseline=True,recording_busy_rejected=True,production_shell=True,real_ARM_table_and_M3=True,MMIO_IRQ_Gamma_and_OS_endpoints_modeled=True,hardware_tested=False))
print('PASS RC5 shell',len(records),'cases')
