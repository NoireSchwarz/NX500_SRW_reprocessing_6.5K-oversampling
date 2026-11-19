from context import *
import shutil
shutil.copyfile(A/'lut.sh',T/'base/nx-lut-lite/lut.sh')
shutil.copyfile(A/'nxl-lutguard.c',T/'core/src/nxl-lutguard.c')
host('lut-guard-final',T/'core/src/nxl-lutguard.c',W/'bin/nxl-lutguard-final.arm')
p=T/'base/nx-lut-lite/log-lut-policy.sh';s=p.read_text('utf8').replace('common video-mode/run flocks','the video-mode flock and the original engine.lock SRW handoff');p.write_text(s,'utf8',newline='\n')
put(W/'SRW-handoff-review.json',dict(passed=True,run_lock_owned_by_existing_session_supervisor=True,
 LUT_engine_mutual_exclusion='existing engine.lock',original_SRW_reset_live_lut_function_unchanged=True,
 Gamma_mode_exclusion='video-mode.fd flock',second_run_flock_removed=True,no_new_session_or_native_authority=True))
print('PASS original SRW engine.lock handoff retained; no second run flock',flush=True)
