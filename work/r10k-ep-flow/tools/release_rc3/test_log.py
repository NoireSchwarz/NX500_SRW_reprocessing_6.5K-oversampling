from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc1-authoring/test_engine.py').read_text('utf8')
s=s[:s.index("if '--handoff'in sys.argv:")]
s+=r'''
# Focus on the new automatic transitions, using actual ARM table writes.
stock=snap()
detail('fuji_400h');call('lut.sh','video-on');call('gamma-reload.sh','log');neutral()
assert(rr/'session/current').read_text().strip()=='NXLOG_REF_V1'
detail('vlog_v709',log=True);creative=snap()
call('gamma-reload.sh','restore');restored()
assert not(rr/'session/current').exists()and not(rr/'session/video_enabled').exists()
# No manual LUT-off is needed before returning to ordinary editing.
detail('fuji_astia100f');assert not(rr/'clean-log.policy').exists()
call('gamma-reload.sh','restore');restored()
# An already-factory curve still clears a normal LUT without a Log marker.
call('gamma-reload.sh','restore');restored()
detail('fuji_classicchrome');before=snap();control(busy=True)
call('gamma-reload.sh','log',ok=False);assert snap()==before
control(busy=False);call('gamma-reload.sh','restore');restored()
# A partial original-LUT restore must not proceed to native Gamma log.
detail('fuji_400h');control(fail_rw='view-restore')
call('gamma-reload.sh','log',ok=False,contains='PREPARE_FAILED')
assert json.loads(cfg.read_text())['mode']=='FACTORY'and not(rr/'clean-log.policy').exists()
call('gamma-reload.sh','restore');restored()
call('gamma-reload.sh','log');detail('vlog_v709',log=True);before=snap();control(fail_restore=True)
call('gamma-reload.sh','restore',ok=False);assert snap()==before and(rr/'clean-log.policy').exists()
control(fail_restore=False);call('gamma-reload.sh','restore');restored()
# A damaged optional engine must not prevent the Gamma restore attempt.
call('gamma-reload.sh','log');p=runtime/'lut.sh';saved=p.read_bytes();p.write_bytes(saved+b'# changed\n')
call('gamma-reload.sh','restore',ok=False,contains='LOOK_CLEANUP_REQUIRED')
assert json.loads(cfg.read_text())['mode']=='FACTORY'and(rr/'clean-log.policy').exists()
p.write_bytes(saved);call('gamma-reload.sh','restore');restored()
listed=call('lut.sh','list');assert 'fuji_400h\n'in listed and 'kodak_5219_apx\n'not in listed
put(W/'log-transition-validation.json',dict(passed=True,cases=len(records),commands=records,
 production_shell_sha256={n:sha(M/n)for n in ['lut.sh','lut-core.sh','gamma-reload.sh','log-lut-policy.sh']},
 actual_ARM_table_and_M3_tools=True,ordinary_RAM_and_Gamma_SAP_OS_endpoints_modeled=True,
 automatic_factory_restore_on_both_transitions=True,no_manual_LUT_off_between_modes=True,
 native_Gamma_recovery_not_blocked_by_optional_engine_identity=True,hardware_tested=False))
print('PASS',len(records),'automatic Log/LUT transition cases',flush=True)
'''
exec(compile(s,str(here/'test_log.py'),'exec'))
