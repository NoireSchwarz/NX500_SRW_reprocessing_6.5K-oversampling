/* SPDX-License-Identifier: AGPL-3.0-only. CPU-owned test layout, not DMA-ready. */
#ifndef P31_LAYOUT_H
#define P31_LAYOUT_H
#include <stdint.h>
#include <string.h>
#define P31_STAGE_WIDTH 6608u
#define P31_STAGE_HEIGHT 4384u
#define P31_STAGE_STRIDE 13216u
#define P31_STAGE_BYTES 57938944u
#define P31_RAW_WIDTH 6496u
#define P31_RAW_HEIGHT 4336u
#define P31_OFFSET_X 0u
#define P31_OFFSET_Y 0u
#define P31_BORDER_VALUE 128u
#define P31_STAGE_HASH "846a5874e057bf9b264d8d3a7b0372145f9f31cfd5d782edcbd85e91605db3a9"
/* Synthetic border for layout testing only. Real optical-black pixels are
 * absent from SRW and this fill has not been validated with the native ISP. */
static void p31_layout_row(unsigned char *out,const unsigned char *raw){
 if(raw){
  for(unsigned x=0;x<P31_OFFSET_X;x++){out[x*2]=P31_BORDER_VALUE;out[x*2+1]=0;}
  memcpy(out+P31_OFFSET_X*2,raw,P31_RAW_WIDTH*2);
  for(unsigned x=P31_OFFSET_X+P31_RAW_WIDTH;x<P31_STAGE_WIDTH;x++){out[x*2]=P31_BORDER_VALUE;out[x*2+1]=0;}
 }else for(unsigned x=0;x<P31_STAGE_WIDTH;x++){out[x*2]=P31_BORDER_VALUE;out[x*2+1]=0;}
}
#endif
