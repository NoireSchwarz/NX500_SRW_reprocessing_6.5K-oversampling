"""Run the existing production shell with shipped ARM LUT tools on modeled OS.
There is deliberately no invented camera frame or ISP completion signal here.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess,sys
from arm_lut_model import Model

def posix(p):
 s=Path(p).resolve().as_posix();return '/'+s[0].lower()+s[2:]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--repo',type=Path,required=True);ap.add_argument('--work',type=Path,required=True);ap.add_argument('--runtime',type=Path,required=True)
 a=ap.parse_args();root=next(a.work/f'reload-model-{i:02d}' for i in range(1,100) if not (a.work/f'reload-model-{i:02d}').exists());root.mkdir();state=root/'memory';state.mkdir();runtime=root/'runtime';runtime.mkdir();(runtime/'bin').mkdir();(runtime/'state').mkdir()
 source=a.runtime/'lut.sh';code=source.read_text('utf8');tested=code.replace('RUN=/tmp/nx-lut-lite','RUN='+posix(root/'run')).replace('/etc/version.info',posix(root/'version.info'))
 assert tested.count(posix(root/'run'))==1 and tested.count(posix(root/'version.info'))==1
 (runtime/'lut.sh').write_text(tested,encoding='utf8',newline='\n');(root/'version.info').write_text('1.12\nNX500\n',encoding='ascii',newline='\n')
 (runtime/'process.sh').write_text('#!/bin/sh\n# Modeled normal-mode endpoint, not a real /proc test.\nnxl_require_normal_mode() { return 0; }\n',encoding='ascii',newline='\n')
 looks=root/'looks';shutil.copytree(a.work/'deploy/looks',looks)
 table=(a.repo/'work/firmware/P7.bin').read_bytes()[0x380000:0x384000]
 assert struct.unpack_from('<4I',table,0x37f0)==(0x810fd100,0x81101e00,0x81106b00,0x81115200)
 slot=bytearray((i*17+31)&255 for i in range(0x6000)) # Synthetic original slot; not a claim about factory LUT bytes.
 original_slot=bytes(slot);(state/'table.bin').write_bytes(table);(state/'slot.bin').write_bytes(original_slot)
 env=dict(os.environ,PYTHONUTF8='1',PYTHONDONTWRITEBYTECODE='1',NX_LUT_LOOKS=posix(looks),NXLOG_REPO=str(a.repo),NXLOG_MODEL_ROOT=str(root),NXLOG_MODEL_STATE=str(state),NXLOG_BINARY_DIR=str(a.runtime/'bin'),NXLOG_PYTHON=sys.executable.replace('\\','/'),NXLOG_ADAPTER=str(Path(__file__).with_name('arm_lut_model.py')))
 for tool in ['nx_slotwrite','nx_regtabwrite','nxl-tablecheck']:
  (runtime/'bin'/(tool+'.arm')).write_text('#!/bin/sh\nexec "$NXLOG_PYTHON" -B "$NXLOG_ADAPTER" '+tool+' "$@"\n',encoding='ascii',newline='\n')
 env['PATH']=r'C:\Program Files\Git\usr\bin;'+os.environ.get('PATH','');env['MSYS2_ARG_CONV_EXCL']='*'
 records=[]
 def snap():return (state/'table.bin').read_bytes(),(state/'slot.bin').read_bytes()
 def run(*args,success=True):
  done=subprocess.run([r'C:\Program Files\Git\usr\bin\sh.exe',posix(runtime/'lut.sh'),*args],env=env,capture_output=True,text=True,encoding='utf8',timeout=90)
  (root/f'command-{len(records):02d}.txt').write_text(done.stdout+done.stderr,encoding='utf8');records.append({'args':args,'exit':done.returncode,'output':done.stdout+done.stderr})
  assert (done.returncode==0)==success,records[-1]
  print('PASS',*args,'rc='+str(done.returncode),flush=True);return done.stdout+done.stderr
 def want_slot(name):assert (state/'slot.bin').read_bytes()[0xf00:0x5c00]==(looks/(name+'.bin')).read_bytes()
 def want_dyn(redirected):
  tab=(state/'table.bin').read_bytes();now=struct.unpack_from('<24I',tab,0x3a24);stock=struct.unpack_from('<24I',table,0x3a24)
  assert now==((0x81119f00,)*24 if redirected else stock)
 run('apply','NXLOG_REF_V1','100');run('video-on');want_slot('NXLOG_REF_V1');want_dyn(True);first=snap()
 run('apply','NXLOG_TEST_V1','100',success=False);assert snap()==first # Existing active-video-LUT interlock, not a recording-state check.
 run('video-off');want_dyn(False);run('apply','NXLOG_TEST_V1','100');run('video-on');want_slot('NXLOG_TEST_V1');want_dyn(True)
 text=run('status');assert 'VERIFY_CURRENT EXPECTED_MATCH' in text;middle=snap();assert middle!=first
 run('video-off');run('apply','NXLOG_REF_V1','100');run('video-on');assert snap()==first
 run('video-off');run('off');assert snap()==(table,original_slot)
 # Exercise a valid preset through the real shell (including raw LF checksum parsing).
 run('apply','NXLOG_TEST_V1','25');run('video-on');run('video-off');run('off');assert snap()==(table,original_slot)
 # Exact production cache checker, including one-bit corruption.
 for level in [25,50,75]:
  b=looks/'NXLOG_TEST_V1.bin';cache=looks/'nxl-strengths/NXLOG_TEST_V1';checksum=(cache/f'{level}.fnv').read_text().strip()
  args=['check',str(b),str(cache/'source.bin'),str(cache/f'{level}.bin'),checksum]
  rr=Model(a.runtime/'bin/nxl-tablecheck.arm',args,state,root).run();assert rr['exit']==0 and not rr['physical_writes']
 args[-1]='00000000';rr=Model(a.runtime/'bin/nxl-tablecheck.arm',args,state,root).run();assert rr['exit']!=0 and not rr['physical_writes']
 # A stale preset must refuse before begin() or any RAM mutation.
 p=cache/'source.bin';bad=bytearray(p.read_bytes());bad[0]^=1;p.write_bytes(bad)
 before=snap();run('apply','NXLOG_TEST_V1','25',success=False);assert snap()==before
 # A truncated table must also be rejected without touching RAM.
 p=looks/'NXLOG_TEST_V1.bin';p.write_bytes(p.read_bytes()[:-1]);run('apply','NXLOG_TEST_V1','100',success=False);assert snap()==before
 calls=[json.loads(line) for line in (state/'calls.jsonl').read_text('utf8').splitlines()]
 assert calls and not any(at>=0x20000000 and at<0x30000000 for c in calls for at,n in c['physical_writes'])
 # Every production write stays within the established slot or pointer tables.
 ranges=[(0x81119f00,0x8111ec00),(0x803837f0,0x80383800),(0x80382c5c,0x80382c74),(0x80382b00,0x80382b30),(0x80383a24,0x80383a84)]
 for c in calls:
  for at,n in c['physical_writes']:assert any(lo<=at and at+n<=hi for lo,hi in ranges),(c['tool'],hex(at),n)
 result={'passed':True,'commands':records,'shipped_ARM_helper_invocations':len(calls),'helper_hashes':{name:digest(a.runtime/'bin'/name) for name in ['nx_slotwrite.arm','nx_regtabwrite.arm','nxl-tablecheck.arm']},'production_shell_sha256':digest(source),'modeled_RAM_restored_byte_for_byte':True,'A1_A2_tables_identical':True,'active_video_LUT_interlock_tested':True,'corrupt_cache_and_truncated_source_rejected':True,'no_MMIO_writes':True,'modeled_endpoints':['Linux system calls and /dev/mem contents','/etc/version.info','normal-mode process check'],'not_modeled':['ISP LUT loading and frame boundaries','actual recording state','cache coherency','camera watchdog and hibernation'],'hardware_tested':False}
 (a.work/'reload-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps({k:result[k] for k in ['passed','shipped_ARM_helper_invocations','modeled_RAM_restored_byte_for_byte','no_MMIO_writes']}))
if __name__=='__main__':main()
