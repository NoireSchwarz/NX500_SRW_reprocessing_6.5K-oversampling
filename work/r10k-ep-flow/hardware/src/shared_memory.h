/* SPDX-License-Identifier: AGPL-3.0-only. Bounded audits of the shared CAP.
 * The protected RTOS prefix/lease are never initialized as image scratch.
 * LUT pixel correctness is not inferred from completion: record the complete
 * observed output digest and bind the JPEG input to it after cleanup. */
#ifndef NXL_SHARED_MEMORY_H
#define NXL_SHARED_MEMORY_H
#ifdef NXL_FAST_PIPELINE
#include "fast_shared_memory.h"
#else
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "sha256.h"
#include "jpeg_core.h"
#include "r6_timing.h"
#include "../../common/shared_workspace_io.h"
#define FULL_MAGIC 0x334d464eu
#define FULL_REFERENCE 0u
#define FULL_LUT 1u
#define FULL_FINAL 2u
struct full_manifest {
 uint32_t magic,version,bytes,request_crc,pa,span,width,height,phase,pad_bytes,checked_bytes,errors,first_bad,expected_byte,actual_byte;
 uint32_t source_crc,output_crc,source_expected_crc,output_expected_crc,table_crc,table_expected_crc,pad_crc;
 uint32_t jpeg_reserved_bytes,jpeg_changed_bytes,jpeg_write_end,jpeg_nonstream_changed;
 uint32_t blocks[TEST_PAGES],crc;
};
static struct full_manifest full_before,full_after,full_lut_after;
static unsigned char full_period[32768],full_table[LUT_SLOT_BYTES],full_prefix[SW_CONTROL],full_footer[16384];
static uint32_t full_initialized,full_stage,full_pad_bytes,full_prepare_unsafe,full_input_crc,full_lut_crc;
static unsigned char *full_pad;
static char full_input_sha256[68],full_lut_sha256[68],full_table_sha256[68];
static struct sw_lease full_workspace_original;
static int full_input_fd=-1;
static char full_root[1100];
static int full_read(int fd,void*p,size_t n,unsigned offset){size_t done=0;while(done<n){ssize_t k=pread(fd,(unsigned char*)p+done,n-done,(off_t)offset+done);if(k<0&&errno==EINTR)continue;if(k<=0)return 0;done+=(size_t)k;}return 1;}
static int full_file_hash(int fd,unsigned bytes,const char*want,uint32_t*crc){
 struct sha256 h;sha256_init(&h);uint32_t c=~0u;unsigned char b[32768],d[32];char hex[65];
 for(unsigned off=0;off<bytes;){unsigned n=bytes-off;if(n>sizeof b)n=sizeof b;if(!full_read(fd,b,n,off))return 0;sha256_update(&h,b,n);c=bitb_crc_step(c,b,n);off+=n;}
 if(pread(fd,b,1,bytes)!=0)return 0;sha256_final(&h,d);digest_hex(d,hex);*crc=~c;return !strcmp(hex,want);
}
static int full_open_inputs(const char*root){
 char path[1200],hash[65],extra;unsigned bytes;if(strlen(root)>=sizeof full_root)return 0;strcpy(full_root,root);
 snprintf(path,sizeof path,"%s/input.identity",root);FILE*f=fopen(path,"r");if(!f)return 0;
 int ok=fscanf(f,"NX422INPUT1 %u %64[0-9a-f]",&bytes,hash)==2&&bytes==TEST_FRAME&&strlen(hash)==64;
 if(fscanf(f," %c",&extra)==1)ok=0;if(fclose(f))ok=0;if(!ok)return 0;memcpy(full_input_sha256,hash,65);
 snprintf(path,sizeof path,"%s/input422.bin",root);full_input_fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(full_input_fd<0||!full_file_hash(full_input_fd,TEST_FRAME,hash,&full_input_crc))return 0;
 snprintf(path,sizeof path,"%s/shared-workspace.bin",root);if(!sw_file_read(path,&full_workspace_original,sizeof full_workspace_original)||!sw_valid(&full_workspace_original)||full_workspace_original.phase!=SW_FRONT_READY)return 0;
 snprintf(path,sizeof path,"%s/srw-lut.bin",root);if(!sw_file_read(path,full_table,sizeof full_table)||!lut_table_valid(full_table,sizeof full_table))return 0;
 unsigned char d[32];struct sha256 h;sha256_init(&h);sha256_update(&h,full_table,sizeof full_table);sha256_final(&h,d);digest_hex(d,full_table_sha256);
 return !strcmp(full_table_sha256,full_workspace_original.lut_sha256);
}
static void full_cache(uint32_t nonce){for(unsigned i=0;i<sizeof full_period;i++)full_period[i]=bitb_pattern(i,nonce);}
static void full_pattern(unsigned char*p,unsigned offset,unsigned bytes,int invert){while(bytes){unsigned n=32768-(offset&32767u);if(n>bytes)n=bytes;memcpy(p,full_period+(offset&32767u),n);if(invert)for(unsigned i=0;i<n;i++)p[i]^=255;p+=n;offset+=n;bytes-=n;}}
static int full_overlap(unsigned block,unsigned start,unsigned len,unsigned*at,unsigned*n){unsigned lo=block>start?block:start,hi=block+TEST_BLOCK<start+len?block+TEST_BLOCK:start+len;if(hi<=lo)return 0;*at=lo;*n=hi-lo;return 1;}
static void full_expected(unsigned char*p,unsigned block){
 full_pattern(p,block,TEST_BLOCK,0);unsigned at,n;
 if(full_overlap(block,0,SW_CONTROL,&at,&n))memcpy(p+at-block,full_prefix+at,n);
 if(full_overlap(block,SW_TABLE,LUT_SLOT_BYTES,&at,&n))memcpy(p+at-block,full_table+at-SW_TABLE,n);
 if(full_overlap(block,SW_LEASE,16384,&at,&n))memcpy(p+at-block,full_footer+at-SW_LEASE,n);
}
static int full_memory_hash(volatile const unsigned char*m,unsigned off,unsigned bytes,char hash[68],uint32_t*crc){
 struct sha256 h;sha256_init(&h);uint32_t c=~0u;unsigned char data[32768],d[32];
 for(unsigned pos=0;pos<bytes;){unsigned n=bytes-pos;if(n>sizeof data)n=sizeof data;for(unsigned i=0;i<n;i++)data[i]=m[off+pos+i];sha256_update(&h,data,n);c=bitb_crc_step(c,data,n);pos+=n;}
 sha256_final(&h,d);digest_hex(d,hash);*crc=~c;return 1;
}
static unsigned full_source_offset(unsigned stage){return stage==FULL_LUT?SW_INPUT:SW_LUT_OUT;}
static unsigned full_source_span(unsigned stage){return stage==FULL_LUT?SW_FRONT_UV+SW_FRONT_STRIDE*TEST_H:TEST_FRAME;}
static int full_is_source(unsigned stage,unsigned pos){unsigned off=full_source_offset(stage);return pos>=off&&pos-off<full_source_span(stage);}
static int full_source_hash(volatile const unsigned char*m,unsigned stage,char hash[68],uint32_t*crc){
 if(stage==FULL_FINAL)return full_memory_hash(m,SW_LUT_OUT,TEST_FRAME,hash,crc);
 struct sha256 h;sha256_init(&h);uint32_t c=~0u;unsigned char row[SW_W],d[32];
 for(unsigned p=0;p<2;p++)for(unsigned y=0;y<TEST_H;y++){
  memcpy(row,(const void*)(m+SW_INPUT+p*SW_FRONT_UV+y*SW_FRONT_STRIDE),TEST_W);
  sha256_update(&h,row,TEST_W);c=bitb_crc_step(c,row,TEST_W);
 }
 sha256_final(&h,d);digest_hex(d,hash);*crc=~c;return 1;
}
static int full_capture(struct full_manifest*s,const struct bitb_request*q,volatile const unsigned char*m,unsigned stage,unsigned after){
 unsigned char actual[TEST_BLOCK],expected[TEST_BLOCK];memset(s,0,sizeof *s);s->magic=FULL_MAGIC;s->version=4;s->bytes=sizeof *s;s->request_crc=q->crc;s->pa=q->pa;s->span=TEST_BYTES;s->width=TEST_W;s->height=TEST_H;s->phase=stage*2+after;s->checked_bytes=TEST_BYTES;
 s->source_crc=s->output_crc=s->table_crc=~0u;s->source_expected_crc=stage==FULL_LUT?full_input_crc:full_lut_crc;
 s->table_expected_crc=bitb_crc(full_table,sizeof full_table);
 if(stage==FULL_FINAL&&after){s->pad_bytes=full_pad_bytes;s->pad_crc=bitb_crc(full_pad,full_pad_bytes);s->jpeg_reserved_bytes=JPEG_CAP;}
 for(unsigned k=0;k<TEST_PAGES;k++){
  unsigned block=k*TEST_BLOCK,at,n;for(unsigned i=0;i<TEST_BLOCK;i++)actual[i]=m[block+i];full_expected(expected,block);
  s->blocks[k]=bitb_crc(actual,sizeof actual);
  if(full_overlap(block,SW_LUT_OUT,TEST_FRAME,&at,&n))s->output_crc=bitb_crc_step(s->output_crc,actual+at-block,n);
  if(full_overlap(block,SW_TABLE,LUT_SLOT_BYTES,&at,&n))s->table_crc=bitb_crc_step(s->table_crc,actual+at-block,n);
  /* Input is bound by a full SHA/CRC before submission and its full-page
   * checksums after completion, including input tail padding. */
  int input_page=full_overlap(block,full_source_offset(stage),full_source_span(stage),&at,&n);
  if(after&&input_page){if(s->blocks[k]!=full_before.blocks[k])s->errors++;continue;}
  for(unsigned i=0;i<TEST_BLOCK;i++){
   unsigned pos=block+i;int input=full_is_source(stage,pos);
   int output=after&&((stage==FULL_LUT&&pos>=SW_LUT_OUT&&pos-SW_LUT_OUT<TEST_FRAME)||
                      (stage==FULL_FINAL&&pos>=JPEG_OUT&&pos-JPEG_OUT<JPEG_CAP));
   if(output&&stage==FULL_FINAL&&actual[i]!=expected[i]){
    s->jpeg_changed_bytes++;s->jpeg_write_end=pos-JPEG_OUT+1;if(pos-JPEG_OUT>=full_pad_bytes)s->jpeg_nonstream_changed++;
   }
   if(input||output)continue;
   if(actual[i]!=expected[i]){if(!s->errors){s->first_bad=pos;s->actual_byte=actual[i];s->expected_byte=expected[i];}s->errors++;}
  }
 }
 char source_hash[68];full_source_hash(m,stage,source_hash,&s->source_crc);
 if(strcmp(source_hash,stage==FULL_LUT?full_input_sha256:full_lut_sha256))s->errors++;
 s->output_crc=~s->output_crc;s->table_crc=~s->table_crc;s->output_expected_crc=s->output_crc;
 if(s->source_crc!=s->source_expected_crc||s->table_crc!=s->table_expected_crc)s->errors++;
 s->crc=bitb_crc(s,offsetof(struct full_manifest,crc));return !s->errors;
}
static int full_manifest_valid(const struct full_manifest*s,const struct bitb_request*q,unsigned stage,unsigned after){
 if(stage==FULL_FINAL&&after){if(s->jpeg_reserved_bytes!=JPEG_CAP||!s->pad_bytes||s->pad_bytes>JPEG_CAP||s->jpeg_write_end>JPEG_CAP||s->jpeg_nonstream_changed>s->jpeg_changed_bytes)return 0;}
 else if(s->pad_bytes||s->pad_crc||s->jpeg_reserved_bytes||s->jpeg_changed_bytes||s->jpeg_write_end||s->jpeg_nonstream_changed)return 0;
 return s->magic==FULL_MAGIC&&s->version==4&&s->bytes==sizeof *s&&s->request_crc==q->crc&&s->pa==q->pa&&s->span==TEST_BYTES&&s->width==TEST_W&&s->height==TEST_H&&s->phase==stage*2+after&&s->checked_bytes==TEST_BYTES&&!s->errors&&s->source_crc==s->source_expected_crc&&s->table_crc==bitb_crc(full_table,sizeof full_table)&&s->table_crc==s->table_expected_crc&&s->crc==bitb_crc(s,offsetof(struct full_manifest,crc));
}
static int full_begin(unsigned stage,const struct bitb_request*q,struct bitb_result*r,volatile unsigned char*m){
 char path[1200],hash[68];uint32_t crc=0;struct sw_lease active;
 if(stage==FULL_LUT){
  if(full_initialized||q->pa!=full_workspace_original.pa||q->owner_va!=full_workspace_original.va||!sw_owner_alive(&full_workspace_original))return 0;
  snprintf(path,sizeof path,"%s/workspace-prefix.bin",full_root);if(!sw_file_read(path,full_prefix,sizeof full_prefix))return 0;
  memcpy(&active,(const void*)(m+SW_LEASE),sizeof active);
  if(!sw_same_allocation(&active,&full_workspace_original)||active.phase!=SW_BACKEND_ACTIVE)return 0;
  memset(full_footer,0,sizeof full_footer);memcpy(full_footer,&active,sizeof active);
  full_memory_hash(m,0,SW_CONTROL,hash,&crc);if(strcmp(hash,active.prefix_sha256)||memcmp((const void*)m,full_prefix,sizeof full_prefix)||memcmp((const void*)(m+SW_LEASE),full_footer,sizeof full_footer))return 0;
  /* Read the original padded ISP planes directly. RDMA uses stride6496;
   * WDMA produces stride6480 into the former RAW region. No pixel copy. */
  full_source_hash(m,FULL_LUT,hash,&crc);if(crc!=full_input_crc||strcmp(hash,full_input_sha256))return 0;
  full_cache(q->nonce);full_initialized=1;
 }else if(stage==FULL_FINAL){
  if(full_initialized!=1||!full_manifest_valid(&full_lut_after,q,FULL_LUT,1)||!sw_owner_alive(&full_workspace_original))return 0;
  unsigned char page[TEST_BLOCK];for(unsigned k=0;k<TEST_PAGES;k++){for(unsigned i=0;i<TEST_BLOCK;i++)page[i]=m[k*TEST_BLOCK+i];if(bitb_crc(page,sizeof page)!=full_lut_after.blocks[k])return 0;}
  /* Native LUT output already has the JPEG input layout. Preserve it and
   * reuse the no-longer-needed padded ISP input region for JPEG output. */
  full_memory_hash(m,SW_LUT_OUT,TEST_FRAME,hash,&crc);if(crc!=full_lut_crc||strcmp(hash,full_lut_sha256))return 0;full_initialized=2;
 }else return 0;
 unsigned char expected[TEST_BLOCK];
 for(unsigned k=0;k<TEST_PAGES;k++){
  unsigned block=k*TEST_BLOCK;if(block<SW_CONTROL||block>=SW_LEASE)continue;full_expected(expected,block);
  for(unsigned i=0;i<TEST_BLOCK;i++){unsigned pos=block+i;if(full_is_source(stage,pos))continue;m[pos]=expected[i];}
 }
 __sync_synchronize();int ok=full_capture(&full_before,q,m,stage,0);full_stage=stage;r->initialized_bytes=TEST_BYTES;r->initialized_verified=ok;if(!ok)full_prepare_unsafe=1;return ok;
}
static int full_finish(unsigned stage,const struct bitb_request*q,struct bitb_result*r,volatile unsigned char*m,unsigned jpeg_bytes){
 if(stage!=full_stage)return 0;int ok;
 if(stage==FULL_LUT){
  ok=full_capture(&full_after,q,m,stage,1);full_lut_after=full_after;
  full_memory_hash(m,SW_LUT_OUT,TEST_FRAME,full_lut_sha256,&full_lut_crc);
  r->source_crc=full_after.source_crc;r->target_crc=full_lut_crc;r->expected_target_crc=0;
  r->source_ok=r->target_ok=r->guards_ok=ok;r->output_changed=0;
  for(unsigned i=0;i<TEST_FRAME;i++)r->output_changed+=m[SW_LUT_OUT+i]!=full_period[(SW_LUT_OUT+i)&32767u];
  if(!r->output_changed)ok=0;
 }else if(stage==FULL_FINAL){
  if(jpeg_bytes<32||jpeg_bytes>JPEG_CAP-JPEG_PAD)return 0;full_pad_bytes=(jpeg_bytes+511u)&~511u;free(full_pad);full_pad=malloc(full_pad_bytes);if(!full_pad)return 0;
  for(unsigned i=0;i<full_pad_bytes;i++)full_pad[i]=m[JPEG_OUT+i];
  ok=full_capture(&full_after,q,m,stage,1);r->source_crc=full_after.source_crc;r->source_ok=r->guards_ok=ok;
  r->target_ok=jpeg_parse(full_pad,jpeg_bytes);r->target_crc=bitb_crc(full_pad,jpeg_bytes);r->output_changed=0;
  for(unsigned i=0;i<full_pad_bytes;i++)r->output_changed+=full_pad[i]!=full_period[(JPEG_OUT+i)&32767u];
  if(!r->target_ok)ok=0;
 }else return 0;
 if(!ok)full_prepare_unsafe=1;return ok;
}
#endif /* fast/diagnostic */
#endif
