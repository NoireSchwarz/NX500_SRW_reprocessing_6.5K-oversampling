/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_JPEG_CORE_H
#define NXL_JPEG_CORE_H
#include "test_geometry.h"
#define JPEG_W TEST_W
#define JPEG_H TEST_H
#define JPEG_Y (JPEG_W*JPEG_H)
#define JPEG_FRAME (JPEG_Y*2u)
#define JPEG_OUT TEST_JPEG_OUT
#define JPEG_CAP TEST_JPEG_CAP
#define JPEG_PAD 512u
#define JPEG_META_MAGIC 0x3147504au
struct jpeg_meta {
 uint32_t magic,version,bytes,width,height,format,native_return,stream_bits,stream_bytes;
 uint32_t structural,source_ok,guards_ok,changed_extent,saved,old_selector;
 uint32_t error_before,error_after,quant_crc,crc;
};
/* Structural validation only. Entropy decoding must additionally pass on host.
 * One baseline, three-component 4:2:0 scan; a hardware stream needs its tables.
 */
struct jpeg_info {uint32_t width,height,sampling[3],format,structural;};
static int jpeg_parse_info(const unsigned char *p,unsigned n,struct jpeg_info *info){
 memset(info,0,sizeof *info);if(n<32||p[0]!=255||p[1]!=0xd8)return 0;
 unsigned pos=2,sof=0,sos=0,dqt=0,dht=0;
 while(pos<n){
  if(p[pos++]!=255)return 0;while(pos<n&&p[pos]==255)pos++;if(pos>=n)return 0;unsigned marker=p[pos++];
  if(marker==0xd9){if(pos!=n||sof!=1||sos!=1||!dqt||!dht)return 0;
   info->format=info->sampling[0]==0x21&&info->sampling[1]==0x11&&info->sampling[2]==0x11?422u:(info->sampling[0]==0x22&&info->sampling[1]==0x11&&info->sampling[2]==0x11?420u:0u);
   info->structural=1;return 1;}
  if(marker==0||marker==0xd8||(marker>=0xd0&&marker<=0xd7)||pos+2>n)return 0;
  unsigned len=((unsigned)p[pos]<<8)|p[pos+1];if(len<2||len>n-pos)return 0;
  if(marker==0xc0){
   if(++sof!=1||len!=17||p[pos+2]!=8||p[pos+7]!=3)return 0;
   info->height=((unsigned)p[pos+3]<<8)|p[pos+4];info->width=((unsigned)p[pos+5]<<8)|p[pos+6];
   if(!info->width||!info->height)return 0;for(unsigned k=0;k<3;k++)info->sampling[k]=p[pos+9+3*k];
  }else if(marker==0xdb)dqt++;
  else if(marker==0xc4)dht++;
  else if(marker==0xda){
   if(!sof||++sos!=1||len!=12||p[pos+2]!=3||p[pos+9]!=0||p[pos+10]!=63||p[pos+11]!=0)return 0;
   pos+=len;while(pos<n){if(p[pos++]!=255)continue;unsigned start=pos-1;while(pos<n&&p[pos]==255)pos++;if(pos>=n)return 0;unsigned next=p[pos++];if(next==0||(next>=0xd0&&next<=0xd7))continue;pos=start;break;}continue;
  }else if(marker>=0xc0&&marker<=0xcf&&marker!=0xc4)return 0;
  pos+=len;
 }return 0;
}
static int jpeg_parse(const unsigned char *p,unsigned n){struct jpeg_info info;return jpeg_parse_info(p,n,&info)&&info.width==JPEG_W&&info.height==JPEG_H&&info.format==422;}
static void p23_color_frame(unsigned char *p){
 for(unsigned y=0;y<TEST_H;y++)for(unsigned x=0;x<TEST_W;x++)p[y*TEST_W+x]=(unsigned char)(((x<TEST_W/2)==(y<TEST_H/2))?80:176);
 for(unsigned y=0;y<TEST_H/2;y++)for(unsigned x=0;x<TEST_W/2;x++){p[TEST_Y+y*TEST_W+2*x]=x<TEST_W/4?112:160;p[TEST_Y+y*TEST_W+2*x+1]=x<TEST_W/4?160:112;}
}
static void p23_effect_frame(const unsigned char *in,unsigned char *out){for(unsigned i=0;i<TEST_FRAME;i++)out[i]=i<TEST_Y?(in[i]<240?(unsigned char)(240-in[i]):0):in[i];}
static void jpeg_init(unsigned char *p,uint32_t nonce){for(unsigned i=0;i<BITB_BYTES;i++)p[i]=bitb_pattern(i,nonce);p23_color_frame(p);}
static int jpeg_buffers_at(const unsigned char *before,const unsigned char *after,unsigned n,unsigned source,struct bitb_result *r){
 if(source!=0&&source!=TEST_OUT)return 0;
 if(n<32||n>JPEG_CAP-JPEG_PAD)return 0;
 unsigned allowed=(n+JPEG_PAD-1)&~(JPEG_PAD-1);
 r->source_ok=r->guards_ok=1;r->output_changed=0;
 for(unsigned i=0;i<BITB_BYTES;i++){
  if(i>=JPEG_OUT&&i<JPEG_OUT+allowed){r->output_changed+=before[i]!=after[i];continue;}
  if(before[i]!=after[i]){if(i>=source&&i<source+JPEG_FRAME)r->source_ok=0;else r->guards_ok=0;}
 }
 r->source_crc=bitb_crc(before+source,JPEG_FRAME);
 r->target_crc=bitb_crc(after+JPEG_OUT,n);
 r->target_ok=jpeg_parse(after+JPEG_OUT,n);
 return r->source_ok&&r->guards_ok&&r->target_ok&&r->output_changed;
}
#endif
