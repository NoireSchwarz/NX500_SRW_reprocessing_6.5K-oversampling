#!/usr/bin/env python3
"""Run the production static ARM adapter read-only under QEMU; no native controller."""
from pathlib import Path
import hashlib,importlib.util,json,os,re,shutil,subprocess,tempfile
P=Path(__file__).resolve().parents[1];R=P.parent;W=R.parent
Q=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';BIN=P/'bin/nxl-demo-post.arm'
HOST_COMPLETE_STATUS='NX52POSTSTATUS1\nphase=COMPLETE\nreason=NONE\ncontroller_pid=529\nLUT_hardware_jobs=0\nCPU_JPEG=0\n'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 spec=importlib.util.spec_from_file_location('post_r6_host_tests',P/'test_post.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 records=[]
 with tempfile.TemporaryDirectory(prefix='post-r6-arm-readonly-') as tmp:
  out=Path(tmp)/'MUST_NOT_CREATE';args=m.Post().base_args(out)
  cases=[('version',['--version'],0),('clock',['--clock'],0),('valid_default',['--verify-input',*args],0),('valid_runner_independent',['--verify-input',*args,'--final-check','runner-independent'],0),('invalid_final_check',['--verify-input',*args,'--final-check','skip'],42)]
  bad=args.copy();bad[bad.index('--producer-report-sha256')+1]='0'*64;cases.append(('producer_sha_mismatch',['--verify-input',*bad],42))
  actual=W/'demo51-field-success/JOB_1793869199_415_10dc3936/jpeg/attempt_1'
  # The archive's real post-status snapshot is stale VERIFY_INPUT. Preserve its
  # expected refusal; construct explicitly labeled host fixtures for positives.
  cases.append(('original_archive_stale_VERIFY_INPUT_status_rejected',['--verify-output',str(actual)],2))
  fixtures=[]
  for mode in ['legacy-R5','runner-independent']:
   model=Path(tmp)/('HOST_FIXTURE_'+mode);shutil.copytree(actual,model,copy_function=os.link)
   status=model/'post-status.txt';status.unlink();status.write_text(HOST_COMPLETE_STATUS)
   if mode=='runner-independent':
    report=model/'result.json';j=json.loads(report.read_text());j.update(final_check_mode=mode,final_verification_delegated=True,successful_return_contract='PUBLISHED_PENDING_RUNNER_VERIFICATION');report.unlink();report.write_text(json.dumps(j)+'\n')
    marker=model/'P52_POST_COMPLETE';lines=marker.read_text().splitlines();lines[1]='result_sha256='+h(report);marker.unlink();marker.write_text('\n'.join(lines)+'\n')
   fixtures.append({'mode':mode,'metadata_is_host_model':True,'native_JPEG_sha256':h(model/'native.jpg'),'original_JPEG_unchanged':h(model/'native.jpg')==h(actual/'native.jpg'),'status_sha256':h(status),'only_human_status_constructed':mode=='legacy-R5','R6_delegation_result_marker_is_host_model':mode=='runner-independent','readonly_hardlinks':{n:{'same_inode':(model/n).stat().st_ino==(actual/n).stat().st_ino,'same_device':(model/n).stat().st_dev==(actual/n).stat().st_dev,'sha256':h(actual/n)} for n in ['native.jpg','hardware/input422.bin','hardware/j422-proof.bin','hardware/copy_request.bin','hardware/result.json']}})
   cases.append(('host_metadata_fixture_'+mode,['--verify-output',str(model)],0))
  for name,arguments,want in cases:
   r=subprocess.run([str(Q),str(BIN),*arguments],text=True,capture_output=True,timeout=90)
   ok=r.returncode==want and not out.exists()
   if name=='clock':ok=ok and re.fullmatch(r'NXPOSTCLOCK1 monotonic_ms=[0-9]+\n',r.stdout) is not None and not r.stderr
   if name=='version':ok=ok and 'FINAL_CHECK_DELEGATION_R6' in r.stdout
   item={'case':name,'exit':r.returncode,'expected_exit':want,'passed':ok,'stdout':r.stdout.strip(),'stderr':r.stderr.strip(),'output_created':out.exists()};records.append(item);print(name,r.returncode,'PASS' if ok else 'FAIL',flush=True)
 result={'format':'NXPOST_R6_ARM_SMOKE1','binary_sha256':h(BIN),'binary_bytes':BIN.stat().st_size,'hardware_called':False,'actual_ARM_execution':True,'tests_run':len(records),'passed':all(x['passed'] for x in records),'cases':records,'positive_output_fixtures':fixtures,'archive_post_status_sha256':h(actual/'post-status.txt'),'host_COMPLETE_status_text_sha256':hashlib.sha256(HOST_COMPLETE_STATUS.encode()).hexdigest(),'archive_post_status_text':(actual/'post-status.txt').read_text(),'host_COMPLETE_status_text':HOST_COMPLETE_STATUS,'camera_pass':False,'archive_status_note':'Untouched uploaded post-status is VERIFY_INPUT and both R5/R6 correctly refuse it; positive fixtures only replace status metadata (plus explicit R6 delegation metadata for that case), retaining real native JPEG/input/proof bytes.','source_sha256':{str(p.relative_to(R)):h(p) for p in [P/'src/hw-post.c',P/'test_post.py',Path(__file__),P/'tools/build_post.py']},'qemu_sha256':h(Q)}
 (P/'validation/arm-smoke.json').write_text(json.dumps(result,indent=2)+'\n');return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
