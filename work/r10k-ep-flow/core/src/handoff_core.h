/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_HANDOFF_CORE_H
#define NXL_HANDOFF_CORE_H
#include <stdint.h>
#include <string.h>
#include "front_layout.h"
#include "batch_contract.h"
#include "front_diagnostics.h"
#include "stop_contract.h"
#include "param_dma_contract.h"
#include "still_lifecycle_audit.h"
static uint32_t p39_fnv(const void *data,unsigned n){const unsigned char *p=data;uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static int p39_extent(uint32_t pa){return pa>=0x94000000u&&!(pa&16383u)&&(uint64_t)pa+P39_CAP_BYTES<=0xbb6cc000ULL;}
static int p39_cache_packet(uint32_t pa,uint32_t nonce,uint32_t out[3]){if(!p39_extent(pa)||!nonce)return 0;out[0]=nonce;out[1]=pa+P39_SOURCE_OFFSET;out[2]=P39_SOURCE_BYTES;return 1;}
/* The pinned RTOS startup enables SCTLR.TRE and sets PRRR/NMRR below.
 * Do not mistake TEX/index4 Normal-NC for a cacheable or Device mapping.
 * AFE is enabled; AF must be set and the section must permit privileged RW.
 * Return1=Normal-NC,2=Normal-WBWA,0=unsupported/unsafe. */
static int p39_section_kind(uint32_t desc,uint32_t address){
 if((desc&3u)!=2u||(desc&0x0004e010u)||!(desc&0x400u)||(desc&0xfff00000u)!=(address&0xfff00000u))return 0;
 unsigned n=((desc>>10)&4u)|((desc>>2)&3u);const uint32_t prrr=0x000a8aa4u,nmrr=0x44e048e0u;
 if(((prrr>>(2*n))&3u)!=2u)return 0;
 unsigned inner=(nmrr>>(2*n))&3u,outer=(nmrr>>(16+2*n))&3u;
 return !inner&&!outer?1:inner==1&&outer==1?2:0;
}
static int p39_section_uncached_executable(uint32_t desc,uint32_t address){return p39_section_kind(desc,address)==1;}

static int p39_packet_extent(unsigned slot,unsigned length,unsigned *offset){if(slot>=12||!length||length>=128)return 0;*offset=P39_PACKET_START+slot*128;return *offset+length+1<P39_SOURCE_OFFSET;}
static void p48_images(unsigned char *b,uint32_t pa,uint32_t nonce,uint32_t callback,unsigned frame_index){
 uint32_t listener_off=P48_BATCH_LISTENER_OFFSET+frame_index*P48_BATCH_LISTENER_STRIDE,vtable_off=listener_off+P48_BATCH_VTABLE_DELTA;
 struct p39_context c={nonce,pa,P39_CAP_BYTES,pa+P39_RESULT_OFFSET,pa+P39_SOURCE_OFFSET,P39_SOURCE_BYTES,pa+P39_TARGET_OFFSET,P39_TARGET_BYTES,pa+P39_JOB_OFFSET,pa+listener_off,pa+vtable_off,P39_ORIGINAL,P39_SLOT,P39_MANAGER,P39_CACHE,P39_TASK,pa+callback};
 memcpy(b+P39_CONTEXT_OFFSET,&c,sizeof c);memset(b+P39_RESULT_OFFSET,0,256);memset(b+P39_STOP_OFFSET,0,P39_STOP_BYTES);memset(b+P39_STOP_COMMAND_OFFSET,0,32);memset(b+P39_DIAG_OFFSET,0,P39_DIAG_BYTES);memset(b+P39_JOB_OFFSET,0,128);
 memset(b+P49_PD_OFFSET,0,P49_PD_BYTES);memset(b+SLA_OFFSET,0,SLA_BYTES);
 /* One whole-frame descriptor; the original driver computes the 14x3 grid. */
 for(unsigned k=0;k<2;k++){
  unsigned at=k?P39_OUTPUT_IMAGE_OFFSET:P39_INPUT_IMAGE_OFFSET;
  unsigned char *im=b+at;memset(im,0,176);uint32_t *w=(uint32_t*)im;
  w[0]=0x80588910u;w[1]=k?P39_TARGET_STRIDE:P39_SOURCE_W;w[2]=k?P39_H:P39_SOURCE_H;w[3]=w[5]=P39_W;w[4]=w[6]=P39_H;
  w[7]=P39_TILE_W;w[8]=P39_TILE_H;w[11]=k?0:88;w[12]=k?0:32;im[60]=1;im[64]=3;im[65]=4;im[66]=1;im[104]=2;
  w[18]=k?P39_TARGET_STRIDE:P39_SOURCE_STRIDE;w[27]=pa+(k?P39_TARGET_OFFSET:P39_SOURCE_OFFSET);w[36]=k?P39_TARGET_BYTES:P39_SOURCE_BYTES;
  if(k){im[60]=2;im[64]=1;im[65]=im[66]=im[67]=0;w[17]=1;w[29]=pa+P39_TARGET_OFFSET+P39_UV_OFFSET;w[36]=2*P39_PLANE_BYTES;}
 }
 for(unsigned v=0;v<P39_CONFIG_VARIANTS;v++){
  uint32_t *q=(uint32_t*)(b+P39_JOB_OFFSET+v*32u);q[0]=0x04000001u;q[2]=pa+P39_INPUT_IMAGE_OFFSET;q[4]=pa+P39_OUTPUT_IMAGE_OFFSET+v*256u;b[P39_JOB_OFFSET+v*32u+6]=1;
 }
 memset(b+listener_off,0,64);uint32_t *listener=(uint32_t*)(b+listener_off);listener[0]=pa+vtable_off;listener[1]=pa+P39_RESULT_OFFSET;listener[2]=nonce;listener[3]=frame_index+1;
 memset(b+vtable_off,0,16);((uint32_t*)(b+vtable_off))[2]=pa+callback;
 /* Source and full planes are streamed directly into the owned CAP alias. */
}
static void p39_images(unsigned char *b,uint32_t pa,uint32_t nonce,uint32_t callback){p48_images(b,pa,nonce,callback,0);}
static int p39_result_valid(const struct p39_result *r,uint32_t pa,uint32_t nonce,uint32_t source_hash,uint32_t target_hash){
 const uint32_t *w=r->w;if(w[0]!=P39_MAGIC||w[1]!=1||w[2]!=nonce||w[3]!=P39_DONE||!p39_extent(pa)||!nonce)return 0;
 for(unsigned i=4;i<=9;i++)if((int32_t)w[i]<0)return 0;
 if(w[8]||w[9]||w[10]||w[11]||w[12]!=1||w[13]||w[14]!=1||w[15]||w[16]!=(nonce^P39_GO_XOR)||w[17]||w[18]!=1||w[19]!=1||w[21]!=~nonce||w[22]!=source_hash||w[23]!=target_hash||w[24]!=pa||w[25]!=pa+P39_SOURCE_OFFSET||w[26]!=pa+P39_TARGET_OFFSET||w[27]!=P39_SOURCE_BYTES||w[28]!=P39_TARGET_BYTES||(w[29]&255)!=7)return 0;
 return w[32]==pa+P39_SOURCE_OFFSET&&w[34]==P39_SOURCE_STRIDE&&w[41]==pa+P39_TARGET_OFFSET&&w[43]==P39_TARGET_STRIDE&&((w[35]>>8)&7)==4&&(w[44]&0x7f3u)==0x41u&&w[42]==pa+P39_TARGET_OFFSET+P39_UV_OFFSET&&(w[51]&15u)==5u&&(w[50]&255u)==1&&w[53]==P39_W&&w[54]==P39_H&&w[57]==P39_TILE_W&&w[58]==P39_TILE_H&&w[59]==14&&w[60]==3;
}
static int p39_output_index(unsigned i){return i<P39_PLANE_BYTES||(i>=P39_UV_OFFSET&&i<P39_UV_OFFSET+P39_PLANE_BYTES);}
static int p39_diagnostics_valid(const uint32_t *d,uint32_t nonce,uint32_t pa){
 for(unsigned k=0;k<P39_DIAG_SLOTS;k++){
  const uint32_t *p=d+k*P39_DIAG_WORDS;
  if(k>=2&&k<=4){for(unsigned j=0;j<P39_DIAG_WORDS;j++)if(p[j])return 0;continue;}
  if(p[0]!=P39_DIAG_MAGIC||p[1]!=k||p[2]!=nonce||p[3]!=1)return 0;
  if(k!=6&&k!=7){if(p[4]!=0xffffffffu||p[5])return 0;for(unsigned j=6;j<12;j++)if(p[j])return 0;}
  else if(p[6]<0x81000000u||p[6]>=0x93fff000u||(p[6]&7u))return 0;
  for(unsigned j=12+P39_DIAG_READS;j<P39_DIAG_WORDS;j++)if(p[j])return 0;
 }
 for(unsigned v=0;v<P39_CONFIG_VARIANTS;v++){
  const uint32_t *p=d+(1+v)*P39_DIAG_WORDS;
  if(p[12]!=pa+P39_SOURCE_OFFSET||p[14]!=P39_SOURCE_STRIDE||p[21]!=pa+P39_TARGET_OFFSET||p[22]!=pa+P39_TARGET_OFFSET+P39_UV_OFFSET||p[23]!=P39_TARGET_STRIDE||(p[24]&0x7f3u)!=0x41u||p[62]!=p[12]||p[64]!=p[14]||p[65]!=p[15]||p[71]!=p[21]||p[72]!=p[22]||p[73]!=p[23]||p[74]!=p[24])return 0;
 }
 const uint32_t *f=d+6*P39_DIAG_WORDS,*m=d+7*P39_DIAG_WORDS;
 return f[4]==0&&f[5]==0&&f[7]==1&&f[8]==5&&(f[9]&(1u<<5))&&f[10]==0x20810418u&&f[11]==1&&m[4]==1&&m[5]==0&&m[7]==1&&m[8]==5&&(m[9]&(1u<<5))&&!(m[9]&(1u<<7))&&m[10]==0x2081040cu&&m[11]==2;
}
#endif
