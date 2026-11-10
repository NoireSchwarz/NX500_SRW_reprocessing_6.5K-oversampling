#define _GNU_SOURCE
static int modeled_slot=1;
static int slot_read(void){return modeled_slot;}
#define P39_SLOT_ORIGINAL slot_read
#define P39_CAMERA_STOPPED slot_read
#define main production_entry_not_run
#include "nxl-native-front.c"
#undef main
#include <assert.h>
static unsigned char memory[P39_CAP_BYTES],source_template[P39_SOURCE_BYTES],target_template[P39_TARGET_BYTES],prefix_template[P39_PROOF_BYTES];
static uint32_t native_template[64];static unsigned char diag_template[P39_DIAG_BYTES],stop_template[P39_STOP_BYTES];static uint32_t wb_template[256];
static void load(const char *path,void *out,size_t n){FILE *f=fopen(path,"rb");assert(f&&fread(out,1,n,f)==n&&!fclose(f));}
static void reset(void){
 memcpy(expected,prefix_template,sizeof expected);memcpy(memory,prefix_template,P39_WINDOW_BYTES);
 memcpy(memory+P39_SOURCE_OFFSET,source_template,sizeof source_template);memcpy(memory+P39_TARGET_OFFSET,target_template,sizeof target_template);
 memcpy(memory+P39_DIAG_OFFSET,diag_template,sizeof diag_template);memcpy(memory+P39_STOP_OFFSET,stop_template,sizeof stop_template);
 const unsigned guards[]={P39_SOURCE_OFFSET+P39_SOURCE_BYTES,P39_TARGET_OFFSET-16384,P39_TARGET_OFFSET+P39_TARGET_BYTES};
 for(unsigned k=0;k<3;k++)memset(memory+guards[k],0x6d,16384);
 memcpy(observed_result.w,native_template,256);memcpy(memory+P39_RESULT_OFFSET,native_template,256);
 memcpy(memory+P41_OFFSET,wb_template,1024);memcpy(p41_info,wb_template,1024);
 cap_alias=memory;response[1]=0xa1c00000;nonce=0x12345678;source_fnv=p39_fnv(source_template,sizeof source_template);
 real_ready=ready_verified=task_quiescent_verified=modeled_slot=1;real_measured=dma_write_verified=copy_verified=stop_requested=0;
 real_front.matched=4336;real_front.stored=4384;real_front.verified=1;
 strcpy(real_front.source_hash,P32_SOURCE_HASH);strcpy(real_front.raw_hash,P32_BAYER_HASH);strcpy(source_before_sha,P31_STAGE_HASH);
}
static void rehash(void){observed_result.w[23]=p39_fnv(memory+P39_TARGET_OFFSET,P39_TARGET_BYTES);memcpy(memory+P39_RESULT_OFFSET,&observed_result,256);}
int main(int argc,char **argv){
 assert(argc==10);load(argv[9],wb_template,1024);load(argv[1],prefix_template,sizeof prefix_template);load(argv[2],native_template,256);load(argv[4],diag_template,sizeof diag_template);load(argv[5],target_template,sizeof target_template);load(argv[6],stop_template,sizeof stop_template);load(argv[7],source_template,sizeof source_template);
 reset();assert(verify_result()&&tiles_written[0]==42&&tiles_written[1]==42&&output_rows_written==4320&&chroma_rows_written==4320);
 response[0]=0x90ba8000;ownership_verified=hook_restored=hook_native_restore_verified=code_restored=detached_ok=idle_registers_verified=rtos_readonly_guards=camera_resumed_verified=1;camera_paused=api_inflight=0;camera_resumes=1;send_calls=native_packet_reads=6;cache_sends=console_outputs_confirmed=3;slot_pointer_writes=2;
 reason="SRW_PRIMARY_WB_NATIVE_IPC_WRITEBACK_RESTORED_RESTART_REQUIRED";assert(save_proof(argv[3])&&write_result(argv[3],0,1));
 reset();for(unsigned y=0;y<4320;y++)if(y>=2944)memset(memory+P39_TARGET_OFFSET+y*6496+13*464,0xa5,448);rehash();assert(!verify_result()&&tiles_written[0]==41);
 reset();for(unsigned y=2944;y<4320;y++)memset(memory+P39_TARGET_OFFSET+P39_UV_OFFSET+y*6496+13*464,0xa5,448);rehash();assert(!verify_result()&&tiles_written[1]==41);
 reset();memory[P39_SOURCE_OFFSET+P39_SOURCE_BYTES-1]^=1;assert(!verify_result()&&!input_unchanged);
 reset();memory[P39_TARGET_OFFSET+P39_PLANE_BYTES]^=1;rehash();assert(!verify_result()&&guard_changed_bytes);
 const unsigned guards[]={P39_SOURCE_OFFSET+P39_SOURCE_BYTES,P39_TARGET_OFFSET-16384,P39_TARGET_OFFSET+P39_TARGET_BYTES};
 for(unsigned k=0;k<3;k++){reset();memory[guards[k]]^=1;assert(!verify_result()&&guard_changed_bytes);}
 reset();memory[123]^=1;assert(!verify_result()&&guard_changed_bytes);
 reset();((uint32_t*)(memory+P39_STOP_OFFSET))[22]=0xbaadf00d;assert(!verify_result());
 reset();((uint32_t*)(memory+P39_DIAG_OFFSET+7*512))[9]|=0x80;assert(!verify_result());
 reset();observed_result.w[59]=1;assert(!verify_result());
 reset();task_quiescent_verified=0;assert(!verify_result());
 reset();modeled_slot=0;assert(!verify_result());
 reset();memset(memory+P39_TARGET_OFFSET,0xa5,P39_TARGET_BYTES);
 for(unsigned y=0;y<P39_TILE_H;y++)for(unsigned plane=0;plane<2;plane++)memcpy(memory+P39_TARGET_OFFSET+(plane?P39_UV_OFFSET:0)+y*P39_TARGET_STRIDE,target_template+(plane?P39_UV_OFFSET:0)+y*P39_TARGET_STRIDE,P39_TILE_W);
 rehash();assert(!verify_result()&&tiles_written[0]==1&&tiles_written[1]==1);
 reason="REAL_SRW_FULL_FRAME_COVERAGE_OR_GUARD_CHECK_FAILED";assert(save_proof(argv[8])&&write_result(argv[8],1,1));
 puts("PASS actual full-frame verifier:42 Y/UV tiles, edge tiles, last source byte, all guards, stale stop, pending IRQ, wrong grid and handoff state; MODELED DMA");return 0;
}
