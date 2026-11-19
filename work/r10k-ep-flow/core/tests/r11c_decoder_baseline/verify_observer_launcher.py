from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile,time
R=Path(__file__).resolve().parents[1];out=R/'validation/r7g';shell='C:/Program Files/Git/usr/bin/sh.exe'
def posix(p):
 s=Path(p).resolve().as_posix();return '/'+s[0].lower()+s[2:]
def write(p,s):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf8',newline='\n')
cases=[]
with tempfile.TemporaryDirectory(dir=out) as tmp:
 p=Path(tmp).resolve();module=p/'module';run=p/'run';sd=p/'sd';module.mkdir();run.mkdir();sd.mkdir()
 watch=module/'bin/nxl-handoff-watch.arm';write(watch,'#!/bin/sh\nprintf "%s\\n" "$1" > "$1/observer-argv"\nexit 0\n')
 write(module/'bin/nxl-fileops','#!/bin/sh\n[ "$1" = sha256 ] || exit 2\nsha256sum "$2" | while read -r h rest;do printf "%s\\n" "$h";done\n')
 mock=p/'mock';write(mock/'date','#!/bin/sh\nprintf "20261106_120000\\n"\n')
 s=(R/'base/nx-lut-lite/handoff-watch.sh').read_text();s=s.replace('RUN=/tmp/nx-lut-lite','RUN='+posix(run)).replace('OUT=/mnt/mmc/NXDEMO51','OUT='+posix(sd))
 h=hashlib.sha256(watch.read_bytes()).hexdigest();s=re.sub(r'^WATCH_SHA=.*','WATCH_SHA='+h,s,flags=re.M)
 # Fixed PID/time model exercises real collision handling across boots.
 s=s.replace('trace=$OUT/INIT_${stamp}_$$','trace=$OUT/INIT_${stamp}_9000').replace('trace=$OUT/INIT_${stamp}_$$_$attempt','trace=$OUT/INIT_${stamp}_9000_$attempt')
 script=module/'handoff-watch.sh';write(script,s)
 old=sd/'INIT_20261106_120000_9000';old.mkdir();write(old/'keep','old evidence\n')
 env={**os.environ,'PATH':posix(mock)+':/usr/bin:/bin'}
 for suffix in ['_1','_2']:
  result=subprocess.run([shell,str(script)],env=env,capture_output=True,text=True,timeout=10);assert result.returncode==0,result
  dest=sd/('INIT_20261106_120000_9000'+suffix);limit=time.monotonic()+2
  while not (dest/'observer-argv').exists() and time.monotonic()<limit:time.sleep(.02)
  assert (dest/'observer-argv').read_text().strip()==posix(dest),(result.stdout,result.stderr)
  assert (old/'keep').read_text()=='old evidence\n';cases.append('existing evidence retained; fresh '+suffix+' directory')
 write(script,s.replace('WATCH_SHA='+h,'WATCH_SHA='+'0'*64));before=set(sd.iterdir())
 result=subprocess.run([shell,str(script)],env=env,capture_output=True,text=True,timeout=10);assert result.returncode!=0 and set(sd.iterdir())==before
 cases.append('wrong observer identity rejected before creating trace directory')
result=dict(format='NX_R7G_LAUNCHER_SHELL_TEST1',passed=True,hardware_tested=False,cases=cases,
 scope='Production shell logic with modeled executable, date, PID and workspace paths; no SD or camera writes.')
(out/'launcher-flows.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
