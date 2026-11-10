/* SPDX-License-Identifier: AGPL-3.0-only. Runtime guards, no whole-frame audit.
 * Pixels remain in the single CAP. Only the final JPEG is persisted.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "sha256.h"
#include "jpeg_core.h"
#include "r6_timing.h"
#include "../../common/album_output.h"
#include "../../common/shared_workspace_io.h"
#include "../../common/lens_profile.h"
#define FULL_REFERENCE 0u
#define FULL_LUT 1u
#define FULL_FINAL 2u
#define FULL_LDC 3u
struct full_manifest {uint32_t runtime_guards_only;};
static unsigned char full_table[LUT_SLOT_BYTES],full_prefix[SW_CONTROL],full_footer[16384];
static unsigned full_initialized,full_stage,full_pad_bytes,full_prepare_unsafe;
static unsigned char*full_pad;
static unsigned char*full_cached_pad;
static unsigned full_jpeg_cache_bytes,full_jpeg_info_valid;
static struct jpeg_info full_jpeg_info;
static void full_jpeg_cache_close(void){free(full_cached_pad);full_cached_pad=NULL;full_jpeg_cache_bytes=full_jpeg_info_valid=0;full_pad=NULL;}
static int full_jpeg_capture(volatile const unsigned char*source,unsigned bytes){
 full_jpeg_cache_close();full_pad=(unsigned char*)(uintptr_t)source;full_pad_bytes=bytes;
 /* Cache only the final compressed stream, after the fresh completion.
  * A failed optional allocation retains the original direct-alias path. */
 if(bytes<=16u*1024u*1024u){
  unsigned n=(bytes+3u)&~3u;full_cached_pad=malloc(n);
  if(full_cached_pad){for(unsigned i=0;i<n;i+=4){uint32_t v=*(volatile const uint32_t*)(source+i);memcpy(full_cached_pad+i,&v,4);}full_pad=full_cached_pad;full_jpeg_cache_bytes=n;}
 }
 full_jpeg_info_valid=jpeg_parse_info(full_pad,bytes,&full_jpeg_info)&&full_jpeg_info.width==JPEG_W&&full_jpeg_info.height==JPEG_H&&full_jpeg_info.format==422;
 return full_jpeg_info_valid;
}
static char full_table_sha256[68],full_root[1100],full_output_target[1200];
static struct sw_lease full_workspace_original;
static struct nj_config full_jpeg_config;
static struct lp_config full_ldc_config;
static void full_hash(const void*p,unsigned n,char out[68]){struct sha256 h;unsigned char d[32];sha256_init(&h);sha256_update(&h,p,n);sha256_final(&h,d);digest_hex(d,out);}
static int full_open_inputs(const char*root){
 if(strlen(root)>=sizeof full_root)return 0;strcpy(full_root,root);char path[1200];
 snprintf(path,sizeof path,"%s/shared-workspace.bin",root);if(!sw_file_read(path,&full_workspace_original,sizeof full_workspace_original)||!sw_valid(&full_workspace_original)||full_workspace_original.phase!=SW_FRONT_READY)return 0;
 snprintf(path,sizeof path,"%s/native-jpeg-config.bin",root);if(!sw_file_read(path,&full_jpeg_config,sizeof full_jpeg_config)||!nj_valid(&full_jpeg_config,full_workspace_original.source_sha256))return 0;
 snprintf(path,sizeof path,"%s/native-ldc-config.bin",root);if(!sw_file_read(path,&full_ldc_config,sizeof full_ldc_config)||!lp_valid(&full_ldc_config,full_workspace_original.source_sha256))return 0;
 snprintf(path,sizeof path,"%s/srw-lut.bin",root);if(!sw_file_read(path,full_table,sizeof full_table)||!lut_table_valid(full_table,sizeof full_table))return 0;
 full_hash(full_table,sizeof full_table,full_table_sha256);if(strcmp(full_table_sha256,full_workspace_original.lut_sha256))return 0;
 snprintf(path,sizeof path,"%s/output-path.txt",root);FILE*f=fopen(path,"r");if(!f)return 0;int ok=fgets(full_output_target,sizeof full_output_target,f)!=NULL&&fgetc(f)==EOF;if(fclose(f))ok=0;
 size_t n=strlen(full_output_target);if(!ok||n<2||full_output_target[n-1]!='\n')return 0;full_output_target[n-1]=0;
 return (!strncmp(full_output_target,"/mnt/mmc/NXDEMO51/JOB_",sizeof "/mnt/mmc/NXDEMO51/JOB_"-1)&&!strstr(full_output_target,".."))||ao_target(full_output_target,1);
}
static unsigned fast_lut_tail(void){return SW_LUT_OUT+TEST_FRAME;}
static unsigned fast_input_tail(void){return SW_LDC_OUT+SW_FRONT_UV+SW_FRONT_STRIDE*TEST_H;}
static unsigned fast_bayer_tail(void){return SW_INPUT+SW_FRONT_UV+SW_FRONT_STRIDE*TEST_H;}
static void fast_guard_put(volatile unsigned char*m,unsigned off){for(unsigned i=0;i<512;i++)m[off+i]=(unsigned char)(0xb7u+(i*13u));}
static int fast_guard_ok(volatile const unsigned char*m,unsigned off){for(unsigned i=0;i<512;i++)if(m[off+i]!=(unsigned char)(0xb7u+i*13u))return 0;return 1;}
static int fast_control_ok(volatile const unsigned char*m){
 return !memcmp((const void*)m,full_prefix,SW_CONTROL)&&!memcmp((const void*)(m+SW_LEASE),full_footer,sizeof full_footer)&&!memcmp((const void*)(m+TEST_TABLE),full_table,sizeof full_table);
}
static int full_begin(unsigned stage,const struct bitb_request*q,struct bitb_result*r,volatile unsigned char*m){
 if(!sw_owner_alive(&full_workspace_original)||q->pa!=full_workspace_original.pa||q->owner_va!=full_workspace_original.va)return 0;
 if(stage==FULL_LDC){
  if(full_initialized)return 0;struct sw_lease active;memcpy(&active,(const void*)(m+SW_LEASE),sizeof active);
  if(!sw_same_allocation(&active,&full_workspace_original)||active.phase!=SW_BACKEND_ACTIVE)return 0;
  char hash[68];memcpy(full_prefix,(const void*)m,sizeof full_prefix);full_hash(full_prefix,sizeof full_prefix,hash);if(strcmp(hash,active.prefix_sha256))return 0;
  memcpy(full_footer,(const void*)(m+SW_LEASE),sizeof full_footer);
  memcpy((void*)(m+TEST_TABLE),full_table,sizeof full_table);
  memset((void*)(m+TEST_BACKUP),0,0x14000u);
  fast_guard_put(m,fast_lut_tail());fast_guard_put(m,fast_input_tail());fast_guard_put(m,fast_bayer_tail());full_initialized=1;
 }else if(stage==FULL_LUT){
  if(full_initialized!=1||!fast_control_ok(m)||!fast_guard_ok(m,fast_input_tail())||!fast_guard_ok(m,fast_bayer_tail()))return 0;
  full_initialized=2;
 }else if(stage==FULL_FINAL){
  if(full_initialized!=2||!fast_control_ok(m)||!fast_guard_ok(m,fast_lut_tail())||!fast_guard_ok(m,fast_input_tail()))return 0;
  fast_guard_put(m,JPEG_OUT+JPEG_CAP);full_initialized=3;
 }else return 0;
 full_stage=stage;r->initialized_bytes=TEST_BYTES;r->initialized_verified=1;__sync_synchronize();return 1;
}
static int full_finish(unsigned stage,const struct bitb_request*q,struct bitb_result*r,volatile unsigned char*m,unsigned bytes){
 (void)q;int ok=stage==full_stage&&fast_control_ok(m)&&fast_guard_ok(m,fast_lut_tail());
 if(stage==FULL_LDC||stage==FULL_LUT)ok=ok&&fast_guard_ok(m,fast_input_tail())&&fast_guard_ok(m,fast_bayer_tail());
 else if(stage==FULL_FINAL){
  ok=ok&&fast_guard_ok(m,JPEG_OUT+JPEG_CAP)&&bytes>=32&&bytes<=JPEG_CAP-JPEG_PAD;
  if(ok)ok=full_jpeg_capture(m+JPEG_OUT,bytes);
 }else ok=0;
 r->source_ok=r->target_ok=r->guards_ok=(unsigned)ok;r->source_crc=r->target_crc=0;r->output_changed=0;
 if(!ok)full_prepare_unsafe=1;return ok;
}
