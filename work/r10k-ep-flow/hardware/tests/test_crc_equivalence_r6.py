#!/usr/bin/env python3
"""Compile frozen R5 vs R6 and compare exact full192MiB manifest bytes."""
from pathlib import Path
import hashlib,json,subprocess,tempfile,shutil,time
R=Path(__file__).resolve().parents[1];W=R.parents[1];OLD=W/'demo-p52-restore-r5/hardware';FIELD=W/'demo51-field-success/JOB_1793869199_415_10dc3936/jpeg/attempt_1/hardware'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
PROGRAM=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "bitb_job.h"
#include "full_memory.h"
static const char *out;
static void manifest(const char *name,const struct full_manifest *m){char p[1200];snprintf(p,sizeof p,"%s/%s",out,name);FILE *f=fopen(p,"wb");assert(f&&fwrite(m,1,sizeof *m,f)==sizeof *m&&!fclose(f));}
static void damage(const char *name,unsigned at,int pad,struct bitb_request *q,unsigned char *m){struct full_manifest s;m[at]^=1;assert(!full_capture(&s,q,m,0,pad?full_pad:NULL,pad?full_pad_bytes:0));assert(s.errors==1&&s.first_bad==at);manifest(name,&s);m[at]^=1;}
int main(int argc,char **argv){
 assert(argc==5);out=argv[4];assert(full_open_inputs(argv[1]));unsigned char *m=malloc(TEST_BYTES);assert(m);struct bitb_request q={BITB_REQUEST_MAGIC,10,0x94000000,TEST_BYTES,123456,123,0x64000000,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));struct bitb_result r={0};
 assert(full_begin(FULL_REFERENCE,&q,&r,m)&&full_verify(&full_before,&q,0,NULL,0));manifest("before-A.bin",&full_before);
 damage("source-first.bin",0,0,&q,m);damage("source-last.bin",TEST_FRAME-1,0,&q,m);damage("partial-source-guard.bin",TEST_FRAME,0,&q,m);damage("last-guard.bin",TEST_BYTES-1,0,&q,m);
 FILE *f=fopen(argv[3],"rb");assert(f);fseek(f,0,SEEK_END);unsigned n=ftell(f);rewind(f);assert(fread(m+JPEG_OUT,1,n,f)==n&&!fclose(f));assert(full_finish(FULL_REFERENCE,&q,&r,m,n)&&full_verify(&full_after,&q,0,full_pad,full_pad_bytes));manifest("after-JPEG.bin",&full_after);
 damage("warm-source-first.bin",0,1,&q,m);damage("warm-tail-guard.bin",TEST_BYTES-1,1,&q,m);
 struct full_manifest changed;m[JPEG_OUT+2*1024*1024+7]^=1;assert(full_capture(&changed,&q,m,0,full_pad,full_pad_bytes));assert(changed.jpeg_nonstream_changed==1);manifest("warm-JPEG-reserved-change.bin",&changed);m[JPEG_OUT+2*1024*1024+7]^=1;
 // A cached-page CRC must not conceal malformed stream content.
 m[JPEG_OUT]=0;assert(!full_finish(FULL_REFERENCE,&q,&r,m,n));manifest("malformed-stream.bin",&full_after);
 q.nonce=987654321;q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));full_initialized=full_io_error=full_prepare_unsafe=0;assert(full_begin(FULL_REFERENCE,&q,&r,m)&&full_verify(&full_before,&q,0,NULL,0));manifest("new-nonce.bin",&full_before);
 assert(full_open_inputs(argv[2]));full_initialized=full_io_error=full_prepare_unsafe=0;assert(full_begin(FULL_REFERENCE,&q,&r,m)&&full_verify(&full_before,&q,0,NULL,0));manifest("new-input-same-request.bin",&full_before);
 q.pa=0x94400000;q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));full_initialized=full_io_error=full_prepare_unsafe=0;assert(full_begin(FULL_REFERENCE,&q,&r,m)&&full_verify(&full_before,&q,0,NULL,0));manifest("new-request-PA.bin",&full_before);
 struct full_manifest forged=full_before;forged.blocks[TEST_PAGES-1]^=1;forged.crc=bitb_crc(&forged,offsetof(struct full_manifest,crc));assert(!full_verify(&forged,&q,0,NULL,0));manifest("forged-manifest.bin",&forged);
 assert(full_before.checked_bytes==201326592u&&TEST_PAGES==12288u);free(m);return 0;
}
'''
def main():
 started=time.monotonic();report=[]
 with tempfile.TemporaryDirectory(prefix='r6-crc-equivalence-',dir=W) as td:
  d=Path(td);alternate=d/'alternate';alternate.mkdir();data=bytearray((FIELD/'input422.bin').read_bytes());data[0]^=1;data[-1]^=7;(alternate/'input422.bin').write_bytes(data);h=sha(alternate/'input422.bin');(alternate/'input.identity').write_text(f'NX422INPUT1 {len(data)} {h}\n');del data
  c=d/'model.c';c.write_text(PROGRAM);outputs=[]
  for label,base in [('old-R5',OLD),('new-R6',R)]:
   exe=d/label;out=d/(label+'-out');out.mkdir();subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation','-I',str(base/'src'),str(c),'-o',str(exe)],check=True)
   before=time.monotonic();p=subprocess.run([str(exe),str(FIELD),str(alternate),str(FIELD/'native-actual.jpg'),str(out)],check=True,capture_output=True,text=True,timeout=180);report.append({'variant':label,'host_seconds':time.monotonic()-before,'stdout':p.stdout});outputs.append({p.name:p.read_bytes() for p in out.iterdir()})
  assert outputs[0].keys()==outputs[1].keys()
  for name in outputs[0]:assert outputs[0][name]==outputs[1][name],name
  cases={name:hashlib.sha256(data).hexdigest() for name,data in outputs[1].items()};assert len(cases)==14
 result={'format':'NX51_R6_EXACT_MANIFEST_EQUIVALENCE1','passed':True,'hardware_tested':False,'tests_run':len(cases),'exact_manifest_byte_equality':True,'full_physical_bytes_per_audit':201326592,'pages':12288,'cases_sha256':cases,'variants':report,'elapsed_seconds':time.monotonic()-started,'source_sha256':{str(p.relative_to(W/'demo-p52-timing-r6')):sha(p) for p in [R/'src/full_memory.h',R/'src/r6_timing.h',Path(__file__)]},'frozen_R5_full_memory_sha256':sha(OLD/'src/full_memory.h'),'input_sha256':sha(FIELD/'input422.bin'),'JPEG422_sha256':sha(FIELD/'native-actual.jpg'),'limits':['Host runtime durations are not camera speed predictions.','Physical RAM/DMA is modeled with host allocated192MiB; actual camera integration remains untested.']}
 (R/'validation/r6-crc-equivalence.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
