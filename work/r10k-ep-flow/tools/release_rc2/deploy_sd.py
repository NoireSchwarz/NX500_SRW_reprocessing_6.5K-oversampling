"""The audited identity-pinned copy transaction, scoped to RC2 additions."""
from pathlib import Path
here=Path(__file__).resolve().parent
support=here/'rc1-test-support/deploy_sd.py'
if not support.exists():support=here.parent/'formal-rc1-authoring/deploy_sd.py'
s=support.read_text('utf8').replace('RC1','RC2').replace('rc1tmp-','rc2tmp-')
s=s.replace("'RC2_LUT_CATALOG_zh.txt'", "'RC2_VIDEO_RELOAD_RESEARCH_zh.txt'")
# Save the complete current installation log in addition to overwritten payloads.
s=s.replace("backup.mkdir(exist_ok=False)","""backup.mkdir(exist_ok=False)
install_log=SD/'NXDEMO51/logs/install.log'
if install_log.is_file():
 h=sha(install_log);saved=backup/'NXDEMO51/logs/install.log';saved.parent.mkdir(parents=True,exist_ok=True)
 shutil.copy2(install_log,saved);assert sha(saved)==h
 put(backup/'installation-log-identity.json',dict(source=str(install_log),sha256=h,bytes=install_log.stat().st_size))""")
exec(compile(s,str(Path(__file__).resolve()),'exec'))
