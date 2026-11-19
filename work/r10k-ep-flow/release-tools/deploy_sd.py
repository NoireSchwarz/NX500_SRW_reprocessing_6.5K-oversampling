from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc1-authoring/deploy_sd.py').read_text('utf8').replace('RC1','RC5').replace('rc1tmp-','rc5tmp-').replace('RC5_LUT_CATALOG_zh.txt','RC5_RELOAD_RESEARCH_zh.txt')
s=s.replace('backup.mkdir(exist_ok=False)', '''backup.mkdir(exist_ok=False)
install_log=SD/'NXDEMO51/logs/install.log'
if install_log.is_file():
 h=sha(install_log);saved=backup/'NXDEMO51/logs/install.log';saved.parent.mkdir(parents=True,exist_ok=True)
 shutil.copy2(install_log,saved);assert sha(saved)==h
 put(backup/'installation-log-identity.json',dict(source=str(install_log),sha256=h,bytes=install_log.stat().st_size))''')
exec(compile(s,str(here/'deploy_sd.py'),'exec'))
