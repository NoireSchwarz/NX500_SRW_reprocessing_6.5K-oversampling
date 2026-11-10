#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/bitb_job.h"
#include "../src/full_memory.h"
int main(int argc,char **argv){
 assert(argc==4&&full_open_inputs(argv[1]));
 struct bitb_request q={BITB_REQUEST_MAGIC,10,0x94000000,TEST_BYTES,123456,123,0x64000000,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));assert(bitb_request_valid(&q));
 struct bitb_result r={0};unsigned char *m=malloc(TEST_BYTES);assert(m);
 FILE *f=fopen(argv[2],"rb");assert(f);fseek(f,0,SEEK_END);unsigned n=ftell(f);rewind(f);unsigned char *jpg=malloc(n);assert(jpg&&fread(jpg,1,n,f)==n);fclose(f);
 struct jpeg_info info;assert(jpeg_parse_info(jpg,n,&info)&&info.format==422&&jpeg_parse(jpg,n));
 f=fopen(argv[3],"rb");assert(f);fseek(f,0,SEEK_END);unsigned oldn=ftell(f);rewind(f);unsigned char *old=malloc(oldn);assert(old&&fread(old,1,oldn,f)==oldn);fclose(f);
 assert(jpeg_parse_info(old,oldn,&info)&&info.format==420&&!jpeg_parse(old,oldn));
 assert(full_begin(FULL_REFERENCE,&q,&r,m));assert(full_verify(&full_before,&q,0,NULL,0));
 memcpy(m+JPEG_OUT,jpg,n);unsigned rounded=(n+511u)&~511u;m[JPEG_OUT+rounded+8]^=1;m[JPEG_OUT+JPEG_CAP-1]^=1;
 assert(full_finish(FULL_REFERENCE,&q,&r,m,n));assert(!full_prepare_unsafe&&r.guards_ok&&r.source_ok&&r.target_ok);
 assert(full_after.jpeg_reserved_bytes==JPEG_CAP&&full_after.jpeg_write_end==JPEG_CAP&&full_after.jpeg_nonstream_changed==2);
 assert(full_verify(&full_after,&q,0,full_pad,full_pad_bytes));
 struct full_manifest bad;unsigned boundary[]={0,TEST_FRAME-1,JPEG_OUT-1,JPEG_OUT+JPEG_CAP,TEST_BYTES-1};
 for(unsigned k=0;k<5;k++){m[boundary[k]]^=1;assert(!full_capture(&bad,&q,m,0,full_pad,full_pad_bytes));assert(bad.first_bad==boundary[k]);m[boundary[k]]^=1;}
 bad=full_after;bad.blocks[0]^=1;bad.crc=bitb_crc(&bad,offsetof(struct full_manifest,crc));assert(!full_verify(&bad,&q,0,full_pad,full_pad_bytes));
 // Candidate mismatch is validation failure, not an ownership failure.
 memcpy(m+JPEG_OUT,old,oldn);assert(!full_finish(FULL_REFERENCE,&q,&r,m,oldn));assert(!full_prepare_unsafe&&r.guards_ok&&r.source_ok&&!r.target_ok);
 assert(jpeg_parse_info(full_pad,oldn,&info)&&info.format==420);
 free(old);free(jpg);free(m);puts("PASS NV16 exactsource, strict422parser/420rejection, ownedJPEGpadding+lastbyte, 5 outside/source guard failures, forgednonJPEGmanifest, safeSOFmismatch; modeled DMA");return 0;
}
