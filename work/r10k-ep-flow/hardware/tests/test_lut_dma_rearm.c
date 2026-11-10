#define _GNU_SOURCE
#define NXL_FAST_PIPELINE
#define NXL_SMALL_MODEL
/* SPDX-License-Identifier: AGPL-3.0-only; every DMA/IRQ/reset boundary is a model. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/bitb_job.h"
#include "../src/jpeg_core.h"
#include "../src/p11_engine.h"
struct model {unsigned mode,locked,tick,core,acc,loads,resets,started,notes,pending_load,dma_rearms;enum p11_call history[2];uint32_t args[2],process_call[5];uint32_t top[2048],lut[1024];unsigned char memory[BITB_BYTES];struct p11_snapshot snap;};
static uint32_t rd(void *v,unsigned b,uint32_t off){struct model *m=v;return (b?m->lut:m->top)[off/4];}
static void wr(void *v,unsigned b,uint32_t off,uint32_t n){struct model *m=v;
 if(!b&&off==0x1060){m->top[0x10d8/4]&=~n;if(n&0x80)m->pending_load=0;}
 else if(!b&&off==0x1050)m->top[0x10c8/4]&=~n;
 else if(m->mode==9&&m->started&&!b&&off==0x1104)return;
 else (b?m->lut:m->top)[off/4]=n;
}
static int state(void *v,unsigned n){struct model *m=v;return m->locked==(n!=0)&&m->core==(n==3)&&m->acc==(n==2);}
static int idle(void *v){return ((struct model*)v)->mode!=1;}
static uint32_t ms(void *v){return ++((struct model*)v)->tick;}
static void delay(void *v){((struct model*)v)->tick++;}
static void barrier(void *v){(void)v;}
static void note(void *v,uint32_t n){struct model *m=v;assert(n);m->notes++;assert(m->notes<64);}
static int call(void *v,enum p11_call f,uintptr_t a,uint32_t b,uint32_t c){struct model *m=v;(void)c;
 assert(f!=P11_SAVE);
 if(f==P11_UPDATE&&((unsigned)a==16||(unsigned)a==18))assert(!m->pending_load);
 if(f==P11_LOAD)assert(m->history[0]==P11_UPDATE&&m->args[0]==16&&m->history[1]==P11_UPDATE&&m->args[1]==18);
 m->history[0]=m->history[1];m->args[0]=m->args[1];m->history[1]=f;m->args[1]=(uint32_t)a;
 if(f==P11_VTOP)return 0xa39b0000+(uint32_t)a-0x90000000;
 if(f==P11_CLOCK||f==P11_SELECT||f==P11_UPDATE)return 0;
 if(f==P13_RESET){assert(a==3&&!m->pending_load);m->resets++;if(m->mode==10&&m->resets==2)return -1;if(m->mode==11&&m->resets==1)return -1;memset(m->lut,0,sizeof m->lut);return 0;}
 if(f==P11_DMA_RESET){assert(m->locked&&!m->acc&&!m->core&&!m->started&&!m->pending_load&&a==(m->dma_rearms?16u:1u));m->dma_rearms++;return(m->mode==20&&a==1)||(m->mode==21&&a==16)?-1:0;}
 if(f==P11_MUXR){m->top[0x1104/4]=112;return 0;}
 if(f==P11_MUXW){m->top[0x110c/4]=3;return 0;}
 if(f==P11_LOAD){assert(m->resets==1&&m->top[0x1104/4]==112&&m->top[0x110c/4]==3);assert((uint32_t)a==0x90000000+P11_ID&&b==0&&c==P20_NATIVE_FORMAT);m->pending_load=1;m->loads++;m->lut[0]=1;m->lut[1]=P20_NATIVE_FORMAT<<8;m->lut[2]=1;m->lut[3]=0xa39b2000;if(m->mode!=4)m->top[0x10d8/4]|=0x80;return 0;}
 if(f==P11_PROCESS){const uint32_t *p=(const void*)a;assert(p[0]==P20_NATIVE_FORMAT&&p[1]==0&&p[2]==0&&p[3]==0);m->process_call[0]++;memcpy(m->process_call+1,p,16);m->lut[0]=m->mode==19?0:1;m->lut[2]=0;return 0;}
 if(f==P11_RDMA||f==P11_WDMA){const uint32_t *p=(const void*)a;unsigned off=f==P11_RDMA?0x1400:0x1700;assert(p[2]==(f==P11_RDMA?SW_FRONT_STRIDE:TEST_W)&&p[3]==8&&p[5]==P20_NATIVE_FORMAT&&p[6]==1&&p[f==P11_RDMA?17:16]==63&&p[f==P11_RDMA?18:17]==31);assert(p[25]==(f==P11_RDMA)&&p[7]==(f==P11_WDMA)&&p[8]==(f==P11_RDMA));m->top[off/4]=p[0]-0x90000000+0xa39b0000;m->top[(off+4)/4]=p[1]-0x90000000+0xa39b0000;m->top[(off+8)/4]=P20_NATIVE_FORMAT<<4;m->top[(off+0x24)/4]=(m->mode==17&&f==P11_RDMA)||(m->mode==18&&f==P11_WDMA)?0:0x400020;assert(p[f==P11_RDMA?21:20]==64&&p[f==P11_RDMA?22:21]==32);if(f==P11_RDMA)m->top[(off+16)/4]=m->mode==14?0:0x100;if(m->mode==12&&f==P11_WDMA)m->top[off/4]+=16;return 0;}
 if(f==P11_START){assert(m->loads==1&&m->resets==1&&m->core&&!m->pending_load&&m->dma_rearms==2);m->started=1;for(unsigned i=0;i<P11_FRAME;i++){unsigned k=i<TEST_Y?((m->memory[i]/16*17)*17)*4+2:0;m->memory[P11_OUT+i]=i<TEST_Y?m->memory[P11_ID+k]:m->memory[i];}if(m->mode==6)m->memory[P11_OUT+P11_FRAME-1]^=1;if(m->mode==7)m->memory[17]^=1;if(m->mode==8)m->memory[BITB_BYTES-1]^=1;return 0;}
 abort();
}
static int ctl(void *v,uint32_t cmd,void *arg){struct model *m=v;
 if(cmd==0x6828){assert(!m->locked);m->locked=1;return 0;}
 if(cmd==0x6829){assert(m->locked);m->locked=0;return 0;}
 if(cmd==0x400c6809){assert(((uint32_t*)arg)[2]==7);return 0;}
 if(cmd==0x40046810){assert(*(uint32_t*)arg==7);m->acc=1;m->top[0x1080/4]|=0x80;return 0;}
 if(cmd==0x40046811){assert(*(uint32_t*)arg==7);m->acc=0;m->top[0x1080/4]&=~0x80;return 0;}
 if(cmd==0xc00c6808){assert(((uint32_t*)arg)[2]==7&&m->pending_load&&m->acc);if(m->mode==4)return -1;m->top[0x10d8/4]&=~0x80;m->pending_load=0;return 240;}
 if(cmd==0x40086807){assert(((uint32_t*)arg)[1]==5);return 0;}
 if(cmd==0x4004680e){assert(*(uint32_t*)arg==5);m->core=1;m->top[0x1070/4]|=0x20;return 0;}
 if(cmd==0x4004680f){m->core=0;m->top[0x1070/4]&=~0x20;return 0;}
 if(cmd==0x40086806){assert(m->core&&m->started);return m->mode==5?-1:490;}
 abort(); /* No ACC8 or SRAM-save wait is permitted. */
}
static void dump(const char *dir,const char *name,const void *data,size_t n){char path[1024];snprintf(path,sizeof path,"%s/%s",dir,name);FILE *f=fopen(path,"wb");assert(f&&fwrite(data,1,n,f)==n&&fclose(f)==0);}

/* Production fast-memory contract, no CPU LUT oracle. Completion is a fixture. */
int main(void){
 unsigned cases[]={0,1,4,5,11,20,21,13};
 for(unsigned ci=0;ci<sizeof cases/sizeof cases[0];ci++){
  unsigned mode=cases[ci];struct model*m=calloc(1,sizeof *m);assert(m);m->mode=mode;m->top[0x1048/4]=0xaeaaaaaa;
  if(mode==13)m->top[0x1200/4]=1;
  unsigned rounds=mode?1:3;
  for(unsigned round=0;round<rounds;round++){
   /* Retain TOP/LUT register RAM across successful rounds. Only per-run
    * bookkeeping and the new image/workspace lease are re-created. */
   m->locked=m->tick=m->core=m->acc=m->loads=m->resets=m->started=m->notes=m->pending_load=m->dma_rearms=0;
   full_initialized=full_stage=full_prepare_unsafe=0;
   memset(m->memory,0xa5,sizeof m->memory);
   struct sw_lease s={0};s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;
   s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;
   s.pa=0xa39b0000;s.va=0x91234000;s.nonce=777+ci*8+round;s.owner_pid=getpid();s.owner_start_ticks=12345;s.index=(s.pa-0x94000000u)/16384;
   s.metadata[0]=35;s.metadata[1]=SW_BYTES/16384;s.front_return_barrier=s.frame_callbacks_retired=s.controller_slot_restored=1;
   full_hash(m->memory,SW_CONTROL,s.prefix_sha256);lut_make_table(full_table,1);full_hash(full_table,sizeof full_table,s.lut_sha256);memset(s.source_sha256,'c',64);sw_seal(&s);
   assert(sw_valid(&s)&&sw_owner_proof_publish(&s));full_workspace_original=s;assert(sw_transition(&s,SW_FRONT_READY,SW_BACKEND_ACTIVE));memcpy(m->memory+SW_LEASE,&s,sizeof s);
   struct bitb_request q={BITB_REQUEST_MAGIC,BITB_WIRE_VERSION,s.pa,BITB_BYTES,123+round,234,s.va,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));
   struct bitb_result r={.ready=1,.paused_contract=1,.alias_va=0x90000000,.safe_to_release=1};
   assert(full_begin(FULL_LDC,&q,&r,m->memory));
   struct p11_ops ops={m,rd,wr,call,ctl,state,idle,ms,delay,barrier,note};
   int rc=p11_run(&q,&r,m->memory,&m->snap,&ops);
   if(!mode)assert(!rc&&r.safe_to_release&&r.restore_verified&&r.fresh_completion&&r.table_completions==1&&m->dma_rearms==2&&m->started&&m->resets==2);
   else assert(rc&&r.error);
   if(mode==1||mode==13)assert(r.safe_to_release&&!m->dma_rearms&&!m->started&&!m->resets);
   if(mode==4||mode==5)assert(!r.safe_to_release&&r.quarantine_reason==1&&m->dma_rearms==2&&m->resets==1);
   if(mode==11)assert(r.safe_to_release&&!m->dma_rearms&&!m->started&&r.error==BJ_RESET);
   if(mode==20||mode==21)assert(r.safe_to_release&&!m->started&&r.error==BJ_RESET&&m->dma_rearms==(mode==20?1u:2u)&&m->resets==2);
   assert(!m->locked&&!m->core&&!m->acc);
   printf("PASS production LUT mode=%u round=%u resets=%u DMA_rearms=%u submitted=%u safe=%u\n",mode,round,m->resets,m->dma_rearms,m->started,r.safe_to_release);
  }
  free(m);
 }
 puts("PASS10 LUT runs: three retained-register rounds, guarded RDMA1/WDMA0 rearm, reset failures and timeout quarantine; hardware is not modeled");
}
