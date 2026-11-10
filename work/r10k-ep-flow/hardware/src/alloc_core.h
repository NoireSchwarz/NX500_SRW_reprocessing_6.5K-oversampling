/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_ALLOC_CORE_H
#define NXL_ALLOC_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "test_geometry.h"
#define ALLOC_BYTES TEST_BYTES
#define ALLOC_SPAN_BLOCKS TEST_PAGES
#define ALLOC_BLOCKS 45056u
/* Free capacity is a contiguous extent in the stable allocator table. The
 * free-count field alone and a fixed 512-block reserve cannot prove it. */
static int alloc_largest_free_span(const uint32_t *table,unsigned *largest){
 if(!table||!largest)return 0;unsigned run=0,covered=0;*largest=0;
 for(unsigned i=0;i<ALLOC_BLOCKS;i++){
  unsigned kind=table[4*i]&255u,span=table[4*i+1];
  if(kind){
   if(!span||span>ALLOC_BLOCKS-i){*largest=0;return 0;}
   /* OEM busy interiors may repeat kind and remaining span. Mark the union
    * of all busy extents; overlapping metadata never creates free space. */
   if(i+span>covered)covered=i+span;
  }
  if(i>=covered&&!kind&&!span){run++;if(run>*largest)*largest=run;}
  else run=0;
 }
 return 1;
}
#define ALLOC_ENTRY 0xc6c8u
#define ALLOC_RETURN 0x715cu
#define ALLOC_REQUEST 0x4ccb0u
#define ALLOC_RELEASE 0x4cd04u
#define ALLOC_FLUSH 0x4cd84u
#define ALLOC_MEMCPY 0x74e30u
#define ALLOC_SAMPLE_BYTES 128u
#define EP_OPEN_COUNT_FN 0x1c0c8u
#define EP_SMA_VTOP_FN 0x1bc30u
#define EP_SMA_FD_DATA 0x55574u
#define EP_OPEN_COUNT_DATA 0x5708cu
#include "test_geometry.h"
#define EP_TEST_WIDTH TEST_W
#define EP_TEST_HEIGHT TEST_H
#define EP_TEST_Y_BYTES (EP_TEST_WIDTH*EP_TEST_HEIGHT)
#define EP_TEST_C_BYTES EP_TEST_Y_BYTES
struct ep_candidate {uint32_t width,height,stride,y,c,type;};
_Static_assert(sizeof(struct ep_candidate)==24,"native CYcc object layout");
static int ep_make_candidates(uint32_t va,uint32_t pa,struct ep_candidate desc[2]) {
 if(!va||!pa||(va&0x3fffu)||(pa&0x3fffu)||(uint64_t)va+ALLOC_BYTES>UINT32_MAX||
    (uint64_t)pa+ALLOC_BYTES>UINT32_MAX)return 0;
 for(unsigned i=0;i<2;i++)desc[i]=(struct ep_candidate){EP_TEST_WIDTH,EP_TEST_HEIGHT,EP_TEST_WIDTH,va+i*TEST_OUT,va+i*TEST_OUT+EP_TEST_Y_BYTES,1};
 return TEST_OUT+EP_TEST_Y_BYTES+EP_TEST_C_BYTES<=ALLOC_BYTES;
}
#define ALLOC_TRAP 0xe7f001f0u
#define ALLOC_QUEUE_KEY 0x8828
struct alloc_regs {uint32_t r[18];};
struct alloc_message {int32_t type;uint32_t argc;unsigned char args[10][20];};
_Static_assert(sizeof(struct alloc_message)==208,"native shell message layout");
static void alloc_make_message(struct alloc_message *m,uint32_t nonce) {
 memset(m,0,sizeof *m);m->type=1;m->argc=3;
 memcpy(m->args[0],"st",3);memcpy(m->args[1],"app",4);
 uint32_t info[5]={0,0,ALLOC_BYTES,2,0};memcpy(m->args[2],info,20);
 memcpy(m->args[9],"NXL035",6);memcpy(m->args[9]+8,&nonce,4);
}
/* Exact native libc 2.13 msgrcv SVC/return sites. The cancellation path
 * keeps its original caller LR at SP+8; the fast path keeps it in r14.
 * A matching thread name alone never authorizes a memory request. */
static int alloc_parked_regs(const struct alloc_regs *r,uint32_t libc_base,
                            uint32_t shell_base,int queue,uint32_t saved_lr) {
 if((r->r[16]&0x20)||r->r[7]!=302||r->r[17]!=(uint32_t)queue||
    r->r[2]!=208||r->r[3]||r->r[4]||(r->r[13]&3)||r->r[1]<r->r[13])return 0;
 uint32_t pc=r->r[15]-libc_base,delta=r->r[1]-r->r[13];
 if(pc==0xc5be0||pc==0xc5be4)
  return delta==0x114&&r->r[14]==shell_base+0x7344;
 if(pc==0xc5c0c||pc==0xc5c10)
  /* r14 is call-clobbered: the native cancellation helper returns through
   * its saved stack PC and may leave a kernel-cmpxchg continuation in LR.
   * The authoritative original caller is the saved LR at SP+8. */
  return delta==0x120&&saved_lr==shell_base+0x7344;
 return 0;
}
static int alloc_entry_regs(const struct alloc_regs *r,uint32_t base) {
 return (r->r[15]==base+ALLOC_ENTRY||r->r[15]==base+ALLOC_ENTRY+4)&&
  r->r[14]==base+ALLOC_RETURN&&r->r[0]==2&&!(r->r[16]&0x20)&&!(r->r[13]&7);
}
static struct alloc_regs alloc_call(const struct alloc_regs *original,uint32_t fn,uint32_t arg) {
 struct alloc_regs r=*original;r.r[0]=arg;r.r[15]=fn;r.r[17]=0xffffffffu;return r;
}
static struct alloc_regs alloc_call3(const struct alloc_regs *original,uint32_t fn,uint32_t a,uint32_t b,uint32_t c) {
 struct alloc_regs r=alloc_call(original,fn,a);r.r[1]=b;r.r[2]=c;return r;
}
static int alloc_sample_range(uint32_t va,uint32_t offset,uint32_t bytes,uint32_t *address) {
 if(!va||(va&0x3fffu)||bytes!=ALLOC_SAMPLE_BYTES||(offset&31u)||offset>ALLOC_BYTES-bytes||
    (uint64_t)va+ALLOC_BYTES>UINT32_MAX)return 0;
 *address=va+offset;return 1;
}
static void alloc_pattern(unsigned char out[ALLOC_SAMPLE_BYTES],uint32_t nonce,unsigned round) {
 for(unsigned i=0;i<ALLOC_SAMPLE_BYTES;i++)out[i]=(unsigned char)((i*73u+(i>>2)*19u+(nonce>>((i&3u)*8u)))^(round?0xa7u:0x39u));
}
/* Full baseline coverage: an unmarked interior block is not necessarily free. */
static int alloc_was_free(const uint32_t *table,uint32_t index) {
 if(index>=ALLOC_BLOCKS)return 0;
 for(uint32_t i=0;i<=index;i++) {
  uint32_t kind=table[i*4]&255u,span=table[i*4+1];
  if(kind&&(!span||span>ALLOC_BLOCKS-i))return 0;
  if(kind&&index-i<span)return 0;
 }
 return !(table[index*4]&255u)&&table[index*4+1]==0;
}
static int alloc_response(const uint32_t info[5],uint32_t mapped_pa,uint32_t index,
                          const uint32_t *before,const uint32_t entry[4]) {
 if(index>ALLOC_BLOCKS-ALLOC_SPAN_BLOCKS)return 0;
 if(index>ALLOC_BLOCKS-ALLOC_SPAN_BLOCKS)return 0;
 for(uint32_t i=0;i<index+ALLOC_SPAN_BLOCKS;i++){
  uint32_t kind=before[i*4]&255u,span=before[i*4+1];
  if(kind&&(!span||span>ALLOC_BLOCKS-i))return 0;
  if(kind&&i+span>index)return 0;
  if(!kind&&i>=index&&span)return 0;
 }
 return info[0]&&info[1]==mapped_pa&&info[2]==ALLOC_BYTES&&info[3]==2&&
  (entry[0]&255u)==0x23u&&entry[1]==ALLOC_SPAN_BLOCKS;
}
#endif
