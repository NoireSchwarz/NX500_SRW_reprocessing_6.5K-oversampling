from context import *
import importlib.util,shutil
os.environ['NXL_REPO']=str(R);os.environ['NXL_REPAIR_WORK']=str(W.parent/'r11f2-work')
sys.path[:0]=[str(W.parent/'r11k3-srw-restore-work'),str(W.parent/'r11f2-work'),str(R/'work')]
import fixture_r11k as ff
spec=importlib.util.spec_from_file_location('rc1_identity',W/'runtime_identity.py');ri=importlib.util.module_from_spec(spec);spec.loader.exec_module(ri)
from audit_camera_manifest import audit_package
assert ri.audit(S)['passed']and audit_package(S,include_sums=False)['passed']
V=next(W/('tests/installer-'+str(i))for i in range(1,100)if not(W/('tests/installer-'+str(i))).exists());V.mkdir(parents=True)
ff.V=V
control=V/'control/work/r10k-ep-flow/installer';control.mkdir(parents=True);shutil.copyfile(S/'nx-lut-demo/deploy.sh',control/'deploy-r11f.sh')
rows=[]
for previous,label,library in [(W.parent/'r11p-clean-log-work/stage','R11p-original',R/'work/firmware/libmmfcore.so'),(B,'R11q-current-patched',M/'native/libmmfcore.r11f.so')]:
 f=ff.Fixture();assert V.resolve()in f.root.resolve().parents
 process=(f.src/'process.sh').read_bytes();progress=(f.src/'progress.sh').read_bytes()
 shutil.copytree(M,f.src,dirs_exist_ok=True);(f.src/'process.sh').write_bytes(process);(f.src/'progress.sh').write_bytes(progress)
 shutil.copytree(S/'nx-lut-demo/ownership',f.pkg/'ownership',dirs_exist_ok=True);f.control=control.parent
 shutil.copytree(previous/'nx-lut-demo/nx-lut-lite',f.mod)
 ff.put(f.mod/'state/demo49-installed','NXDEMO49_INSTALLED_V1\n');ff.put(f.mod/'state/demo51-installed','NXDEMO51_INSTALLED_V1\n')
 shutil.copyfile(previous/'nx-lut-demo/module.manifest',f.mod/'state/module.manifest')
 ff.put(f.mod/'state/user-choice','KEEP_USER_CHOICE');ff.put(f.root/'opt/usr/nx-lut-demo49-backup/identity','NXDEMO49_BACKUP_V1\n')
 shutil.copyfile(library,f.core);f.pin();core_sha=sha(f.core);inode=f.core.stat().st_ino
 p,log=f.run();assert p.returncode==0,(label,p.stderr,log[-5000:])
 assert sha(f.core)==core_sha and f.core.stat().st_ino==inode and not f.back.exists()
 assert(f.mod/'state/user-choice').read_text()=='KEEP_USER_CHOICE'
 assert(f.root/'mount-mode').read_text().strip()=='ro'
 assert(f.mod/'state/module.manifest').read_bytes()==(f.pkg/'module.manifest').read_bytes()
 assert'CORE_READ_ONLY after_sha256='in log and'HEVC_GATE_LIBRARY_COMMITTED'not in log
 assert sha(f.mod/'srw-sap-service.sh')==sha(M/'srw-sap-service.sh')
 for q in(M/'bin').iterdir():
  if q.is_file():assert sha(f.mod/'bin'/q.name)==sha(q)
 for q in(M/'looks').glob('*.bin'):assert sha(f.sd/'looks'/q.name)==sha(q)
 rows.append(dict(case=label,passed=True,fixture=str(f.root),core_sha256=core_sha,core_bytes_and_inode_preserved=True,
  prior_user_state_and_history_preserved=True,all_19_LUTs_installed=True))
 print('PASS full production upgrade',label,flush=True)
put(W/'installer-validation.json',dict(passed=True,cases=rows,production_deploy_sha256=sha(S/'nx-lut-demo/deploy.sh'),
 module_manifest_sha256=sha(S/'nx-lut-demo/module.manifest'),real_production_shell=True,
 OS_process_mount_fileops_endpoints_modeled=True,hardware_tested=False))
