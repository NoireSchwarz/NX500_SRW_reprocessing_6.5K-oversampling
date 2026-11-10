/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_ALLOC_CORE_H
#define NXL_ALLOC_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ALLOC_BYTES 16384u
#define ALLOC_BLOCKS 45056u
#define ALLOC_ENTRY 0xc6c8u
#define ALLOC_RETURN 0x715cu
#define ALLOC_REQUEST 0x4ccb0u
#define ALLOC_RELEASE 0x4cd04u
#define ALLOC_TRAP 0xe1200070u
#define ALLOC_QUEUE_KEY 0x8828
struct alloc_regs {uint32_t r[18];};
struct alloc_message {int32_t type;uint32_t argc;unsigned char args[10][20];};
_Static_assert(sizeof(struct alloc_message)==208,"native shell message layout");
static void alloc_make_message(struct alloc_message *m,uint32_t nonce) {
 memset(m,0,sizeof *m);m->type=1;m->argc=3;
 memcpy(m->args[0],"st",3);memcpy(m->args[1],"app",4);
 uint32_t info[5]={0,0,ALLOC_BYTES,2,0};memcpy(m->args[2],info,20);
 memcpy(m->args[9],"NXL034",6);memcpy(m->args[9]+8,&nonce,4);
}
static int alloc_entry_regs(const struct alloc_regs *r,uint32_t base) {
 return (r->r[15]==base+ALLOC_ENTRY||r->r[15]==base+ALLOC_ENTRY+4)&&
  r->r[14]==base+ALLOC_RETURN&&r->r[0]==2&&!(r->r[16]&0x20)&&!(r->r[13]&7);
}
static struct alloc_regs alloc_call(const struct alloc_regs *original,uint32_t fn,uint32_t arg) {
 struct alloc_regs r=*original;r.r[0]=arg;r.r[15]=fn;r.r[17]=0xffffffffu;return r;
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
 return info[0]&&info[1]==mapped_pa&&info[2]==ALLOC_BYTES&&info[3]==2&&
  index<ALLOC_BLOCKS&&
  alloc_was_free(before,index)&&(entry[0]&255u)==0x23u&&entry[1]==1;
}
#endif
