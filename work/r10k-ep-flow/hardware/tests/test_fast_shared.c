#define _GNU_SOURCE
#define NXL_SMALL_MODEL
#define NXL_FAST_PIPELINE
#include <assert.h>
#include "../src/bitb_job.h"
#include <stdlib.h>
static unsigned cache_fail;
static void*cache_alloc(size_t n){return cache_fail?NULL:malloc(n);}
#define malloc cache_alloc
#include "../src/shared_memory.h"
#undef malloc
#include "shared_jpeg_fixture.h"
static unsigned char memory[SW_BYTES],pixels[TEST_FRAME];
int main(void){
 assert(BITB_WIRE_VERSION==12&&TEST_TRANSLATIONS==2&&test_translation_offset(0)==0&&test_translation_offset(1)==SW_BYTES-16384);
 memset(memory,0xa5,sizeof memory);for(unsigned i=0;i<SW_CONTROL;i++)memory[i]=(unsigned char)(i*7+3);
 for(unsigned p=0;p<2;p++)for(unsigned y=0;y<TEST_H;y++)for(unsigned x=0;x<TEST_W;x++){
  unsigned char v=(unsigned char)(p*73+y*5+x*3);pixels[p*TEST_Y+y*TEST_W+x]=v;memory[SW_INPUT+p*SW_FRONT_UV+y*SW_FRONT_STRIDE+x]=v;
 }
 struct sw_lease s={0};s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;
 s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;
 s.pa=0x98000000;s.va=0x87000000;s.nonce=777;s.owner_pid=getpid();s.owner_start_ticks=12345;s.index=(s.pa-0x94000000u)/16384;
 s.metadata[0]=35;s.metadata[1]=SW_BYTES/16384;s.metadata[2]=42;s.metadata[3]=100;s.front_return_barrier=s.frame_callbacks_retired=s.controller_slot_restored=1;
 full_hash(memory,SW_CONTROL,s.prefix_sha256);lut_make_table(full_table,1);full_hash(full_table,sizeof full_table,s.lut_sha256);memset(s.source_sha256,'c',64);sw_seal(&s);assert(sw_valid(&s));
 assert(sw_owner_proof_publish(&s)&&sw_owner_alive(&s));
 assert(sw_file_new("/tmp/fastmem/shared-workspace.bin",&s,sizeof s));assert(sw_file_new("/tmp/fastmem/srw-lut.bin",full_table,sizeof full_table));
 const char*target="/mnt/mmc/NXDEMO51/JOB_MODEL/jpeg/attempt_1/native.jpg.pending\n";
 assert(sw_file_new("/tmp/fastmem/output-path.txt",target,strlen(target)));
 struct nj_config jc={0};jc.magic=NJ_MAGIC;jc.schema=1;jc.bytes=sizeof jc;jc.sampling=422;jc.jpeg_offset=8;jc.jpeg_bytes=100;memset(jc.quant,1,128);nj_seal(&jc,s.source_sha256);
 struct lp_config lc={0};lc.magic=LP_MAGIC;lc.schema=1;lc.bytes=sizeof lc;lc.key=lp_catalog[0].key;memcpy(lc.source_packet,lp_catalog[0].packet,272);memcpy(lc.profile_key,lp_catalog[0].profile_key,65);memcpy(lc.table_sha256,lp_catalog[0].table_sha,65);lp_seal(&lc,s.source_sha256);
 assert(sw_file_new("/tmp/fastmem/native-jpeg-config.bin",&jc,sizeof jc)&&sw_file_new("/tmp/fastmem/native-ldc-config.bin",&lc,sizeof lc));assert(full_open_inputs("/tmp/fastmem"));
 assert(sw_transition(&s,SW_FRONT_READY,SW_BACKEND_ACTIVE));memset(memory+SW_LEASE,0,16384);memcpy(memory+SW_LEASE,&s,sizeof s);
 struct bitb_request q={BITB_REQUEST_MAGIC,BITB_WIRE_VERSION,s.pa,SW_BYTES,999,123,s.va,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));struct bitb_result r={0};
 assert(!full_begin(FULL_FINAL,&q,&r,memory));assert(!full_begin(FULL_LUT,&q,&r,memory));assert(full_begin(FULL_LDC,&q,&r,memory));assert(full_finish(FULL_LDC,&q,&r,memory,0));assert(full_begin(FULL_LUT,&q,&r,memory));memcpy(memory+SW_LUT_OUT,pixels,sizeof pixels);
 assert(full_finish(FULL_LUT,&q,&r,memory,0)&&r.guards_ok&&r.source_crc==0&&r.target_crc==0);
 unsigned off[]={0,SW_TABLE+3,SW_LEASE+800,fast_lut_tail()+7,fast_input_tail()+9,fast_bayer_tail()+11};
 for(unsigned k=0;k<sizeof off/sizeof *off;k++){memory[off[k]]^=1;assert(!full_finish(FULL_LUT,&q,&r,memory,0));memory[off[k]]^=1;full_prepare_unsafe=0;}
 assert(full_begin(FULL_FINAL,&q,&r,memory));memcpy(memory+JPEG_OUT,shared_jpeg,sizeof shared_jpeg);
 assert(full_finish(FULL_FINAL,&q,&r,memory,sizeof shared_jpeg)&&full_pad!=memory+JPEG_OUT&&full_jpeg_cache_bytes>=sizeof shared_jpeg&&full_pad_bytes==sizeof shared_jpeg&&!memcmp(full_pad,shared_jpeg,sizeof shared_jpeg));
 /* The completed compressed stream is an owned snapshot, released between
  * captures; optional allocation failure keeps the validated DMA alias. */
 memory[JPEG_OUT+30]^=1;assert(!memcmp(full_pad,shared_jpeg,sizeof shared_jpeg));memory[JPEG_OUT+30]^=1;
 full_jpeg_cache_close();full_jpeg_cache_close();assert(!full_pad&&!full_cached_pad&&!full_jpeg_info_valid);
 cache_fail=1;assert(full_jpeg_capture(memory+JPEG_OUT,sizeof shared_jpeg)&&full_pad==memory+JPEG_OUT&&!full_jpeg_cache_bytes);cache_fail=0;
 assert(!full_jpeg_capture(memory+JPEG_OUT,1));full_jpeg_cache_close();
 static unsigned char oversize[16u*1024u*1024u+4];assert(!full_jpeg_capture(oversize,sizeof oversize)&&!full_jpeg_cache_bytes);full_jpeg_cache_close();
 assert(full_jpeg_capture(memory+JPEG_OUT,sizeof shared_jpeg));
 memory[JPEG_OUT+JPEG_CAP+1]^=1;assert(!full_finish(FULL_FINAL,&q,&r,memory,sizeof shared_jpeg));memory[JPEG_OUT+JPEG_CAP+1]^=1;
 assert(!memcmp(memory+SW_LUT_OUT,pixels,sizeof pixels));full_jpeg_cache_close();puts("PASS direct NV16 handoff and all guards; compressed JPEG cache ownership, malloc failure/oversize alias fallback, invalid stream and repeat/free paths; no DMA modeled");return 0;
}
