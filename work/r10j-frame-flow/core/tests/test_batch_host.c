/* Tests actual Linux predicates against production-ARM-generated prefixes.
 * Native task/DMA in that generator are models; no camera accesses here. */
#define _GNU_SOURCE
#define main production_main_not_run
#include "nxl-native-front.c"
#undef main
#include <assert.h>
static unsigned char memory[65536],previous_saved[128],fixed[256];
static void load(const char *path,void *out,size_t n){FILE *f=fopen(path,"rb");assert(f&&fread(out,1,n,f)==n&&fgetc(f)==EOF&&!fclose(f));}
static int has(const char *path,const char *needle){FILE *f=fopen(path,"rb");assert(f);char text[65536];size_t n=fread(text,1,sizeof text-1,f);text[n]=0;assert(!fclose(f));return strstr(text,needle)!=NULL;}
int main(int argc,char **argv){
 assert(argc==3);unsigned rejects=0;batch_directory=argv[2];started=ns();
 for(unsigned i=0;i<10;i++){
  char path[1200],selector[2]={(char)('0'+i),0},out[1000];assert(p48_select_case(selector));snprintf(path,sizeof path,"%s/frame-%02u-prefix.bin",argv[1],i+1);load(path,memory,sizeof memory);
  uint32_t *q=(uint32_t*)(memory+P48_BATCH_CONTROL_OFFSET),*r=q+64;
  nonce=q[2];cap_alias=memory;response[1]=((uint32_t*)(memory+P39_RESULT_OFFSET))[24];source_fnv=q[8];target_fnv=r[13];
  memcpy(p48_expected_previous_idle,memory+P48_PREVIOUS_IDLE_OFFSET,256);
  memcpy(&observed_result,memory+P39_RESULT_OFFSET,256);memcpy(p41_info,memory+P41_OFFSET,256);memcpy(expected,memory,65536);
  if(!i)memcpy(fixed,memory+P48_BATCH_FROZEN_OFFSET,256);else assert(!memcmp(fixed,memory+P48_BATCH_FROZEN_OFFSET,256));
  /* ARM fixture is post-SAVED; reconstruct its preceding host-owned SAVED
   * word range only. No RTOS-produced audit/reply byte is fabricated. */
  memcpy(p48_saved,previous_saved,128);memcpy(memory+P48_BATCH_CONTROL_OFFSET+0x80,previous_saved,128);memcpy(expected+P48_BATCH_CONTROL_OFFSET+0x80,previous_saved,128);
  uint32_t original_q[32];memcpy(original_q,q,128);assert(p48_prepare_query());assert(!memcmp(original_q,p48_request,124)&&p48_request[31]==0&&q[31]==0);
  assert(!p48_batch_request_valid(p48_request,nonce,i+1,10,p48_case->source,p48_case->wb|p48_case->cc<<1));
  p48_request[31]=q[31]=i+1;assert(p48_read_reply(P48_PHASE_DONE));assert(p48_snapshot_valid(memory));
  const unsigned bad_offsets[]={P41_OFFSET+4,P48_CC_OFFSET+36,P48_CC_OFFSET+618*4,P48_BATCH_CONTROL_OFFSET+4,P48_BATCH_CONTROL_OFFSET+0x80+4,P48_BATCH_CONTROL_OFFSET+0x200,P48_BATCH_CONTROL_OFFSET+144*4,P48_BATCH_FROZEN_OFFSET+32,P48_INITIAL_STOP_OFFSET+32,P39_STOP_OFFSET+10*4,P48_IDLE_HANDOFF_OFFSET+32,P48_PREVIOUS_IDLE_OFFSET+32};
  for(unsigned k=0;k<sizeof bad_offsets/sizeof bad_offsets[0];k++){memory[bad_offsets[k]]^=1;assert(!p48_snapshot_valid(memory));memory[bad_offsets[k]]^=1;rejects++;}
  r[P48_R_REQUEST_CRC]^=1;r[62]=p48_batch_crc(r,248);r[63]=p48_batch_token(nonce,i+1,r[4],r[62]);assert(!p48_read_reply(P48_PHASE_DONE));r[P48_R_REQUEST_CRC]^=1;r[62]=p48_batch_crc(r,248);r[63]=p48_batch_token(nonce,i+1,r[4],r[62]);assert(p48_read_reply(P48_PHASE_DONE));rejects++;
  memcpy(p48_control_copy,memory+P48_BATCH_CONTROL_OFFSET,1024);
  snprintf(out,sizeof out,"%s/case_%02u",argv[2],i);assert(!mkdir(out,0700));assert(p48_publish_saved(out));memcpy(previous_saved,p48_saved,128);assert(!memcmp(memory+P48_BATCH_CONTROL_OFFSET+0x80,p48_saved,128));
  /* The report must use its immutable audit copy even after alias unmap. */
  memcpy(p48_wb_copy,memory+P41_OFFSET,1024);memcpy(p48_initial_stop_copy,memory+P48_INITIAL_STOP_OFFSET,P48_INITIAL_STOP_BYTES);memcpy(observed_stop,memory+P39_STOP_OFFSET,P39_STOP_BYTES);p48_canonical_copied=1;cap_alias=NULL;reason="HOST_REPORT_COPY_TEST";
  ((uint32_t*)p48_wb_copy)[78]=1;((uint32_t*)p48_wb_copy)[77]=1;((uint32_t*)p48_wb_copy)[79]=1;
  assert(write_result(out,1,0));snprintf(path,sizeof path,"%s/result.json",out);assert(has(path,"\"wb_after_start_match\":1")&&has(path,"\"wb_after_completion_match\":1")&&has(path,"\"wb_secondary_observed_stable\":1"));assert(has(path,"\"native_stop_gate_ready_for_isp\":1"));assert(has(path,i?"\"native_stop_completion_verified\":0":"\"native_stop_completion_verified\":1"));
 }
 printf("PASS10 actual ARM prefixes,10 staged-Q publications,10 SAVED commits,%u hostile field mutations,10 audit-after-unmap summaries; PC model only\n",rejects);return 0;
}
