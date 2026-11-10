/* SPDX-License-Identifier: AGPL-3.0-only. Stream the pinned, real SRW into CAP.
 * Embedded JPEG payloads in this fixture were zeroed in p24. No preview path.
 */
#include "bayer_front.h"
static struct p32_front real_front;
static uint32_t real_opened,real_ready,real_measured,task_quiescent_verified;
static uint32_t source_staged_fnv,frame_measured_ms,frame_prepare_ms;
static char source_before_sha[65],source_after_sha[65],plane_sha[2][65];
static const char *real_error="REAL_FRAME_NOT_PREPARED";
struct p39_tile {uint32_t x,y,w,h,changed[2],rows[2];};
static struct p39_tile tiles[42];
static uint32_t tiles_written[2];
static unsigned char real_scratch[65536];
static uint32_t fnv_more(uint32_t h,const unsigned char *p,unsigned n){while(n--)h=(h^*p++)*16777619u;return h;}
static void alias_digest(uint32_t offset,uint32_t bytes,char out[65],uint32_t *fnv){
 struct sha256 s;unsigned char d[32];sha256_init(&s);uint32_t h=2166136261u;
 for(unsigned at=0;at<bytes;){unsigned n=bytes-at;if(n>sizeof real_scratch)n=sizeof real_scratch;
  memcpy(real_scratch,(const void*)(cap_alias+offset+at),n);sha256_update(&s,real_scratch,n);h=fnv_more(h,real_scratch,n);at+=n;}
 sha256_final(&s,d);digest_hex(d,out);if(fnv)*fnv=h;
}
static int real_open(const char *dir){
 char input[512],refs[512];
 if(p48_selected_ready)snprintf(input,sizeof input,"%s",selected_path);
 else snprintf(input,sizeof input,"/mnt/mmc/nx-lut-demo/assets/%s",p48_source->asset_file);
 snprintf(refs,sizeof refs,"/mnt/mmc/nx-lut-demo/assets/%s",p48_source->row_file);
 if(!p32_front_open(&real_front,input,refs,dir,log_file)){real_error=real_front.error;return 0;}
 real_opened=1;return 1;
}
static int real_cap_row(void *unused,uint32_t y,const unsigned char *row,size_t n){
 (void)unused;if(y>=P39_SOURCE_H||n!=P39_SOURCE_STRIDE||stop_requested)return 0;
 if(!(y&255u)&&!still_owned())return 0;
 memcpy((void*)(cap_alias+P39_SOURCE_OFFSET+y*P39_SOURCE_STRIDE),row,n);
#ifndef NXL_FAST_PIPELINE
 source_staged_fnv=fnv_more(source_staged_fnv,row,(unsigned)n);
#endif
 return 1;
}
static int real_prepare(void){
 unsigned begin=ms();source_staged_fnv=2166136261u;front_mark("DECODE_BEGIN");nxl_live_progress("PIPE_RAW_DECODE","RUNNING","Decoding SRW pixels into the shared workspace");
 if(!real_opened||!p32_front_write(&real_front,NULL,real_cap_row)){real_error=real_front.error;return 0;}
#ifdef NXL_FAST_PIPELINE
 source_fnv=0;
#else
 alias_digest(P39_SOURCE_OFFSET,P39_SOURCE_BYTES,source_before_sha,&source_fnv);
 if(strcmp(source_before_sha,p48_source->layout_sha256)||source_fnv!=source_staged_fnv){real_error="REAL_CAP_FULL_READBACK_MISMATCH";return 0;}
#endif
 if(!p32_front_close(&real_front)){real_error="REAL_INPUT_CLOSE_FAILED";return 0;}real_opened=0;
 /* Audit a prefix plus exact guards. Other unused CAP memory is not claimed. */
 for(unsigned i=P39_SOURCE_OFFSET;i<P39_WINDOW_BYTES;i++)expected[i]=cap_alias[i];
 memset((void*)(cap_alias+P39_SOURCE_OFFSET+P39_SOURCE_BYTES),0x6d,16384);
 memset((void*)(cap_alias+P39_TARGET_OFFSET-16384),0x6d,16384);
#ifndef NXL_FAST_PIPELINE
 memset((void*)(cap_alias+P39_TARGET_OFFSET),0xa5,P39_TARGET_BYTES);
#endif
 memset((void*)(cap_alias+P39_TARGET_OFFSET+P39_TARGET_BYTES),0x6d,16384);
 memset(expected+P39_TARGET_PROOF_OFFSET,0xa5,P39_WINDOW_BYTES);
 __sync_synchronize();real_ready=1;frame_prepare_ms=ms()-begin;real_error="";
 nxl_live_progress("PIPE_ISP","RUNNING","SRW loaded; running the native Still ISP");
 fprintf(log_file,"REAL_SRW_CAP_READY rows=4336 stored_rows=4384 source_bytes=%u grid=14x3 prepare_ms=%u\n",P39_SOURCE_BYTES,frame_prepare_ms);fflush(log_file);return 1;
}
static void real_tiles_init(void){
 memset(tiles,0,sizeof tiles);tiles_written[0]=tiles_written[1]=0;
 for(unsigned y=0;y<3;y++)for(unsigned x=0;x<14;x++){
  struct p39_tile *t=&tiles[y*14+x];t->x=x*P39_TILE_W;t->y=y*P39_TILE_H;
  t->w=P39_W-t->x;if(t->w>P39_TILE_W)t->w=P39_TILE_W;
  t->h=P39_H-t->y;if(t->h>P39_TILE_H)t->h=P39_TILE_H;
 }
}
static int real_native_completion(void){
 if(!p41_audit_valid((const uint32_t*)(cap_alias+P41_OFFSET),nonce))return 0;
 if(!p49_pd_single_audit_valid((const uint32_t*)(cap_alias+P49_PD_OFFSET),nonce))return 0;
 if(!ready_verified||!camera_paused||!camera_threads_stopped()||!slot_original())return 0;
 if(!p48_stop_record_valid((const uint32_t*)(cap_alias+P39_STOP_OFFSET),(const uint32_t*)(cap_alias+P48_INITIAL_STOP_OFFSET),nonce,response[1],p48_case_index+1)||
    !p39_diagnostics_valid((const uint32_t*)(cap_alias+P39_DIAG_OFFSET),nonce,response[1])||
    !p39_result_valid(&observed_result,response[1],nonce,source_fnv,observed_result.w[23]))return 0;
 task_quiescent_verified=1;return 1;
}
