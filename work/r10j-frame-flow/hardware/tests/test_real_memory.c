#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../src/bitb_job.h"
#include "../src/full_memory.h"
int main(int argc,char **argv){
 assert(argc==3);assert(full_open_inputs(argv[1]));
 struct bitb_request q={BITB_REQUEST_MAGIC,10,0x94000000,TEST_BYTES,123456,123,0x64000000,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));assert(bitb_request_valid(&q));
 struct bitb_result r={0};unsigned char *m=malloc(TEST_BYTES);assert(m);
 FILE *j=fopen(argv[2],"rb");assert(j);fseek(j,0,SEEK_END);unsigned n=ftell(j);rewind(j);unsigned char *jpg=malloc(n);assert(jpg&&fread(jpg,1,n,j)==n);fclose(j);assert(jpeg_parse(jpg,n));
 assert(full_begin(FULL_REFERENCE,&q,&r,m));assert(r.initialized_verified);
 memcpy(m+TEST_JPEG_OUT,jpg,n);
 // Real field04: native DMA padding crossed the stream's old512B rounding.
 unsigned rounded=(n+511u)&~511u;m[TEST_JPEG_OUT+rounded+9]^=1;
 m[TEST_JPEG_OUT+JPEG_CAP-1]^=1; // last owned output byte is allowed
 assert(full_finish(FULL_REFERENCE,&q,&r,m,n));assert(full_verify(&full_after,&q,0,full_pad,full_pad_bytes));
 assert(!full_after.errors&&full_after.jpeg_reserved_bytes==JPEG_CAP&&full_after.jpeg_write_end==JPEG_CAP&&full_after.jpeg_nonstream_changed==2);
 struct full_manifest boundary;
 m[TEST_JPEG_OUT-1]^=1;assert(!full_capture(&boundary,&q,m,0,full_pad,full_pad_bytes));assert(boundary.first_bad==TEST_JPEG_OUT-1);m[TEST_JPEG_OUT-1]^=1;
 m[TEST_JPEG_OUT+JPEG_CAP]^=1;assert(!full_capture(&boundary,&q,m,0,full_pad,full_pad_bytes));assert(boundary.first_bad==TEST_JPEG_OUT+JPEG_CAP);m[TEST_JPEG_OUT+JPEG_CAP]^=1;

 assert(full_begin(FULL_LUT,&q,&r,m));
 assert(m[TEST_JPEG_OUT+rounded+9]==full_period[(TEST_JPEG_OUT+rounded+9)&32767u]);
 assert(m[TEST_JPEG_OUT+JPEG_CAP-1]==full_period[(TEST_JPEG_OUT+JPEG_CAP-1)&32767u]);
 // Inject modeled output, deliberately not a software LUT reference.
 for(unsigned i=0;i<TEST_FRAME;i++)m[TEST_OUT+i]=m[i]^0x33;
 assert(full_finish(FULL_LUT,&q,&r,m,0));assert(full_verify(&full_after,&q,1,NULL,0));
 struct full_manifest good=full_after,bad=good;bad.blocks[TEST_PAGES/2]^=1;bad.crc=bitb_crc(&bad,offsetof(struct full_manifest,crc));assert(!full_verify(&bad,&q,1,NULL,0));
 // Guard corruption and source corruption each fail even with correct output.
 m[TEST_TABLE-1]^=1;assert(!full_capture(&bad,&q,m,1,NULL,0));m[TEST_TABLE-1]^=1;
 m[0]^=1;assert(!full_capture(&bad,&q,m,1,NULL,0));m[0]^=1;
 assert(full_begin(FULL_FINAL,&q,&r,m));assert(!memcmp(full_before.blocks,full_lut_after.blocks,sizeof full_before.blocks));
 memcpy(m+TEST_JPEG_OUT,jpg,n);assert(full_finish(FULL_FINAL,&q,&r,m,n));assert(full_verify(&full_after,&q,1,full_pad,full_pad_bytes));
 printf("PASS real-input exact192MiB guards, full3stage handoff, source/table corruption rejection, JPEGpadding/last-owned-byte accepted, immediateoutsidebytes rejected, fullJPEGreset beforeLUT, forgednonJPEGmanifest rejection; MODEL DMA ONLY\n");free(jpg);free(m);return 0;
}
