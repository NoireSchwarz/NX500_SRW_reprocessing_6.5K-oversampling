"""Verify RC2 on the identified SD and restore only three installer controls."""
from context import *
import ast,ctypes,datetime,uuid,shutil
EXPECTED=4040321079
reader=A.parent/'formal-rc1-authoring/deploy_sd.py'
tree=ast.parse(reader.read_text('utf8'))
node=next(n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name=='volumes')
exec(compile(ast.Module(body=[node],type_ignores=[]),'verified-volume-reader','exec'))
matches=volumes();assert len(matches)==1,matches;volume=matches[0]
assert volume['filesystem'].lower()=='exfat';SD=Path(volume['root']);P=W/'release-payload'
package=json.loads((W/'final-package-validation.json').read_text('utf8'))
assert package['passed']and package['release']=='0.4.0-RC2'
assert sha(package['archive'])==package['sha256']
assert sha(P/'nx-lut-demo/module.manifest')==package['module_manifest_sha256']
sums={}
for line in (P/'SHA256SUMS').read_text('utf8').splitlines():
 h,rel=line.split('  ',1);assert rel not in sums;sums[rel]=h
assert(SD/'NX500.bin').stat().st_size==352121435
checks=['nx-lut-demo/deploy.sh','nx-lut-demo/module.manifest','nx-lut-demo/patch-fileops','nx-lut-demo/bin/nxl-holdcheck.arm','nx-lut-demo/bin/nxl-session.arm','nx-lut-demo/ownership/index.sha256']
checks += [p.relative_to(P).as_posix()for p in (P/'nx-lut-demo/ownership').glob('*.manifest')]
for line in (P/'nx-lut-demo/module.manifest').read_text('utf8').splitlines():
 h,rel=line.split(' ',1);name='nx-lut-demo/nx-lut-lite/'+rel;assert sha(P/name)==h;checks.append(name)
for rel in checks:
 assert sha(P/rel)==sums[rel],('local payload changed',rel)
 q=SD/rel;assert q.is_file()and not q.is_symlink()and sha(q)==sums[rel],('SD payload mismatch',rel)
controls=['install.sh','nx_cs.adj','info.tg']
for rel in controls:
 b=(P/rel).read_bytes();assert sha(P/rel)==sums[rel]and b.endswith(b'\n')and b'\r'not in b
 q=SD/rel;assert not q.is_symlink()and(not q.exists()or q.is_file())
assert b'install_revision=release-0.4.0-rc2'in(P/'install.sh').read_bytes()
log=SD/'NXDEMO51/logs/install.log';log_before=sha(log)if log.is_file()else None
text=log.read_text('utf8',errors='replace')if log_before else''
last=text.rsplit('NX51_INSTALL_ONLY_ENTRY ',1)[-1]
observed='GAMMA_LEASE_REQUIRES_RESTORE_AND_ACTUAL_BOOT_BEFORE_MODULE_UPDATE'if 'INSTALL_ERROR: GAMMA_LEASE_REQUIRES_RESTORE_AND_ACTUAL_BOOT_BEFORE_MODULE_UPDATE'in last else'UNCLASSIFIED_SEE_SAVED_LOG'
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
backup=W/('reinstall-entry-'+stamp);backup.mkdir()
if log_before:
 shutil.copy2(log,backup/'install-before.log');assert sha(backup/'install-before.log')==log_before
prior={}
for rel in controls:
 q=SD/rel;prior[rel]=sha(q)if q.is_file()else None
 if q.is_file():shutil.copy2(q,backup/(rel+'.before'));assert sha(backup/(rel+'.before'))==prior[rel]
assert volumes()==[volume]
for rel in controls:
 q=SD/rel;temp=SD/('.rc2-entry-'+uuid.uuid4().hex[:12]);assert not temp.exists()
 with temp.open('xb')as f:f.write((P/rel).read_bytes());f.flush();os.fsync(f.fileno())
 assert sha(temp)==sums[rel];os.replace(temp,q);assert sha(q)==sums[rel]
assert volumes()==[volume]and(sha(log)if log.is_file()else None)==log_before
result=dict(passed=True,release='0.4.0-RC2',volume=volume,validated_payload_files=len(checks),
 restored_controls={rel:dict(before=prior[rel],after=sha(SD/rel),bytes=(SD/rel).stat().st_size)for rel in controls},
 backup=str(backup),installation_log_preserved=log_before,latest_observed_install_result=observed,
 media_firmware_backups_and_history_not_written=True,camera_install_success_not_claimed=True)
put(backup/'receipt.json',result);put(W.parent/'outputs'/('RC2_reinstall_entry_'+stamp+'.json'),result)
print(json.dumps(dict(passed=True,volume=volume,validated_payload_files=len(checks),controls=result['restored_controls'],backup=str(backup),observed_failure=observed),ensure_ascii=False),flush=True)
