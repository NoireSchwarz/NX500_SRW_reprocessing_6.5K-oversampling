/* SPDX-License-Identifier: AGPL-3.0-only. Exact streaming comparisons; bounded RAM. */
#ifndef NXL_FULL_MEMORY_H
#define NXL_FULL_MEMORY_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "sha256.h"
#include "jpeg_core.h"
#include "r6_timing.h"
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
static struct full_manifest full_before,full_after,full_expected_cache[2];
_Alignas(4) static unsigned char full_period[32768];
static uint32_t full_pattern_crc[2],full_pattern_crc_ready;
_Static_assert(TEST_BLOCK==16384u,"R6 two periodic sixteen-KiB pages");
static uint32_t full_nonce,full_initialized,full_stage,full_pad_bytes,full_prepare_unsafe;
static unsigned char *full_pad;
static int full_input_fd=-1,full_io_error;
static char full_root[1100];
static uint32_t full_input_crc,full_lut_crc;
static char full_input_sha256[68];
static int full_read(int fd,void *p,size_t n,unsigned off){return pread(fd,p,n,off)==(ssize_t)n;}
static int full_file_hash(int fd,unsigned bytes,const char *want,uint32_t *crc){
 struct sha256 h;sha256_init(&h);uint32_t c=~0u;unsigned char b[32768],d[32];char hex[65];
 for(unsigned off=0;off<bytes;){unsigned n=bytes-off;if(n>sizeof b)n=sizeof b;if(!full_read(fd,b,n,off))return 0;sha256_update(&h,b,n);c=bitb_crc_step(c,b,n);off+=n;}
 if(pread(fd,b,1,bytes)!=0)return 0;sha256_final(&h,d);digest_hex(d,hex);*crc=~c;return !strcmp(hex,want);
}
static int full_open_inputs(const char *root){
 char path[1200],hash[65],extra;unsigned bytes;
 full_expected_cache[0].magic=full_expected_cache[1].magic=0;full_pattern_crc_ready=0;
 if(strlen(root)>=sizeof full_root)return 0;strcpy(full_root,root);
 snprintf(path,sizeof path,"%s/input.identity",root);FILE *f=fopen(path,"r");if(!f)return 0;
 int ok=fscanf(f,"NX422INPUT1 %u %64[0-9a-f]",&bytes,hash)==2&&bytes==TEST_FRAME&&strlen(hash)==64;
 if(fscanf(f," %c",&extra)==1)ok=0;fclose(f);if(!ok)return 0;memset(full_input_sha256,0,sizeof full_input_sha256);memcpy(full_input_sha256,hash,64);
 snprintf(path,sizeof path,"%s/input422.bin",root);full_input_fd=open(path,O_RDONLY|O_NOFOLLOW);
 r6_timing("INPUT_HASH","BEGIN",TEST_FRAME,0);int verified=full_input_fd>=0&&full_file_hash(full_input_fd,TEST_FRAME,hash,&full_input_crc);r6_timing("INPUT_HASH","END",TEST_FRAME,verified);return verified;
}
static void full_pattern(unsigned char*,unsigned,unsigned,int);
static void full_cache(uint32_t nonce){
 r6_timing("PERIODIC_PATTERN_CACHE","BEGIN",sizeof full_period,0);
 full_expected_cache[0].magic=full_expected_cache[1].magic=0;full_pattern_crc_ready=0;full_nonce=nonce;for(unsigned i=0;i<sizeof full_period;i++)full_period[i]=bitb_pattern(i,nonce);
 for(unsigned i=0;i<2;i++)full_pattern_crc[i]=bitb_crc(full_period+i*TEST_BLOCK,TEST_BLOCK);full_pattern_crc_ready=1;r6_timing("PERIODIC_PATTERN_CACHE","END",sizeof full_period,1);

}
static void full_pattern(unsigned char *p,unsigned offset,unsigned bytes,int invert){
 while(bytes){unsigned n=32768-(offset&32767);if(n>bytes)n=bytes;memcpy(p,full_period+(offset&32767),n);if(invert)for(unsigned i=0;i<n;i++)p[i]^=255;p+=n;offset+=n;bytes-=n;}
}
static void full_frame_range(unsigned char *p,unsigned offset,unsigned bytes,unsigned mode){
 (void)mode;if(!full_read(full_input_fd,p,bytes,offset)){memset(p,0,bytes);full_io_error=1;}
}
static int full_overlap(unsigned block,unsigned start,unsigned len,unsigned *at,unsigned *n){
 uint32_t low=block>start?block:start,high=block+TEST_BLOCK<start+len?block+TEST_BLOCK:start+len;
 if(high<=low)return 0;*at=low;*n=high-low;return 1;
}
static void full_render(unsigned char *out,unsigned block,unsigned phase,const unsigned char *pad,unsigned pad_bytes){
 (void)phase;full_pattern(out,block,TEST_BLOCK,0);unsigned at,n;
 if(full_overlap(block,0,TEST_FRAME,&at,&n))full_frame_range(out+at-block,at,n,0);
 if(pad_bytes&&full_overlap(block,TEST_JPEG_OUT,pad_bytes,&at,&n))memcpy(out+at-block,pad+at-TEST_JPEG_OUT,n);
}
/* A plain page is exactly one of two nonce-derived periods. Never apply
 * this shortcut to a source or padded-stream overlap. Physical comparisons
 * and independently accumulated source CRC are still performed in full. */
static int full_plain_page(unsigned block,unsigned pad_bytes){
 unsigned at,n;if(full_overlap(block,0,TEST_FRAME,&at,&n))return 0;
 return !pad_bytes||!full_overlap(block,TEST_JPEG_OUT,pad_bytes,&at,&n);
}
static const unsigned char *full_expected_page(uint32_t scratch[TEST_BLOCK/4],unsigned block,unsigned phase,const unsigned char *pad,unsigned pad_bytes){
 if(full_plain_page(block,pad_bytes))return full_period+(block&32767u);
 full_render((unsigned char*)scratch,block,phase,pad,pad_bytes);return (const unsigned char*)scratch;
}
static void full_crcs(struct full_manifest *s,const unsigned char *p,unsigned offset,int expected){
 unsigned at,n;uint32_t *v=expected?&s->source_expected_crc:&s->source_crc;
 if(full_overlap(offset,0,TEST_FRAME,&at,&n))*v=bitb_crc_step(*v,p+at-offset,n);

}
static void full_manifest_init(struct full_manifest *s,const struct bitb_request *q,unsigned phase,unsigned pad_bytes,const unsigned char *pad){
 memset(s,0,sizeof *s);s->magic=FULL_MAGIC;s->version=3;s->bytes=sizeof *s;s->request_crc=q->crc;s->pa=q->pa;s->span=TEST_BYTES;s->width=TEST_W;s->height=TEST_H;s->phase=phase;s->pad_bytes=pad_bytes;s->checked_bytes=TEST_BYTES;
 s->jpeg_reserved_bytes=pad_bytes?JPEG_CAP:0;
 s->source_crc=s->output_crc=s->source_expected_crc=s->output_expected_crc=s->table_crc=s->table_expected_crc=~0u;s->pad_crc=pad_bytes?bitb_crc(pad,pad_bytes):0;
}
static void full_manifest_end(struct full_manifest *s){
 s->source_crc=~s->source_crc;s->output_crc=~s->output_crc;s->source_expected_crc=~s->source_expected_crc;s->output_expected_crc=~s->output_expected_crc;s->table_crc=~s->table_crc;s->table_expected_crc=~s->table_expected_crc;s->crc=bitb_crc(s,offsetof(struct full_manifest,crc));
}
static int full_capture(struct full_manifest *s,const struct bitb_request *q,volatile const unsigned char *memory,unsigned phase,const unsigned char *pad,unsigned pad_bytes){
 uint32_t actual[TEST_BLOCK/4],expected[TEST_BLOCK/4];const char *audit=pad_bytes?"CAP_AUDIT_AFTER":"CAP_AUDIT_BEFORE";r6_timing(audit,"BEGIN",TEST_BYTES,0);full_manifest_init(s,q,phase,pad_bytes,pad);
 for(unsigned k=0;k<TEST_PAGES;k++){
  unsigned offset=k*TEST_BLOCK;volatile const uint32_t *m=(volatile const uint32_t*)(memory+offset);
  for(unsigned i=0;i<TEST_BLOCK/4;i++)actual[i]=m[i];const unsigned char *expected_bytes=full_expected_page(expected,offset,phase,pad,pad_bytes);int exact=0,jpeg_exact=0;
  /* Native encoder owns its entire reserved JPEG output allocation. Its
   * stream counter bounds the published JPEG, not DMA burst padding. Keep
   * every source/LUT/table/outer guard exact; audit all JPEG pages separately. */
  if(pad_bytes&&offset>=JPEG_OUT&&offset-JPEG_OUT<JPEG_CAP){
   jpeg_exact=1;for(unsigned i=0;i<TEST_BLOCK;i++){
    unsigned at=offset+i;unsigned char a=((unsigned char*)actual)[i];
    if(a!=full_period[at&32767u]){jpeg_exact=0;s->jpeg_changed_bytes++;s->jpeg_write_end=at-JPEG_OUT+1;if(at-JPEG_OUT>=pad_bytes)s->jpeg_nonstream_changed++;}
   }
  }else if(!(exact=!memcmp(actual,expected_bytes,TEST_BLOCK))){
   for(unsigned i=0;i<TEST_BLOCK;i++)if(((unsigned char*)actual)[i]!=expected_bytes[i]){
    if(!s->errors){s->first_bad=offset+i;s->actual_byte=((unsigned char*)actual)[i];s->expected_byte=expected_bytes[i];}s->errors++;
   }
  }
  s->blocks[k]=(exact||jpeg_exact)&&full_pattern_crc_ready&&full_plain_page(offset,pad_bytes)?full_pattern_crc[k&1u]:bitb_crc(actual,TEST_BLOCK);full_crcs(s,(unsigned char*)actual,offset,0);full_crcs(s,expected_bytes,offset,1);
 }
 full_manifest_end(s);int verified=!s->errors&&!full_io_error&&s->source_crc==full_input_crc&&s->table_crc==0;r6_timing(audit,"END",TEST_BYTES,verified);return verified;
}
static int full_verify(const struct full_manifest *s,const struct bitb_request *q,unsigned phase,const unsigned char *pad,unsigned pad_bytes){
 if(s->jpeg_reserved_bytes!=(pad_bytes?JPEG_CAP:0)||s->jpeg_write_end>JPEG_CAP||s->jpeg_nonstream_changed>s->jpeg_changed_bytes||(!pad_bytes&&(s->jpeg_changed_bytes||s->jpeg_write_end||s->jpeg_nonstream_changed)))return 0;
 if(s->magic!=FULL_MAGIC||s->version!=3||s->bytes!=sizeof *s||s->request_crc!=q->crc||s->pa!=q->pa||s->span!=TEST_BYTES||s->width!=TEST_W||s->height!=TEST_H||s->phase!=phase||s->pad_bytes!=pad_bytes||s->checked_bytes!=TEST_BYTES||s->errors||s->source_crc!=full_input_crc||s->table_crc!=0||s->crc!=bitb_crc(s,offsetof(struct full_manifest,crc))||s->pad_crc!=(pad_bytes?bitb_crc(pad,pad_bytes):0))return 0;
 uint32_t expected[TEST_BLOCK/4];struct full_manifest *cache=full_expected_cache+phase;
 if(cache->magic!=FULL_MAGIC||cache->request_crc!=q->crc||cache->pa!=q->pa){
  r6_timing("EXPECTED_MANIFEST_BUILD","BEGIN",TEST_BYTES,0);full_manifest_init(cache,q,phase,0,NULL);
  for(unsigned k=0;k<TEST_PAGES;k++){unsigned offset=k*TEST_BLOCK;const unsigned char *bytes=full_expected_page(expected,offset,phase,NULL,0);cache->blocks[k]=full_pattern_crc_ready&&full_plain_page(offset,0)?full_pattern_crc[k&1u]:bitb_crc(bytes,TEST_BLOCK);full_crcs(cache,bytes,offset,0);full_crcs(cache,bytes,offset,1);}full_manifest_end(cache);r6_timing("EXPECTED_MANIFEST_BUILD","END",TEST_BYTES,1);
 }
 struct full_manifest want=*cache;want.pad_bytes=pad_bytes;want.pad_crc=pad_bytes?bitb_crc(pad,pad_bytes):0;
 if(pad_bytes){
  /* JPEG-region page checksums describe observed owned output. Only the
   * non-JPEG pages have a deterministic expected pattern. The exact stream
   * prefix is independently bound by pad_crc and the stage target CRC. */
  for(unsigned k=TEST_JPEG_OUT/TEST_BLOCK;k<(TEST_JPEG_OUT+JPEG_CAP)/TEST_BLOCK;k++)want.blocks[k]=s->blocks[k];
  want.jpeg_reserved_bytes=JPEG_CAP;want.jpeg_changed_bytes=s->jpeg_changed_bytes;want.jpeg_write_end=s->jpeg_write_end;want.jpeg_nonstream_changed=s->jpeg_nonstream_changed;
 }
 want.crc=bitb_crc(&want,offsetof(struct full_manifest,crc));return !memcmp(s,&want,sizeof want);
}

static int full_begin(unsigned stage,const struct bitb_request *q,struct bitb_result *r,volatile unsigned char *m){
 uint32_t expected[TEST_BLOCK/4];if(stage!=FULL_REFERENCE||full_initialized)return 0;full_cache(q->nonce);
 r6_timing("CAP_INITIALIZE","BEGIN",TEST_BYTES,0);for(unsigned k=0;k<TEST_PAGES;k++){const unsigned char *bytes=full_expected_page(expected,k*TEST_BLOCK,0,NULL,0);volatile uint32_t *dst=(volatile uint32_t*)(m+k*TEST_BLOCK);for(unsigned i=0;i<TEST_BLOCK/4;i++){uint32_t value;memcpy(&value,bytes+4*i,4);dst[i]=value;}}full_initialized=1;r6_timing("CAP_INITIALIZE","END",TEST_BYTES,1);
 int ok=full_capture(&full_before,q,m,0,NULL,0);full_stage=stage;r->initialized_bytes=TEST_BYTES;r->initialized_verified=ok;if(!ok)full_prepare_unsafe=1;return ok;
}
static int full_finish(unsigned stage,const struct bitb_request *q,struct bitb_result *r,volatile unsigned char *m,unsigned jpeg_bytes){
 if(stage!=FULL_REFERENCE||jpeg_bytes<32||jpeg_bytes>JPEG_CAP-JPEG_PAD)return 0;
 full_pad_bytes=(jpeg_bytes+511u)&~511u;free(full_pad);full_pad=malloc(full_pad_bytes);if(!full_pad)return 0;
 r6_timing("JPEG_STREAM_COPY","BEGIN",full_pad_bytes,0);for(unsigned i=0;i<full_pad_bytes;i+=4){uint32_t v=*(volatile uint32_t*)(m+TEST_JPEG_OUT+i);memcpy(full_pad+i,&v,4);}
 r6_timing("JPEG_STREAM_COPY","END",full_pad_bytes,1);int ok=full_capture(&full_after,q,m,0,full_pad,full_pad_bytes);if(!ok)full_prepare_unsafe=1;
 r->source_ok=r->guards_ok=ok;r->source_crc=full_after.source_crc;r->target_ok=jpeg_parse(full_pad,jpeg_bytes);r->target_crc=bitb_crc(full_pad,jpeg_bytes);r->output_changed=0;
 for(unsigned i=0;i<full_pad_bytes;i++)r->output_changed+=full_pad[i]!=full_period[(TEST_JPEG_OUT+i)&32767];
 return ok&&r->target_ok;
}
#endif
