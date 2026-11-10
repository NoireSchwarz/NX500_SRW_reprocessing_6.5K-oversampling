/* SPDX-License-Identifier: AGPL-3.0-only
 * Selected, PC-verified SRW fixture. Rows go straight to an owned sink.
 * No full-frame ordinary file, JPEG preview or full-frame heap allocation.
 */
#ifndef NXL_BAYER_FRONT_H
#define NXL_BAYER_FRONT_H
#include "sha256.h"
#include "srw3.h"
#include "stage_layout.h"
#define P32_SOURCE_HASH "dfd6608e4ee05f0d5b4b402fedd3c928aee509595e6964efa4c1ce87f1d9715c"
#define P32_ROWS_HASH "ee3e5a162cfed92492829ef9f674d86e7eaba63ec5d1449f6736f96bce7bfdba"
#define P32_BAYER_HASH "21dcf52ab180f6efdd18f8f26cbe0adc35fce895119b7e5c52671b13e81cad53"
struct p32_front {
 FILE *source,*reference,*rows,*log;
 struct srw_info info;struct srw_stats stats;
 struct sha256 raw_sha,layout_sha;
 uint32_t matched,stored,verified;
 const char *error;
 char source_hash[65],reference_hash[65],raw_hash[65],layout_hash[65];
 void *sink_user;int (*write_row)(void*,uint32_t,const unsigned char*,size_t);
 unsigned char left[16],raw[P31_RAW_WIDTH*2],middle[16],layout[P31_STAGE_STRIDE],right[16];
};
static int p32_file_hash(FILE *f,char out[65],uint64_t *bytes){
 unsigned char b[32768],d[32];struct sha256 s;sha256_init(&s);*bytes=0;
 if(fseeko(f,0,SEEK_SET))return 0;
 size_t n;while((n=fread(b,1,sizeof b,f))){if(stop_requested)return 0;sha256_update(&s,b,n);*bytes+=n;}
 if(ferror(f))return 0;sha256_final(&s,d);digest_hex(d,out);return 1;
}
#ifdef NXL_FAST_PIPELINE
#include "bayer_front_fast.inc"
#else
static int p32_front_open(struct p32_front *f,const char *input,const char *refs,const char *dir,FILE *log){
 memset(f,0,sizeof *f);f->log=log;f->error="FRONT_PREPARE_FAILED";
 f->source=fopen(input,"rb");f->reference=fopen(refs,"rb");
 if(!f->source||!f->reference){f->error="FIXED_SRW_OR_REFERENCE_MISSING";return 0;}
 uint64_t n=0;
 if(!p32_file_hash(f->source,f->source_hash,&n)||n!=p48_source->file_bytes||strcmp(f->source_hash,p48_source->source_sha256)){f->error="SOURCE_HASH_MISMATCH";return 0;}
 if(!p32_file_hash(f->reference,f->reference_hash,&n)||n!=138768||strcmp(f->reference_hash,p48_source->rows_sha256)){f->error="REFERENCE_HASH_MISMATCH";return 0;}
 const char *why="";if(!srw_parse(f->source,&f->info,&why)){f->error=why;return 0;}
 if(f->info.width!=6496||f->info.height!=4336||f->info.bits!=14||f->info.compression!=32773||f->info.offset!=p48_source->strip_offset||f->info.bytes!=p48_source->strip_bytes){f->error="FIXED_SRW_GEOMETRY_CHANGED";return 0;}
 unsigned char header[16];
 if(fseeko(f->reference,0,SEEK_SET)||fread(header,1,16,f->reference)!=16||t32(header,1)!=0x3432524e||t32(header+4,1)!=1||t32(header+8,1)!=6496||t32(header+12,1)!=4336){f->error="REFERENCE_HEADER_INVALID";return 0;}
 char p[1200];snprintf(p,sizeof p,"%s/p39_rows.csv",dir);f->rows=fopen(p,"wx");if(!f->rows){f->error="ROW_REPORT_CREATE_FAILED";return 0;}
 fputs("row,pixels,sha256,reference_match\n",f->rows);
 sha256_init(&f->raw_sha);sha256_init(&f->layout_sha);
 memset(f->left,0xa5,16);memset(f->middle,0x5a,16);memset(f->right,0xa5,16);f->error="";return 1;
}
static int p32_front_row(struct p32_front *f,const unsigned char *raw){
 if(stop_requested){f->error="CANCELLED_OR_TIMEOUT";return 0;}
 if(f->stored>=P31_STAGE_HEIGHT){f->error="LAYOUT_ROW_OVERFLOW";return 0;}
 p31_layout_row(f->layout,raw);
 for(unsigned i=0;i<16;i++)if(f->left[i]!=0xa5||f->middle[i]!=0x5a||f->right[i]!=0xa5){f->error="CPU_ROW_CANARY_CHANGED";return 0;}
 if(!f->write_row(f->sink_user,f->stored,f->layout,sizeof f->layout)){f->error="CAP_ROW_WRITE_FAILED";return 0;}
 sha256_update(&f->layout_sha,f->layout,sizeof f->layout);f->stored++;return 1;
}
static int p32_front_decoded_row(void *user,unsigned row,const uint16_t *pixels,unsigned count){
 struct p32_front *f=user;unsigned char expected[32],actual[32];char hex[65];
 if(count!=P31_RAW_WIDTH||row!=f->matched){f->error="SOURCE_ROW_ORDER_OR_WIDTH";return 0;}
 for(unsigned x=0;x<count;x++){if(pixels[x]>16383){f->error="SOURCE_VALUE_OUT_OF_14BIT_RANGE";return 0;}f->raw[2*x]=pixels[x];f->raw[2*x+1]=pixels[x]>>8;}
 digest_bytes(f->raw,sizeof f->raw,actual);digest_hex(actual,hex);
 int same=fread(expected,1,32,f->reference)==32&&!memcmp(expected,actual,32);
 if(fprintf(f->rows,"%u,%u,%s,%u\n",row,count,hex,same)<0){f->error="ROW_REPORT_WRITE_FAILED";return 0;}
 if(!same){f->error="SOURCE_ROW_HASH_MISMATCH";return 0;}
 if(!p32_front_row(f,f->raw))return 0;
 sha256_update(&f->raw_sha,f->raw,sizeof f->raw);f->matched++;
 if(!(f->matched%256)||f->matched==4336){fprintf(f->log,"CAP_DECODE_WRITE rows=%u/4336 stored_rows=%u elapsed_ms=%u\n",f->matched,f->stored,ms());fflush(f->log);}
 return 1;
}
static int p32_front_write(struct p32_front *f,void *user,int (*emit)(void*,uint32_t,const unsigned char*,size_t)){
 f->sink_user=user;f->write_row=emit;
 for(unsigned y=0;y<P31_OFFSET_Y;y++)if(!p32_front_row(f,NULL))return 0;
 const char *why="";
 if(!srw_decode(f->source,&f->info,&f->stats,p32_front_decoded_row,f,&stop_requested,&why)){if(!*f->error)f->error=why;return 0;}
 unsigned char d[32];sha256_final(&f->raw_sha,d);digest_hex(d,f->raw_hash);
 if(f->matched!=4336||f->stats.rows!=4336||f->stats.pixels!=6496u*4336u||strcmp(f->raw_hash,p48_source->bayer_sha256)||f->stats.min_value!=p48_source->expected_min||f->stats.max_value!=p48_source->expected_max||f->stats.zeros!=p48_source->expected_zeros||fgetc(f->reference)!=EOF){f->error="DECODED_BAYER_REFERENCE_MISMATCH";return 0;}
 for(unsigned y=f->stored;y<P31_STAGE_HEIGHT;y++)if(!p32_front_row(f,NULL))return 0;
 sha256_final(&f->layout_sha,d);digest_hex(d,f->layout_hash);
 if(f->stored!=4384||strcmp(f->layout_hash,p48_source->layout_sha256)){f->error="WRITTEN_LAYOUT_REFERENCE_MISMATCH";return 0;}
 if(fflush(f->rows)||fsync(fileno(f->rows))){f->error="ROW_REPORT_FLUSH_FAILED";return 0;}
 f->verified=1;return 1;
}
static int p32_front_close(struct p32_front *f){int ok=1;
 if(f->source&&fclose(f->source))ok=0;if(f->reference&&fclose(f->reference))ok=0;if(f->rows&&fclose(f->rows))ok=0;
 f->source=f->reference=f->rows=NULL;return ok;
}
#endif /* fast/diagnostic */
#endif
