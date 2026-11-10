#!/usr/bin/env python3
"""Validate a supplied complete SD tree without manufacturing task state.

Only absolute-root routing and corresponding record digests are projected into
an isolated host directory. Runtime process/UI locks are explicitly modeled.
The unchanged ARM post executable is permitted only --version/--verify-input.
"""
from pathlib import Path
import argparse,fcntl,hashlib,json,os,re,shlex,shutil,subprocess,tempfile,time
R=Path(__file__).resolve().parents[2]
WORK=R.parent
LOGICAL=Path('/mnt/mmc/NXDEMO51')
JOB='JOB_1793869199_415_10dc3936'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def files(root):return {str(p.relative_to(root)):sha(p) for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()}
def exe(p,text):p.write_text(text);p.chmod(0o755)
def must(ok,label):
 if not ok:raise AssertionError(label)
def parsed(text):return dict(line.split('=',1) for line in text.splitlines() if '=' in line)
def project_record_file(path,old,new,end):
 original=path.read_text();blocks=original.split(end+'\n');must(blocks[-1]=='','complete record tail '+str(path));changes=[];out=[]
 for raw in blocks[:-1]:
  body,sep,tail=raw.rpartition('sha256=');must(bool(sep) and re.fullmatch('[0-9a-f]{64}\n',tail) is not None,'record digest shape '+str(path));must(hashlib.sha256(body.encode()).hexdigest()==tail.strip(),'original record checksum '+str(path))
  lines=body.splitlines(keepends=True);after=[];fields=[]
  for line in lines:
   projected=line
   if line.startswith(('job=','front=')):
    key,value=line.rstrip('\n').split('=',1)
    if value==old or value.startswith(old+'/'):
     projected=key+'='+new+value[len(old):]+'\n';fields.append({'field':key,'before':value,'after':new+value[len(old):]})
   after.append(projected)
  transformed=''.join(after)
  must(all(x['field'] in ['job','front'] for x in fields),'only path routing may change')
  # Every nonrouting line, including seq/boot/state/risk/attempts, remains exact.
  old_other=[x for x in lines if not x.startswith(('job=','front='))]
  new_other=[x for x in after if not x.startswith(('job=','front='))]
  must(old_other==new_other,'nonrouting fields unchanged')
  out.append(transformed+'sha256='+hashlib.sha256(transformed.encode()).hexdigest()+'\n'+end+'\n');changes.extend(fields)
 path.write_text(''.join(out));return {'original_sha256':hashlib.sha256(original.encode()).hexdigest(),'projected_sha256':sha(path),'routing_fields':changes,'all_other_fields_byte_identical':True}
def validate(source,output):
 source=source.resolve();must(source.name=='NXDEMO51' and source.is_dir(),'input is complete NXDEMO51 directory')
 must(not any(p.is_symlink() for p in source.rglob('*')),'source contains no symlinks')
 initial=files(source)
 archived_events=(source/'logs/repair-original'/JOB/'events.log').read_bytes();old_lines=archived_events.splitlines(keepends=True)
 must(len(old_lines)==6,'complete original six event lines archived')
 must((source/JOB/'events.log').read_bytes()==b''.join(old_lines[:4]),'working event history is exact original four-line prefix')
 must(old_lines[4]==b'seq=2 event=fail state=RETRYABLE stage=idle outcome=SAFE_NO_SUBMIT reason=MOD_TRACE_LOCK_PRESENT\n' and old_lines[5]==b'seq=3 event=fail state=RETRYABLE stage=idle outcome=SAFE_NO_SUBMIT reason=MOD_TRACE_LOCK_PRESENT\n','only two known safe no-submit retry lines retired')
 must((source/JOB/'STATE.journal').is_file(),'preconfigured journal supplied');must((source/'CURRENT.journal').is_file(),'preconfigured current ledger supplied')
 temp=tempfile.TemporaryDirectory(prefix='nx51-real-preconfigured-',dir=WORK);w=Path(temp.name);out=w/'NXDEMO51';shutil.copytree(source,out)
 run=w/'run';mod=w/'mod';payload=w/'payload';proc=w/'proc'
 for p in [run,mod/'bin',mod/'state',mod/'data',payload/'bin',proc/'self']:p.mkdir(parents=True,exist_ok=True)
 (proc/'self/status').write_text('Name:\tsh\n')
 projections={}
 for name,end in [('current.job','end=NX51END1'),('CURRENT.journal','end=NX51PTR4END'),(JOB+'/SESSION.state','end=NX51END1'),(JOB+'/STATE.journal','end=NX51END1')]:
  p=out/name
  if p.exists():projections[name]=project_record_file(p,str(LOGICAL),str(out),end)
 session=mod/'bin/nxl-session.arm';ops=mod/'bin/nxl-fileops'
 subprocess.run(['cc','-O2','-Wno-unused-function','-DHOST_MODEL',str(R/'session/src/session.c'),'-o',str(session)],check=True)
 subprocess.run(['cc','-O2',str(R/'installer/SOURCE/fileops.c'),'-o',str(ops)],check=True)
 calls=w/'calls.jsonl';post=payload/'bin/nxl-demo-post.arm';actual_post=R/'post/bin/nxl-demo-post.arm';qemu=WORK/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static'
 must(qemu.is_file(),'existing ARM emulator');must(sha(actual_post)=='2bd368c3d085c083e94ee0e54a8fbe4633c876065e517be9b8c62d5d712c0a9f','original post identity')
 exe(post,'#!/usr/bin/env python3\nimport sys,os,json\nfrom pathlib import Path\np=Path('+repr(str(calls))+')\nmode=sys.argv[1] if len(sys.argv)>1 else "MISSING"\nwith p.open("a") as f:f.write(json.dumps({"program":"ARM_POST","mode":mode})+"\\n")\nif mode not in ["--version","--verify-input"]:sys.exit(97)\nos.execv('+repr(str(qemu))+',['+repr(str(qemu))+','+repr(str(actual_post))+',*sys.argv[1:]])\n')
 front=payload/'bin/nxl-demo-front.arm'
 exe(front,'#!/usr/bin/env python3\nimport sys,json\nfrom pathlib import Path\nif sys.argv[1:]==["--version"]:print("NXL_DEMO_FRONT=0.3.6-demo50; HOST_DISPATCH_MODEL_ONLY");sys.exit(0)\nwith Path('+repr(str(calls))+').open("a") as f:f.write(json.dumps({"program":"HOST_FRONT_MODEL","report":sys.argv[1]})+"\\n")\nsys.exit(97)\n')
 for name in ['nxl-hw-controller.arm','nxl-hw-worker.arm']:
  exe(payload/'bin'/name,'#!/bin/sh\nprintf \'{"program":"FORBIDDEN_NATIVE","argv":"%s"}\\n\' "$*" >> '+shlex.quote(str(calls))+'\nexit 97\n')
 engine=mod/'lut.sh';exe(engine,'#!/bin/sh\n[ \"$*\" = off ] || exit 97\nprintf \'{"program":"HOST_LUT_OFF_MODEL"}\\n\' >> '+shlex.quote(str(calls))+'\nexit 0\n')
 menu=mod/'bin/nxl-menu';exe(menu,'#!/bin/sh\nprintf \'{"program":"MENU","argv":"%s"}\\n\' "$*" >> '+shlex.quote(str(calls))+'\nexit 0\n')
 (mod/'process.sh').write_text('nxl_require_normal_mode(){ return 0; }\n');(mod/'progress.sh').write_bytes((R/'base/nx-lut-lite/progress.sh').read_bytes())
 manifest=R/'base/nx-lut-lite/data/prior-front-retire.sha256';shutil.copyfile(manifest,mod/'data/prior-front-retire.sha256')
 (mod/'state/p49-experiment.state').write_text('NXP33INSTALL1\nSTARTED=0.3.6-demo50\npid=111\n')
 source_runner=R/'installer/SOURCE/runner.sh.in';text=source_runner.read_text()
 programs={'SESSION':session,'FRONT':payload/'bin/nxl-demo-front.arm','POST':post,'HWCTL':payload/'bin/nxl-hw-controller.arm','HWWORK':payload/'bin/nxl-hw-worker.arm','MENU':menu,'ENGINE':engine}
 for token,p in programs.items():text=text.replace('@DEMO'+('51' if token=='SESSION' else '49')+'_'+token+'_SHA256@',sha(p))
 text=text.replace('@DEMO51_RETIRE_MANIFEST_SHA256@',sha(manifest));text=re.sub(r'^OPS_SHA=[0-9a-f]{64}$','OPS_SHA='+sha(ops),text,flags=re.M)
 for a,b in [('RUN=/tmp/nx-lut-lite','RUN='+str(run)),('OUT=/mnt/mmc/NXDEMO51','OUT='+str(out)),('OLD_OUT=/mnt/mmc/NXDEMO50','OLD_OUT='+str(w/'NXDEMO50')),('PAYLOAD=/mnt/mmc/nx-lut-demo','PAYLOAD='+str(payload)),('PREPARE_PROC=/proc','PREPARE_PROC='+str(proc)),('/tmp/nx-isp-full-capture.lock',str(w/'other-trace.lock')),('/mnt/mmc/NXISP_FULL_ARM',str(w/'other-arm'))]:text=text.replace(a,b)
 must('@DEMO' not in text,'all identity tokens bound');runner=mod/'memory-test.sh';exe(runner,text)
 env=dict(os.environ,NX51_TEST_BOOT_ID='UNKNOWN',NXL_PROGRESS_SD=str(w));job=out/JOB
 for name in ['engine.lock','trace.lock']:
  p=run/name;p.mkdir();(p/'NX51_OWNER').write_text('NX51LOCK1\njob='+str(job)+'\nboot=UNKNOWN\n');(p/'RESTART_REQUIRED').write_text('NX51HOLD1\njob='+str(job)+'\nboot=UNKNOWN\nreason=FRONT_RESTART_OR_PATH_INVALID\n')
 held=[];ui_original={}
 try:
  for name in ['keys.lock','menu.lock','memory-ui.lock','trace-ui.lock']:
   p=run/name;f=p.open('w');f.write('MODELED_UI_FLOCK\n');f.flush();fcntl.flock(f.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB);held.append(f);ui_original[name]=(p.stat().st_ino,p.read_bytes())
  p=subprocess.run([str(session),str(out),'status'],env=env,capture_output=True,text=True);must(p.returncode==0,'supplied tree status readable: '+p.stderr);before=parsed(p.stdout)
  must(before['state']=='RESTART_REQUIRED' and before['reason']=='FRONT_RESTART_OR_PATH_INVALID','supplied held state preserved');must(before['front']=='-' and before['front_verified']=='0' and before['front_attempt']=='1' and before['backend_attempt']=='0' and before['requires_reset']=='1' and before['boot']=='UNKNOWN','supplied safety/attempt fields preserved')
  before_native=files(job/'front_1/front');start=time.monotonic();p=subprocess.run(['/bin/sh',str(runner),'--prepare-full'],env=env,capture_output=True,text=True,timeout=90);elapsed=time.monotonic()-start
  failure=(job/'RESULT.txt').read_text() if (job/'RESULT.txt').exists() else p.stderr
  must(p.returncode==0,'original R4 prepare-full succeeds: '+failure)
  s=subprocess.run([str(session),str(out),'status'],env=env,capture_output=True,text=True);must(s.returncode==0,'final status readable');after=parsed(s.stdout)
  for k,v in {'state':'NEED_STILL_INIT','front':'-','front_verified':'0','front_attempt':'1','backend_attempt':'0','requires_reset':'0','reason':'FULL_TEST_PREPARED_NO_REUSE'}.items():must(after[k]==v,'postcondition '+k)
  time.sleep(.1);seen=[json.loads(x) for x in calls.read_text().splitlines()];must(not any(x['program'].startswith(('FORBIDDEN','HOST_FRONT','HOST_LUT')) for x in seen),'zero native/LUT/model submissions during proof preparation');must(sum(x['program']=='ARM_POST' and x['mode']=='--verify-input' for x in seen)==1,'exact original ARM full verification');must((job/'input-verify.stdout.txt').read_text()=='RAW422_POST_INPUT_VERIFIED\n','actual ARM POST success text');must(not (job/'jpeg').exists(),'no JPEG attempt');must(not (job/'front_2').exists(),'no automatic new front');must(files(job/'front_1/front')==before_native,'all old native data unchanged')
  must(all(not (run/n).exists() for n in ['engine.lock','trace.lock']),'only own exact holds retired')
  for name,(inode,data) in ui_original.items():must((run/name).stat().st_ino==inode and (run/name).read_bytes()==data,'UI flock inode/data preserved')
  prepare_seen=list(seen)
  demo=subprocess.run(['/bin/sh',str(runner)],env=env,capture_output=True,text=True,timeout=30);must(demo.returncode!=0,'host front stub stops intentionally without hardware')
  ds=subprocess.run([str(session),str(out),'status'],env=env,capture_output=True,text=True);must(ds.returncode==0,'host dispatch status readable');demo_status=parsed(ds.stdout);must(demo_status['front_attempt']=='2' and demo_status['backend_attempt']=='0' and demo_status['front_verified']=='0' and demo_status['front']=='-','fresh front2 scheduled without previous pixel import')
  seen=[json.loads(x) for x in calls.read_text().splitlines()];demo_seen=seen[len(prepare_seen):];front_calls=[x for x in demo_seen if x['program']=='HOST_FRONT_MODEL'];must(len(front_calls)==1 and front_calls[0]['report']==str(job/'front_2/front'),'exact fresh front2 dispatch');must(not any(x['program']=='ARM_POST' and x['mode']!='--version' for x in demo_seen),'no old or new planes sent to JPEG');must(not any(x['program'].startswith('FORBIDDEN') for x in seen),'no physical controller/helper calls');must(not (job/'jpeg').exists(),'host stub never starts JPEG');must(files(job/'front_1/front')==before_native,'old native files unchanged after host scheduling');must(files(source)==initial,'supplied preconfigured directory unchanged')
  evidence={'format':'NX51_PRECONFIGURED_TREE_R4_VALIDATION','passed':True,'host_control_model':True,'actual_ARM_POST_verify_input':True,'hardware_tested':False,'native_submissions':0,'source_directory':str(source),'source_file_sha256':initial,'routing_projection':projections,'all_nonrouting_control_fields_unchanged':True,'native49_bytes_unchanged':True,'archived_original_events_sha256':hashlib.sha256(archived_events).hexdigest(),'working_events_exact_original_prefix':True,'two_safe_no_submit_retry_lines_archived':True,'before_status':before,'after_status':after,'worker_exit':p.returncode,'elapsed_seconds':elapsed,'prepare_calls':prepare_seen,'host_front2_scheduling':{'executed':True,'hardware_executed':False,'intentional_stub_exit':demo.returncode,'status':demo_status,'calls':demo_seen,'JPEG_attempted':False,'old_planes_used_for_JPEG':False},'runtime_model':{'processes':'normal mode, no native workers','holds':'exact known own false holds; not read from SD archive','UI_flocks':'four real held regular flock files; unchanged','installed_front_grant':'STARTED -> READY after proof'},'next_native_action':'manual initialization then PREPARE RAW; host dispatch to front2 demonstrated only; actual SRW decode/ISP/JPEG hardware not executed by this verifier','source_sha256':{str(p.relative_to(R)):sha(p) for p in [source_runner,R/'session/src/session.c',R/'session/src/journal.inc',R/'installer/SOURCE/fileops.c',actual_post,Path(__file__)]},'post_binary_sha256':sha(actual_post),'retirement_manifest_sha256':sha(manifest),'stdout':p.stdout,'stderr':p.stderr}
  output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(evidence,indent=2)+'\n');print(json.dumps({'passed':True,'worker_exit':p.returncode,'native_submissions':0,'ARM_POST_verify':True,'state':after['state'],'front':after['front'],'elapsed_seconds':elapsed,'evidence':str(output)},indent=2))
 finally:
  for f in held:f.close()
  temp.cleanup()
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--input',required=True,type=Path);ap.add_argument('--output',required=True,type=Path);a=ap.parse_args();validate(a.input,a.output)
