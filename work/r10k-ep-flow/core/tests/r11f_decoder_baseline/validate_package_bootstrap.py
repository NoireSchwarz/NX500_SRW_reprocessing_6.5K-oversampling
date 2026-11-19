"""Replay the actual packaged bootstrap, with OS paths/exec endpoints modeled.

Component bytes and their identities are real package files. ARM execution
and camera operations are never attempted. Hashing is a host OS endpoint.
"""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,tempfile
from runtime_identity import audit
R=Path(__file__).resolve().parents[1];W=R.parent;M='nx-lut-demo/nx-lut-lite/'
SHELL='C:/Program Files/Git/usr/bin/sh.exe'
def posix(p):
 s=Path(p).resolve().as_posix();return '/'+s[0].lower()+s[2:]
def write(p,s):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf8',newline='\n')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def validate(package):
 package=Path(package).resolve();records=[]
 with tempfile.TemporaryDirectory(dir=R/'validation/r7h',prefix='bootstrap-') as temporary:
  f=Path(temporary).resolve();module=f/'module';shutil.copytree(package/M,module);out=f/'reports';(out/'logs').mkdir(parents=True);run=f/'run';run.mkdir()
  helper=f/'hash.py';write(helper,'import hashlib,sys\nprint(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())\n')
  write(module/'process.sh','nxl_require_normal_mode(){ return 0; }\n')
  write(module/'progress.sh','nxl_progress(){ :; }\n')
  hash_function='hash(){ "'+Path(sys.executable).as_posix()+'" "'+helper.as_posix()+'" "$1"; }'
  def source(kind,origin=package):
   name='memory-test.sh' if kind=='memory' else 'rearm-session.sh'
   s=(origin/(M+name)).read_text(encoding='utf8')
   s=s.split('[ "${NX51_SESSION_ROOT:-}"',1)[0] if kind=='memory' else s.split('job=${NX51_JOB:-}',1)[0]
   s=re.sub(r'^D=.*$', 'D='+posix(module),s,count=1,flags=re.M)
   s=re.sub(r'^OUT=.*$', 'OUT='+posix(out),s,count=1,flags=re.M)
   s=re.sub(r'^RUN=.*$', 'RUN='+posix(run),s,count=1,flags=re.M)
   s=re.sub(r'^PAYLOAD=.*$', 'PAYLOAD='+posix(package/'nx-lut-demo'),s,count=1,flags=re.M)
   s=re.sub(r'^hash\(\)\{[^\n]*\}',lambda _:hash_function,s,count=1,flags=re.M)
   # Model executable permission for Linux ELF files on Windows NTFS.
   s=s.replace('[ -x "','[ -f "')
   if kind=='memory':
    s=s.replace('exec /bin/sh "$REARM_SCRIPT" --rearm','printf "DISPATCH_REARM_OK\\n";exit 0')
    s=s.replace('exec /bin/sh "$RESTORE_SCRIPT" --restore-camera','printf "DISPATCH_RESTORE_OK\\n";exit 0')
    for op in ['status','new']:
     s=s.replace('exec "$SESSION" "$OUT" '+op+';;','printf "DISPATCH_'+op.upper()+'_OK\\n";exit 0;;')
    s=s.replace('exec "$SESSION" "$OUT" run "$0" --worker "$@" >> "$BOOT_LOG" 2>&1','printf "DISPATCH_RUN_OK\\n";exit 0')
   else:s=s.replace('exec "$SESSION" "$OUT" rearm "$0" --rearm-worker','printf "DISPATCH_REARM_SUPERVISOR_OK\\n";exit 0')
   return s
  def case(name,kind,args,expected,marker,origin=package):
   script=module/'bootstrap-test.sh';write(script,source(kind,origin))
   for leaf in ['run.log','rearm.log']:
    p=out/'logs'/leaf
    if p.exists():p.unlink()
   result=subprocess.run([SHELL,str(script),*args],env={**os.environ,'PATH':'/usr/bin:/bin'},capture_output=True,text=True,encoding='utf8',errors='replace',timeout=20)
   text=result.stdout+result.stderr
   for leaf in ['run.log','rearm.log']:
    p=out/'logs'/leaf
    if p.exists():text+=p.read_text(encoding='utf8',errors='replace')
   assert result.returncode==expected and marker in text,(name,result.returncode,text)
   records.append(dict(case=name,exit=result.returncode,marker=marker,passed=True))
  case('replay shipped R7g reset failure','memory',['--rearm'],1,'REARM_WRAPPER_IDENTITY_FAILED',W/'r7g-full-v3')
  for args,marker in [(['--rearm'],'REARM'),(['--restore-camera'],'RESTORE'),(['--status'],'STATUS'),(['--new'],'NEW'),([], 'RUN')]:
   case('fixed packaged '+marker.lower()+' dispatch','memory',args,0,'DISPATCH_'+marker+'_OK')
  case('RESET wrapper passes all identities and reaches supervisor','rearm',['--rearm'],0,'DISPATCH_REARM_SUPERVISOR_OK')
  for leaf,args,kind,marker in [
   ('rearm-session.sh',['--rearm'],'memory','REARM_WRAPPER_IDENTITY_FAILED'),
   ('restore-camera.sh',['--restore-camera'],'memory','RESTORE_WRAPPER_IDENTITY_FAILED'),
   ('bin/nxl-session.arm',['--status'],'memory','SESSION_MANAGER_IDENTITY_FAILED'),
   ('bin/nxl-menu',['--rearm'],'rearm','COMPONENT_IDENTITY_FAILED'),
   ('bin/nxl-session-rearm.arm',['--rearm'],'rearm','COMPONENT_IDENTITY_FAILED'),
   ('bin/nxl-holdcheck.arm',['--rearm'],'rearm','COMPONENT_IDENTITY_FAILED')]:
   p=module/leaf;b=p.read_bytes();p.write_bytes(b+b'x')
   try:case('altered '+leaf+' remains rejected',kind,args,1,marker)
   finally:p.write_bytes(b)
 # Audit tests use real final package dependencies, not fixture-generated pins.
 baseline=audit(package);assert baseline['passed'],baseline['failed']
 checks=[];script=package/(M+'memory-test.sh');original=script.read_bytes()
 try:
  for key in ['SESSION_SHA','MENU_SHA','REARM_SCRIPT_SHA','RESTORE_SCRIPT_SHA']:
   s=original.decode();s=re.sub(r'^'+key+r'=[0-9a-f]{64}$',key+'='+'0'*64,s,flags=re.M);script.write_text(s,encoding='utf8',newline='\n')
   assert not audit(package)['passed'];checks.append('bad '+key+' rejected')
  for label,text in [('malformed',re.sub(r'^SESSION_SHA=.*$', 'SESSION_SHA=abc',original.decode(),flags=re.M)),('missing',re.sub(r'^SESSION_SHA=.*\n','',original.decode(),flags=re.M)),('unclassified',original.decode()+'\nUNKNOWN_COMPONENT_SHA='+'0'*64+'\n')]:
   script.write_text(text,encoding='utf8',newline='\n')
   try:audit(package);raise AssertionError(label+' accepted')
   except ValueError:checks.append(label+' declaration rejected')
 finally:script.write_bytes(original)
 assert audit(package)['passed']
 return dict(format='NX_R7H_PACKAGED_BOOTSTRAP_TEST1',passed=True,hardware_tested=False,cases=records,identity_audit_checks=len(baseline['checks']),negative_identity_cases=checks,
  scope='Actual packaged shell identity preambles and real component bytes; host path/hash/exec and NTFS executable-bit endpoints modeled. No camera resource checks or rendering executed.')
if __name__=='__main__':
 result=validate(Path(sys.argv[1]));p=R/'validation/r7h/bootstrap.json';p.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(dict(passed=True,bootstrap_cases=len(result['cases']),identity_checks=result['identity_audit_checks'],negative_identity_cases=len(result['negative_identity_cases']))))
