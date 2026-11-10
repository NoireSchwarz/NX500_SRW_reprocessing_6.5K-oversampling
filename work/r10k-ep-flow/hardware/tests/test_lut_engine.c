/* SPDX-License-Identifier: AGPL-3.0-only; every DMA/IRQ/reset boundary is a model. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/bitb_job.h"
#include "../src/jpeg_core.h"
#include "../src/p11_engine.h"
struct model {unsigned mode,locked,tick,core,acc,loads,resets,started,notes,pending_load;enum p11_call history[2];uint32_t args[2],process_call[5];uint32_t top[2048],lut[1024];unsigned char memory[BITB_BYTES];struct p11_snapshot snap;};
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
 if(f==P11_MUXR){m->top[0x1104/4]=112;return 0;}
 if(f==P11_MUXW){m->top[0x110c/4]=3;return 0;}
 if(f==P11_LOAD){assert(m->resets==1&&m->top[0x1104/4]==112&&m->top[0x110c/4]==3);assert((uint32_t)a==0x90002000&&b==0&&c==1);m->pending_load=1;m->loads++;m->lut[0]=1;m->lut[1]=m->mode==15?0:0x100;m->lut[2]=1;m->lut[3]=0xa39b2000;if(m->mode!=4)m->top[0x10d8/4]|=0x80;return 0;}
 if(f==P11_PROCESS){const uint32_t *p=(const void*)a;assert(p[0]==1&&p[1]==0&&p[2]==0&&p[3]==0);m->process_call[0]++;memcpy(m->process_call+1,p,16);m->lut[0]=m->mode==19?0:1;m->lut[2]=0;return 0;}
 if(f==P11_RDMA||f==P11_WDMA){const uint32_t *p=(const void*)a;unsigned off=f==P11_RDMA?0x1400:0x1700;assert(p[2]==64&&p[3]==8&&p[5]==1&&p[6]==1&&p[f==P11_RDMA?17:16]==63&&p[f==P11_RDMA?18:17]==31);assert(p[25]==(f==P11_RDMA)&&p[7]==(f==P11_WDMA)&&p[8]==(f==P11_RDMA));m->top[off/4]=p[0]-0x90000000+0xa39b0000;m->top[(off+4)/4]=p[1]-0x90000000+0xa39b0000;m->top[(off+8)/4]=m->mode==16&&f==P11_RDMA?0:0x10;m->top[(off+0x24)/4]=(m->mode==17&&f==P11_RDMA)||(m->mode==18&&f==P11_WDMA)?0:0x400020;assert(p[f==P11_RDMA?21:20]==64&&p[f==P11_RDMA?22:21]==32);if(f==P11_RDMA)m->top[(off+16)/4]=m->mode==14?0:0x100;if(m->mode==12&&f==P11_WDMA)m->top[off/4]+=16;return 0;}
 if(f==P11_START){assert(m->loads==1&&m->resets==1&&m->core&&!m->pending_load);m->started=1;for(unsigned i=0;i<P11_FRAME;i++){unsigned k=i<TEST_Y?((m->memory[i]/16*17)*17)*4+2:0;m->memory[P11_OUT+i]=i<TEST_Y?m->memory[P11_ID+k]:m->memory[i];}if(m->mode==6)m->memory[P11_OUT+P11_FRAME-1]^=1;if(m->mode==7)m->memory[17]^=1;if(m->mode==8)m->memory[BITB_BYTES-1]^=1;return 0;}
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
int main(void){
 for(unsigned mode=0;mode<=19;mode++){
  struct model *m=calloc(1,sizeof *m);assert(m);m->mode=mode;m->top[0x1048/4]=0xaeaaaaaa;
  if(mode==2)m->lut[0]=1;if(mode==3)m->top[0x1020/4]=16;if(mode==13)m->top[0x1200/4]=1;
  struct bitb_request q={BITB_REQUEST_MAGIC,10,0xa39b0000,BITB_BYTES,123,234,0x91234000,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));
  struct bitb_result r={.ready=1,.paused_contract=1,.alias_va=0x90000000,.safe_to_release=1};struct p11_ops ops={m,rd,wr,call,ctl,state,idle,ms,delay,barrier,note};
  free(full_pad);full_pad=NULL;full_pad_bytes=full_initialized=full_stage=full_prepare_unsafe=0;full_cache(q.nonce);full_initialized=1;full_stage=FULL_REFERENCE;for(unsigned off=0;off<BITB_BYTES;off+=TEST_BLOCK)full_render(m->memory+off,off,0,NULL,0);
  int rc=p11_run(&q,&r,m->memory,&m->snap,&ops);
  if(!mode){assert(!rc&&r.target_ok&&r.source_ok&&r.guards_ok&&r.safe_to_release&&r.fresh_completion&&r.inactive_start_verified&&r.backup_skipped&&r.cold_restart_required&&r.local_reset_calls==2&&r.table_operations==1&&!r.load_poll_completions&&r.table_completions==1&&!r.bank_saved&&!r.bank_restored&&!r.table_readback&&r.restore_verified&&r.output_changed==P11_FRAME);}
  else assert(rc&&r.error);
  if(mode==1||mode==2||mode==3||mode==13)assert(!r.local_reset_calls&&!r.cold_restart_required&&!r.mmio_modified&&r.safe_to_release);
  if(mode==4||mode==5)assert(!r.safe_to_release&&r.quarantine_reason==1&&r.local_reset_calls==1);
  if(mode==6||mode==7||mode==8||mode==11||mode==12||mode==14||mode==15||mode==16||mode==17||mode==18||mode==19)assert(r.safe_to_release&&r.local_reset_calls==2);
  if(mode==14)assert(!r.submission_attempted&&!m->started&&r.error==BJ_PREPARE);
  if(mode==15)assert(!r.submission_attempted&&!m->started&&r.error==BJ_LUT&&r.failed_stage==3);
  if(mode>=16&&mode<=19)assert(!r.submission_attempted&&!m->started&&r.error==BJ_PREPARE&&r.failed_stage==5);
  if(mode==9||mode==10)assert(!r.safe_to_release);
  assert(!m->locked&&!m->core&&!m->acc);
  free(m);
 }
 puts("PASS 20 nonidentity LUT controls, geometry/readback gates, Still format pairing/readback, initialized load/pixel, timeout, full-output/guard comparison and cleanup models; no physical DMA/IRQ/reset");
}
