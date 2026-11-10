/* SPDX-License-Identifier: LGPL-2.1-only
 * Streaming C adaptation of LibRaw0.22.1 samsung3_load_raw/ph1_bithuff.
 * Original copyright2019-2022 LibRaw LLC(info@libraw.org).
 * Modifications2026: bounded reader, four-row ring, explicit zero-length signed
 * differences, checked TIFF/raw geometry, cancellation and row sink.
 * Corresponding original sources and license are in SOURCE/upstream/.
 */
#ifndef P24_SRW3_H
#define P24_SRW3_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
struct srw_info {uint32_t width,height,bits,compression,offset,bytes,samples;uint64_t file_bytes;};
struct srw_stats {uint32_t rows,bit_depth,optflags,init_value,min_value,max_value;uint64_t zeros,pixels,consumed_bytes,heap_bytes;};
static uint16_t t16(const unsigned char *p,int le){return le?(uint16_t)(p[0]|p[1]<<8):(uint16_t)(p[0]<<8|p[1]);}
static uint32_t t32(const unsigned char *p,int le){return le?((uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24):((uint32_t)p[3]|(uint32_t)p[2]<<8|(uint32_t)p[1]<<16|(uint32_t)p[0]<<24);}
static int srw_at(FILE *f,uint64_t size,uint64_t off,void *buf,size_t n){return off<=size&&n<=size-off&&fseeko(f,(off_t)off,SEEK_SET)==0&&fread(buf,1,n,f)==n;}
static int srw_parse(FILE *f,struct srw_info *s,const char **error){
 unsigned char h[8];memset(s,0,sizeof *s);*error="TIFF_INVALID";
 if(fseeko(f,0,SEEK_END))return 0;off_t end=ftello(f);if(end<8)return 0;s->file_bytes=(uint64_t)end;
 if(!srw_at(f,s->file_bytes,0,h,8))return 0;int le=h[0]=='I'&&h[1]=='I';if(!le&&!(h[0]=='M'&&h[1]=='M'))return 0;if(t16(h+2,le)!=42)return 0;
 uint32_t queue[64]={t32(h+4,le)},seen[64];unsigned queued=1,read=0,visited=0,found=0;
 while(read<queued){
  uint32_t off=queue[read++];if(!off)continue;for(unsigned i=0;i<visited;i++)if(seen[i]==off){*error="TIFF_CYCLE";return 0;}if(visited>=64)return 0;seen[visited++]=off;
  unsigned char count[2];if(!srw_at(f,s->file_bytes,off,count,2))return 0;unsigned n=t16(count,le);if(n>512||(uint64_t)off+2+(uint64_t)n*12+4>s->file_bytes)return 0;
  struct srw_info one={0};one.samples=1;unsigned mask=0,invalid=0;
  for(unsigned i=0;i<n;i++){
   unsigned char e[12];if(!srw_at(f,s->file_bytes,(uint64_t)off+2+i*12,e,12))return 0;unsigned tag=t16(e,le),type=t16(e+2,le);uint32_t num=t32(e+4,le),v=0;
   if(tag==330){
    if(type!=4||num>16||queued+num>64)return 0;uint32_t at=num==1?off+2+i*12+8:t32(e+8,le);
    for(unsigned j=0;j<num;j++){unsigned char p[4];if(!srw_at(f,s->file_bytes,(uint64_t)at+j*4,p,4))return 0;queue[queued++]=t32(p,le);}continue;
   }
   unsigned bit=tag==256?1:tag==257?2:tag==258?4:tag==259?8:tag==273?16:tag==279?32:tag==277?64:0;if(!bit)continue;
   if(mask&bit)invalid=1;mask|=bit;
   if(num!=1||(type!=3&&type!=4)){invalid=1;continue;}v=type==3?t16(e+8,le):t32(e+8,le);
   switch(tag){case 256:one.width=v;break;case 257:one.height=v;break;case 258:one.bits=v;break;case 259:one.compression=v;break;case 273:one.offset=v;break;case 279:one.bytes=v;break;case 277:one.samples=v;break;}
  }
  if(one.compression==32773){if(found++||invalid||((mask&63)!=63)){*error="TIFF_RAW_AMBIGUOUS";return 0;}one.file_bytes=s->file_bytes;*s=one;}
  unsigned char next[4];if(!srw_at(f,s->file_bytes,(uint64_t)off+2+n*12,next,4))return 0;if(t32(next,le)){if(queued>=64)return 0;queue[queued++]=t32(next,le);}
 }
 if(found!=1||s->width<646||s->height<436||s->width>16384||s->height>16384||s->width%16||s->bits!=14||(s->samples!=1&&s->samples!=3)||s->bytes<16||s->offset<8||(uint64_t)s->offset+s->bytes>s->file_bytes){*error="SRW_GEOMETRY_OR_STRIP_INVALID";return 0;}
 *error="";return 1;
}
struct srw_reader {FILE *f;uint64_t start,pos,end;unsigned at,used;int error;uint32_t bitbuf;unsigned vbits;unsigned char buf[65536];};
static unsigned srw_byte(struct srw_reader *r){if(r->pos>=r->end){r->error=1;return 0;}if(r->at==r->used){size_t n=(size_t)(r->end-r->pos);if(n>sizeof r->buf)n=sizeof r->buf;r->used=(unsigned)fread(r->buf,1,n,r->f);r->at=0;if(r->used!=n){r->error=1;return 0;}}r->pos++;return r->buf[r->at++];}
static uint32_t srw_word(struct srw_reader*r){
 if(!r->error&&r->used-r->at>=4&&r->pos<=r->end&&r->end-r->pos>=4){uint32_t w;memcpy(&w,r->buf+r->at,4);r->at+=4;r->pos+=4;return w;}
 uint32_t w=srw_byte(r);w|=srw_byte(r)<<8;w|=srw_byte(r)<<16;w|=srw_byte(r)<<24;return w;
}
static unsigned srw_bits(struct srw_reader*r,unsigned n){
 if(n>16){r->error=1;return 0;}if(!n)return 0;
 if(r->vbits>=n){r->vbits-=n;return(r->bitbuf>>r->vbits)&((1u<<n)-1);}
 /* At refill only0..15 old bits remain and1..16 new bits are consumed.
  * Keep the two portions in32-bit registers: no per-sample64-bit shifts. */
 unsigned old=r->vbits,take=n-old;uint32_t v=(r->bitbuf&((1u<<old)-1))<<take,w=srw_word(r);
 r->bitbuf=w;r->vbits=32-take;return v|(w>>r->vbits);
}
static int srw_ref(const uint16_t *ring,unsigned width,int refrow,int refcol,unsigned row,unsigned nextcol,uint16_t *v){
 if(refcol<0){refrow--;refcol+=(int)width;}else if((unsigned)refcol>=width){refrow++;refcol-=(int)width;}
 if(refrow<0||refcol<0||(unsigned)refcol>=width||(unsigned)refrow+3<row||(unsigned)refrow>row||((unsigned)refrow==row&&(unsigned)refcol>=nextcol))return 0;
 *v=ring[((unsigned)refrow&3)*width+(unsigned)refcol];return 1;
}
typedef int (*srw_sink)(void *,unsigned,const uint16_t *,unsigned);
static int srw_decode(FILE *f,const struct srw_info *s,struct srw_stats *stats,srw_sink sink,void *user,const volatile sig_atomic_t *cancel,const char **error){
 memset(stats,0,sizeof *stats);stats->min_value=65535;*error="DECODER_SETUP";
 if(s->width<646||s->height<436||s->width>16384||s->height>16384||s->width%16||s->bits!=14||(s->samples!=1&&s->samples!=3)||s->compression!=32773||s->bytes<16||(uint64_t)s->offset+s->bytes>s->file_bytes)return 0;
 struct srw_reader *r=calloc(1,sizeof *r);uint16_t *ring=calloc((size_t)s->width*4,sizeof *ring);if(!r||!ring){free(r);free(ring);*error="OUT_OF_MEMORY";return 0;}
 stats->heap_bytes=sizeof *r+(uint64_t)s->width*4*sizeof *ring;r->f=f;r->pos=r->start=s->offset;r->end=(uint64_t)s->offset+s->bytes;if(fseeko(f,s->offset,SEEK_SET))goto done;
 srw_bits(r,16);srw_bits(r,4);stats->bit_depth=srw_bits(r,4)+1;unsigned max=(1u<<stats->bit_depth)-1;srw_bits(r,8);srw_bits(r,16);srw_bits(r,16);srw_bits(r,16);srw_bits(r,4);stats->optflags=srw_bits(r,4);srw_bits(r,16);srw_bits(r,8);srw_bits(r,2);stats->init_value=srw_bits(r,14);
 if(r->error||stats->bit_depth!=s->bits||stats->init_value>max){*error="RAW_HEADER_INVALID";goto done;}
 for(unsigned row=0;row<s->height;row++){
  if(cancel&&*cancel){*error="CANCELLED_OR_TIMEOUT";goto done;}
  while(((r->pos-r->start)&15)&&!r->error)srw_byte(r);if(r->error){*error="RAW_STRIP_TRUNCATED";goto done;}r->bitbuf=0;r->vbits=0;
  uint16_t *dst=ring+(row%4)*s->width;memset(dst,0,s->width*sizeof *dst);unsigned motion=7;int scale=0;unsigned mode[3][2];for(unsigned i=0;i<3;i++)mode[i][0]=mode[i][1]=row<2?7:4;
  for(unsigned col=0;col<s->width;col+=16){
   if(!(stats->optflags&4)&&!(col&63)){static const int delta[3]={0,-2,2};unsigned v=srw_bits(r,2);scale=v<3?scale+delta[v]:(int)srw_bits(r,12);if(scale<0||scale>4095){*error="RAW_SCALE_INVALID";goto done;}}
   if(stats->optflags&2)motion=srw_bits(r,1)?3:7;else if(!srw_bits(r,1))motion=srw_bits(r,3);
   if(motion==7){for(unsigned i=0;i<16;i++)dst[col+i]=col?dst[col+i-2]:(uint16_t)stats->init_value;}
   else{
    static const int offset[7]={-4,-2,-2,0,0,2,4},average[7]={0,0,1,0,1,0,0};if(row<2){*error="RAW_REFERENCE_INVALID";goto done;}
    if(col>=16&&col+32<=s->width){
     const uint16_t*prev1=ring+((row-1)&3u)*s->width,*prev2=ring+((row-2)&3u)*s->width;
     int shift=offset[motion];unsigned blend=(unsigned)average[motion];
     for(unsigned i=0;i<16;i++){
      unsigned parity=(row+i)&1u;const uint16_t*line=parity?prev2:prev1;
      int refcol=(int)(col+i)+shift+(1-(int)parity)*(1-2*(int)(i&1u));
      unsigned a=line[refcol];dst[col+i]=blend?(uint16_t)((a+line[refcol+2]+1u)>>1):(uint16_t)a;
     }
    }else{
    for(unsigned i=0;i<16;i++){
     int refrow=((row+i)&1)?(int)row-2:(int)row-1;int refcol=(int)(col+i)+offset[motion]+(1-(int)((row+i)&1))*(1-2*(int)(i&1));uint16_t a,b=0;
     if(!srw_ref(ring,s->width,refrow,refcol,row,col+i,&a)||(average[motion]&&!srw_ref(ring,s->width,refrow,refcol+2,row,col+i,&b))){*error="RAW_REFERENCE_INVALID";goto done;}dst[col+i]=average[motion]?(uint16_t)(((unsigned)a+b+1)>>1):a;
    }
    }
   }
   unsigned len[4]={0};if((stats->optflags&1)||!srw_bits(r,1)){
    unsigned flags[4];for(unsigned i=0;i<4;i++)flags[i]=srw_bits(r,2);
    for(unsigned i=0;i<4;i++){unsigned color=row%2?(i>>1):((i>>1)+2)%3;unsigned old=mode[color][0];len[i]=flags[i]==0?old:flags[i]==1?old+1:flags[i]==2?old-1:srw_bits(r,4);if(len[i]>stats->bit_depth+1){*error="RAW_DIFFERENCE_INVALID";goto done;}mode[color][0]=mode[color][1];mode[color][1]=len[i];}
   }
   for(unsigned i=0;i<16;i++){
    unsigned bits=len[i>>2],v=srw_bits(r,bits);int diff=bits?((int)(v^(1u<<(bits-1)))-(int)(1u<<(bits-1))):0;diff=diff*(scale*2+1)+scale;
    unsigned x=row%2?col+((i&7)<<1)+1-(i>>3):col+((i&7)<<1)+(i>>3);int value=(int)dst[x]+diff;dst[x]=(uint16_t)(value<0?0:value>(int)max?(int)max:value);
   }
   if(r->error){*error="RAW_STRIP_TRUNCATED";goto done;}
  }
  for(unsigned x=0;x<s->width;x++){unsigned v=dst[x];if(v<stats->min_value)stats->min_value=v;if(v>stats->max_value)stats->max_value=v;stats->zeros+=v==0;}
  if(!sink(user,row,dst,s->width)){*error="ROW_SINK_REJECTED";goto done;}stats->rows++;stats->pixels+=s->width;
 }
 *error="";
done:stats->consumed_bytes=r->pos-r->start;free(ring);free(r);return !**error;
}
#endif
