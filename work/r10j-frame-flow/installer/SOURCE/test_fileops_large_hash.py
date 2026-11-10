#!/usr/bin/env python3
"""Exercise bounded streaming digests with real file bytes; no camera devices."""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile
R=Path(__file__).resolve().parents[2];W=R.parent
SOURCE=R/'installer/SOURCE/fileops.c';ARM=R/'installer/patch-fileops'
QEMU=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def main():
 cases=[]
 with tempfile.TemporaryDirectory(prefix='nx51-r4-fileops-hash-') as td:
  d=Path(td);host=d/'fileops.host'
  subprocess.run(['cc','-O2',str(SOURCE),'-o',str(host)],check=True)
  large=d/'real-32MiB.dat';block=bytes(range(256))*32
  with large.open('wb') as f:
   for _ in range((32*1024*1024)//len(block)):f.write(block)
  expected=sha(large)
  edge=d/'exact-128MiB.dat'
  with edge.open('wb') as f:f.truncate(128*1024*1024)
  edgehash=sha(edge)
  over=d/'over-128MiB.dat'
  with over.open('wb') as f:f.truncate(128*1024*1024+1)
  link=d/'linked.dat';link.symlink_to(large);fifo=d/'pipe';os.mkfifo(fifo)
  def run(label,cmd,path,rc,digest=None):
   p=subprocess.run(cmd+['sha256',str(path)],capture_output=True,text=True,timeout=45)
   assert p.returncode==rc,(label,p.returncode,p.stdout,p.stderr)
   assert p.stdout==(digest+'\n' if digest else ''),(label,p.stdout)
   cases.append({'case':label,'passed':True,'return':p.returncode,'bytes':path.stat().st_size if path.is_file() else None,'digest':digest})
  run('host_real_32MiB_stream', [str(host)],large,0,expected)
  run('host_exact_128MiB_boundary',[str(host)],edge,0,edgehash)
  run('host_over_limit_rejected',[str(host)],over,2)
  run('host_symlink_rejected',[str(host)],link,2)
  run('host_directory_rejected',[str(host)],d,2)
  run('host_FIFO_rejected_without_open',[str(host)],fifo,2)
  run('actual_ARM_real_32MiB_stream',[str(QEMU),str(ARM)],large,0,expected)
  run('actual_ARM_over_limit_rejected',[str(QEMU),str(ARM)],over,2)
  run('actual_ARM_symlink_rejected',[str(QEMU),str(ARM)],link,2)
  field=W/'demo-p50/field-report-02/DEMO_20261105_141315_391/front/case_00/p39_output.y8'
  assert field.is_file() and field.stat().st_size==28062720
  run('actual_ARM_real_camera_luma_plane',[str(QEMU),str(ARM)],field,0,sha(field))
 assert 'unsigned char buf[8192],digest[32]' in SOURCE.read_text()
 result={'format':'NX51_FILEOPS_LARGE_HASH_R4_TESTS1','passed':True,'tests_run':len(cases),'hardware_tested':False,'actual_ARM_execution':True,'max_regular_hash_bytes':128*1024*1024,'stream_buffer_bytes':8192,'cases':cases,'source_sha256':sha(SOURCE),'sha_header_sha256':sha(R/'installer/SOURCE/src/sha256.h'),'binary_sha256':sha(ARM),'test_sha256':sha(Path(__file__))}
 (R/'validation/fileops-large-hash-tests.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
