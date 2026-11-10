/* SPDX-License-Identifier: AGPL-3.0-only
 * CPU reference path, NOT the EP 3DLUT engine. Native Cr,Cb,Y table encoding
 * follows the existing metol native_codec specification; no RGB reinterpretation.
 * This diagnostic defines its own Y + interleaved Cb,Cr 4:2:0 test layout.
 */
#ifndef NXL_LUT_CPU_H
#define NXL_LUT_CPU_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define LUT_GRID 17u
#define LUT_NODES 4913u
#define LUT_DATA_BYTES 19652u
#define LUT_SLOT_BYTES 19712u
#define LUT_FRAME_BYTES 3072u
static unsigned lut_grid(unsigned i){return i==16?255:i*16;}
static void lut_make_table(unsigned char out[LUT_SLOT_BYTES],int identity){
 memset(out,0,LUT_SLOT_BYTES);
 for(unsigned z=0;z<17;z++)for(unsigned y=0;y<17;y++)for(unsigned x=0;x<17;x++){
  unsigned cr=lut_grid(x),cb=lut_grid(y),luma=lut_grid(z),i=((z*17+y)*17+x)*4;
  unsigned yy=(luma*luma+2*cb*cr+12240)/765;
  out[i]=(unsigned char)(identity?cr:(3*cr+cb+luma+2)/5);
  out[i+1]=(unsigned char)(identity?cb:(2*cb+cr+255-luma+1)/4);
  out[i+2]=(unsigned char)(identity?luma:(yy>255?255:yy));
 }
 for(unsigned i=0;i<12;i++)out[LUT_DATA_BYTES+i]=out[LUT_DATA_BYTES-4+i%4];
}
static int lut_table_valid(const unsigned char *p,size_t n){
 if(!p||n!=LUT_SLOT_BYTES)return 0;
 for(unsigned i=3;i<LUT_DATA_BYTES;i+=4)if(p[i])return 0;
 for(unsigned i=0;i<12;i++)if(p[LUT_DATA_BYTES+i]!=p[LUT_DATA_BYTES-4+i%4])return 0;
 for(unsigned i=LUT_DATA_BYTES+12;i<LUT_SLOT_BYTES;i++)if(p[i])return 0;
 return 1;
}
/* Quotient is known to be in [0,255]. No FPU or dynamic libgcc division ABI. */
static unsigned lut_round_byte(uint32_t numerator,uint32_t denominator){
 uint32_t n=numerator+(denominator>>1),q=0;
 for(uint32_t bit=128;bit;bit>>=1){uint32_t candidate=q|bit;if(candidate*denominator<=n)q=candidate;}
 return q;
}
static void lut_sample(const unsigned char *table,unsigned cr,unsigned cb,unsigned yy,unsigned char out[3]){
 unsigned v[3]={cr,cb,yy},lo[3],distance[3],span[3];uint32_t den=1,sum[3]={0,0,0};
 for(unsigned a=0;a<3;a++){lo[a]=v[a]>>4;if(lo[a]>15)lo[a]=15;distance[a]=v[a]-lo[a]*16;span[a]=lo[a]==15?15:16;den*=span[a];}
 for(unsigned z=0;z<2;z++)for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++){
  uint32_t w=(x?distance[0]:span[0]-distance[0])*(y?distance[1]:span[1]-distance[1])*(z?distance[2]:span[2]-distance[2]);
  unsigned i=(((lo[2]+z)*17+lo[1]+y)*17+lo[0]+x)*4;
  for(unsigned c=0;c<3;c++)sum[c]+=table[i+c]*w;
 }
 for(unsigned c=0;c<3;c++)out[c]=(unsigned char)lut_round_byte(sum[c],den);
}
static int lut420_apply(const unsigned char *table,size_t table_bytes,const volatile unsigned char *src,size_t src_bytes,volatile unsigned char *dst,size_t dst_bytes){
 if(!lut_table_valid(table,table_bytes)||!src||!dst||src_bytes!=LUT_FRAME_BYTES||dst_bytes!=LUT_FRAME_BYTES)return 0;
 uintptr_t a=(uintptr_t)src,b=(uintptr_t)dst;
 if(a>UINTPTR_MAX-LUT_FRAME_BYTES||b>UINTPTR_MAX-LUT_FRAME_BYTES||!(a+LUT_FRAME_BYTES<=b||b+LUT_FRAME_BYTES<=a))return 0;
 for(unsigned cy=0;cy<16;cy++)for(unsigned cx=0;cx<32;cx++){
  unsigned chroma=2048+cy*64+cx*2,cb=src[chroma],cr=src[chroma+1],cb_sum=0,cr_sum=0;
  for(unsigned dy=0;dy<2;dy++)for(unsigned dx=0;dx<2;dx++){
   unsigned i=(cy*2+dy)*64+cx*2+dx;unsigned char o[3];lut_sample(table,cr,cb,src[i],o);
   dst[i]=o[2];cr_sum+=o[0];cb_sum+=o[1];
  }
  dst[chroma]=(unsigned char)((cb_sum+2)>>2);dst[chroma+1]=(unsigned char)((cr_sum+2)>>2);
 }
 return 1;
}
static void lut_make_input(unsigned char out[LUT_FRAME_BYTES],uint32_t nonce){
 static const unsigned edge[16]={0,1,15,16,17,31,127,128,239,240,241,247,248,253,254,255};
 for(unsigned y=0;y<32;y++)for(unsigned x=0;x<64;x++)out[y*64+x]=(unsigned char)(x<16?edge[(x+y)&15]:(x*29+y*47+(nonce>>((y&3)*8)))&255);
 for(unsigned y=0;y<16;y++)for(unsigned x=0;x<32;x++){
  out[2048+y*64+x*2]=(unsigned char)(x<16?edge[(x+y*3)&15]:(x*43+y*11+(nonce&255))&255);
  out[2048+y*64+x*2+1]=(unsigned char)(y<8?edge[(x*3+y)&15]:(x*13+y*37+((nonce>>8)&255))&255);
 }
}
#endif
