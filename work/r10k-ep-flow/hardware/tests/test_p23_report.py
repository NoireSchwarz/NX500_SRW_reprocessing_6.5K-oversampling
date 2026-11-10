#!/usr/bin/env python3
"""Offline full-size modeled report and tampering checks; never hardware evidence."""
import argparse,csv,json,shutil,struct,sys,tempfile,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
import analyze_p23_report as a
from p20_steps import OFFSETS,FIELDS
L={f['name']:f for f in a.LAYOUT}
def setfield(b,k,v):
 f=L[k];values=v if isinstance(v,list) else [v];struct.pack_into('<'+('i' if f['signed'] else 'I')*f['count'],b,f['offset'],*values)
def finish(b):struct.pack_into('<I',b,len(b)-4,zlib.crc32(b[:-4]));return b
def fixture(model,baseline,target):
 shutil.copytree(model,target);proof=bytearray((target/'p23_pipeline.bin').read_bytes());pa,alias=struct.unpack_from('<2I',proof,16);q=struct.pack('<8I',0x3751424e,10,pa,a.N,12345,99,0x91000000,0);q+=struct.pack('<I',zlib.crc32(q));(target/'copy_request.bin').write_bytes(q)
 owner=(target/'p11_rtos_irq_before.bin').read_bytes();(target/'p11_rtos_irq_after.bin').write_bytes(owner);(target/'p11_kernel_after.bin').write_bytes((target/'p11_kernel_before.bin').read_bytes())
 guards=json.loads((ROOT/'p13_irq_guards.json').read_text());(target/'p13_rtos_irq_guards.bin').write_bytes(b''.join(struct.pack('<9I',x['address'],*x['words']) for x in guards))
 offsets={};p=0
 for key,n in FIELDS:offsets[key]=p;p+=n
 for i,name in enumerate(a.DIRS):
  d=target/name;s=bytearray((d/'result.bin').read_bytes());r=a.decode(s)
  if i==1:
   sb=bytearray(baseline.read_bytes());oldpa=struct.unpack_from('<I',sb,16+5*344+51*4)[0]-0x2000
   assert struct.unpack_from('<I',sb,8)[0]==19;struct.pack_into('<I',sb,8,18);sb[16+18*344:16+19*344]=bytes(344)
   for n in range(18):
    pos=16+n*344;words=list(struct.unpack_from('<86I',sb,pos))
    for off,delta in [(0x1400,0),(0x1404,a.Y),(0x1700,a.LUTOUT),(0x1704,a.LUTOUT+a.Y)]:
     k=2+OFFSETS.index(off)
     if words[k]:words[k]=pa+delta
    for off in [0x1424,0x1724]:
     k=2+OFFSETS.index(off)
     if words[k]:words[k]=(a.W<<16)|a.H
    if words[51]:words[51]=pa+a.TABLE
    if words[offsets['last_dma_pa']]:words[offsets['last_dma_pa']]=pa+a.TABLE;words[offsets['last_dma_va']]=alias+a.TABLE
    struct.pack_into('<86I',sb,pos,*words)
   finish(sb)
  else:
   j=dict(zip(a.META,struct.unpack('<19I',(d/'jpeg_meta.bin').read_bytes())));sb=bytearray(1680);struct.pack_into('<3I',sb,0,0x31534a4e,1,7);source=a.LUTOUT if i==2 else 0
   for n,ident in enumerate([101,201,301,302,401,501,999]):
    row=[0]*26;row[0]=ident;row[1]=n;row[6]=1;row[17]=j['old_selector'];row[4]=row[15]=0x24 if ident in [301,302,401,501] else 0
    if ident in [302,401,501]:row[17]=5;row[12:15]=[pa+source,pa+source+a.Y,pa+a.OUT];row[10]=j['stream_bits']
    struct.pack_into('<26I',sb,12+n*104,*row)
   finish(sb)
  (d/'steps.bin').write_bytes(sb);setfield(s,'route_crc',a.word(sb,len(sb)-4));setfield(s,'route_samples',18 if i==1 else 7);finish(s);(d/'result.bin').write_bytes(s);proof[60+i*636:60+(i+1)*636]=s
 finish(proof);(target/'p23_pipeline.bin').write_bytes(proof);worker=bytearray(proof[60:696]);stage=[a.decode(proof[60+i*636:60+(i+1)*636]) for i in range(3)]
 for k,v in {'engine_jobs':3,'lut_hardware_jobs':1,'table_operations':1,'table_completions':1,'local_reset_calls':6,'translations':3*a.PAGES,'matches':3*a.PAGES,'route_samples':3,'route_crc':a.word(proof,2140),'pixel_bytes':stage[0]['pixel_bytes']+stage[2]['pixel_bytes']}.items():setfield(worker,k,v)
 finish(worker);(target/'copy_worker.bin').write_bytes(worker)
 root={k:1 for k in ['ownership_verified','request_returned','release_returned','allocator_return_verified','original_code_restored','detached','copy_result_valid','copy_safe_to_release','copy_rehearsal_verified','lut_jpeg_pipeline_verified','idle_registers_verified']}
 root.update(format='NXPIPE1',version='0.3.4-p23',exit=0,reason='NATIVE_LUT_JPEG_PIPELINE_VERIFIED_RESTART_REQUIRED',camera_pauses=2,camera_resumes=2,camera_resumed_verified=2,camera_stopped_at_exit=0,copy_memory_quarantined=0,api_inflight_at_exit=0,allocator_index=99,free_before=26396,free_after=26396-a.PAGES,pa=pa)
 (target/'result.json').write_text(json.dumps(root));(target/'probe.log').write_text('OFFLINE MODEL ONLY\nRELEASE_WAIT_AFTER_DETACH\n')
 with (target/'allocator.csv').open('w') as f:
  wr=csv.writer(f);wr.writerow(['stage','stable','index','pa','entry_word0','span_blocks'])
  for _ in range(3):wr.writerow(['release_wait',1,99,pa,0,0])
 (target/'p21a_preflight.txt').write_text('P21A_PREFLIGHT_V1; OFFLINE MODEL ONLY\nGATE_RESULT type=STATE call=1 pass=1 reason=PASS round=0 index=0 address=0 expected=0 actual=0 errno=0\nGATE_RESULT type=IDLE call=1 pass=1 reason=PASS round=16 index=0 address=0 expected=0 actual=0 errno=0\nGATE_SUMMARY state_calls=1 idle_calls=1 rejections=0\n');return target

def main():
 p=argparse.ArgumentParser();p.add_argument('model',type=Path);p.add_argument('p20_steps',type=Path);p.add_argument('--output',type=Path);args=p.parse_args()
 with tempfile.TemporaryDirectory() as td:
  td=Path(td);base=fixture(args.model,args.p20_steps,td/'positive');r=a.analyze(base);assert r['verdict']=='NATIVE_LUT_TO_JPEG_PIPELINE_VERIFIED',[k for k,v in r['checks'].items() if not v]
  mutations=[('01_reference/memory_before.bin',60,True),('02_lut/memory_after.bin',88,True),('03_lut_jpeg/memory_before.bin',100,True),('03_lut_jpeg/memory_after.bin',len((base/'03_lut_jpeg/memory_after.bin').read_bytes())-8,True),('03_lut_jpeg/padded_stream.bin',25,False),('p23_lut.jpg',20,False),('p23_pipeline.bin',60,False),('02_lut/steps.bin',20,False),('p11_kernel_after.bin',20,False),('03_lut_jpeg/kernel_after.bin',20,False),('01_reference/native_packet.bin',40,False),('03_lut_jpeg/jpeg_meta.bin',20,False)]
  for i,(name,off,recrc) in enumerate(mutations):
   path=base/name;original=path.read_bytes();b=bytearray(original);b[off]^=1
   if recrc:finish(b)
   path.write_bytes(b)
   try:
    try:bad=a.analyze(base);assert bad['verdict']!='NATIVE_LUT_TO_JPEG_PIPELINE_VERIFIED',name
    except (OSError,ValueError,KeyError,struct.error):pass
   finally:path.write_bytes(original)
  for key in ['exit','release_returned','camera_stopped_at_exit','copy_memory_quarantined','lut_jpeg_pipeline_verified']:
   f=base/'result.json';original=f.read_text();root=json.loads(original);root[key]=0 if root[key] else 1;f.write_text(json.dumps(root));assert a.analyze(base)['verdict']=='NOT_VERIFIED';f.write_text(original)
  result={'passed':18,'positive_checks':len(r['checks']),'details':r['details'],'hardware_verified':False,'scope':__doc__};print(json.dumps(result,indent=2))
 if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
