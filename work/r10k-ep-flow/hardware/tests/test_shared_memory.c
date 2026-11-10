#define _GNU_SOURCE
#define NXL_SMALL_MODEL
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/bitb_job.h"
#include "../src/shared_memory.h"
#include "shared_jpeg_fixture.h"
static unsigned char memory[SW_BYTES],pixels[TEST_FRAME],prefix[SW_CONTROL];
static void hash_bytes(const void*p,unsigned bytes,char out[68]){struct sha256 h;unsigned char d[32];sha256_init(&h);sha256_update(&h,p,bytes);sha256_final(&h,d);digest_hex(d,out);}
int main(void){
 for(unsigned i=0;i<SW_CONTROL;i++)prefix[i]=(unsigned char)(i*7+3);
 memset(memory,0xa5,sizeof memory);memcpy(memory,prefix,sizeof prefix);
 for(unsigned p=0;p<2;p++)for(unsigned y=0;y<TEST_H;y++)for(unsigned x=0;x<TEST_W;x++){
  unsigned char v=(unsigned char)(p*73+y*5+x*3);pixels[p*TEST_Y+y*TEST_W+x]=v;memory[SW_INPUT+p*SW_FRONT_UV+y*SW_FRONT_STRIDE+x]=v;
 }
 struct sw_lease s={0};s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;
 s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;
 s.pa=0x98000000;s.va=0x87000000;s.nonce=777;s.owner_pid=77;s.owner_start_ticks=12345;s.index=(s.pa-0x94000000u)/16384;
 s.metadata[0]=35;s.metadata[1]=SW_BYTES/16384;s.metadata[2]=42;s.metadata[3]=100;s.front_return_barrier=s.frame_callbacks_retired=s.controller_slot_restored=1;
 hash_bytes(prefix,sizeof prefix,s.prefix_sha256);lut_make_table(full_table,1);hash_bytes(full_table,sizeof full_table,s.lut_sha256);memset(s.source_sha256,'c',64);sw_seal(&s);assert(sw_valid(&s));full_workspace_original=s;
 hash_bytes(pixels,sizeof pixels,full_input_sha256);full_input_crc=bitb_crc(pixels,sizeof pixels);strcpy(full_root,"/mnt/mmc/sharedtest");
 assert(sw_transition(&s,SW_FRONT_READY,SW_BACKEND_ACTIVE));memset(memory+SW_LEASE,0,16384);memcpy(memory+SW_LEASE,&s,sizeof s);
 struct bitb_request q={BITB_REQUEST_MAGIC,10,s.pa,SW_BYTES,999,123,s.va,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));struct bitb_result r={0};
 assert(full_begin(FULL_LUT,&q,&r,memory));assert(r.initialized_verified);assert(!memcmp(memory,prefix,sizeof prefix));
 /* A model of identity LUT DMA, never a camera or algorithm verification. */
 memcpy(memory+SW_LUT_OUT,pixels,sizeof pixels);assert(full_finish(FULL_LUT,&q,&r,memory,0));assert(r.source_crc==full_input_crc&&r.target_crc==full_input_crc&&r.guards_ok);
 struct full_manifest bad;unsigned offsets[]={0,SW_INPUT,SW_INPUT+TEST_W,SW_LUT_OUT+TEST_FRAME+1,SW_TABLE+3,SW_LEASE+800};
 for(unsigned k=0;k<sizeof offsets/sizeof *offsets;k++){memory[offsets[k]]^=1;assert(!full_capture(&bad,&q,memory,FULL_LUT,1));memory[offsets[k]]^=1;}
 assert(full_begin(FULL_FINAL,&q,&r,memory));assert(!memcmp(memory+SW_LUT_OUT,pixels,sizeof pixels));assert(!memcmp(memory,prefix,sizeof prefix));
 memcpy(memory+JPEG_OUT,shared_jpeg,sizeof shared_jpeg);memory[JPEG_OUT+4096]^=1;
 assert(full_finish(FULL_FINAL,&q,&r,memory,sizeof shared_jpeg));assert(r.source_crc==full_input_crc&&r.target_ok&&r.guards_ok);assert(full_after.jpeg_nonstream_changed>0);
 for(unsigned k=0;k<sizeof offsets/sizeof *offsets;k++){
  unsigned off=offsets[k];if(off>=JPEG_OUT&&off-JPEG_OUT<JPEG_CAP)continue;
  memory[off]^=1;assert(!full_capture(&bad,&q,memory,FULL_FINAL,1));memory[off]^=1;
 }
 assert(!memcmp(memory+SW_LUT_OUT,pixels,sizeof pixels));assert(!full_prepare_unsafe);
 puts("PASS actual shared memory: padded NV16 direct LUT input, direct JPEG source, protected prefix/lease, source/table/outer guards, allowed JPEG burst tail; no whole-frame handoff copies, no hardware");free(full_pad);return 0;
}
