/* SPDX-License-Identifier: AGPL-3.0-only
 * Actual orchestration/controller verifier with mocked hardware-engine calls.
 * JPEG fixtures are software generated, not hardware evidence.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include "bitb_job.h"
#define jpeg_run_input jpeg_engine_real_run_input
#include "jpeg_engine.h"
#undef jpeg_run_input
#define p11_run p11_engine_real_run
#include "p11_engine.h"
#undef p11_run
#include "p12_steps.h"
#include "pipeline_proof.h"
#include "pipeline_jpeg_fixtures.h"
struct native_context {volatile uint32_t *top,*lut,*rtos,*h3dl;FILE *log,*gate_log;uint32_t udd,alias_base,pa,origin,last_wait_errno,note_overflow,memory_initialized,last_dma_pa,process_call[5];unsigned lut_mode,stage;};
static struct jpeg_snapshot saved;static struct p11_snapshot lut_saved;static struct p23_proof proof;
struct jpeg_step {uint32_t id,ms,clock[2],mask,pending,valid,jpeg[8],kernel_mask,error,selector,queue[3],rtos[5];};
struct jpeg_capture {uint32_t magic,version,count;struct jpeg_step records[16];uint32_t crc;};
static struct jpeg_capture steps;static struct p12_capture lut_steps;
static uint32_t irq_before[136],top[2048],jpeg_regs[1024],rtos_regs[1024],lut_regs[1024];
static unsigned char kernel_before[EP_KERNEL_STATE_BYTES+12],memory[BITB_BYTES];
static unsigned mode,jpeg_calls,lut_calls;static struct bitb_result p7_result;
static int save_bytes(const char *path,const void *data,size_t n){FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(data,1,n,f)==n;if(fclose(f))ok=0;return ok;}
static int p7_read(const char *path,void *data,size_t n){FILE *f=fopen(path,"rb");if(!f)return 0;int ok=fread(data,1,n,f)==n&&fgetc(f)==EOF;fclose(f);return ok;}
static int camera_stopped(uint32_t pid){(void)pid;return !(mode==11&&jpeg_calls==1&&lut_calls==0);}
static uint32_t millis(void *v){(void)v;return 1;}
static void barrier(void *v){(void)v;}
static void delay(void *v){(void)v;}
static int kernel_read(struct native_context *c,uint32_t va,void *out,size_t n){(void)c;
 if(va==EP_KERNEL_STATE_VA){assert(n==EP_KERNEL_STATE_BYTES);memcpy(out,kernel_before,n);if(mode==12)((unsigned char*)out)[0]^=1;return 1;}
 assert(va==EP_KERNEL_MUTEX_VA&&n==12);memcpy(out,kernel_before+EP_KERNEL_STATE_BYTES,n);return 1;
}
static uint32_t rd(void *v,unsigned b,uint32_t o){(void)v;return (b?jpeg_regs:top)[o/4];}
static void wr(void *v,unsigned b,uint32_t o,uint32_t n){(void)v;(b?jpeg_regs:top)[o/4]=n;}
static uint32_t rd_lut(void *v,unsigned b,uint32_t o){(void)v;return (b?lut_regs:top)[o/4];}
static void wr_lut(void *v,unsigned b,uint32_t o,uint32_t n){(void)v;(b?lut_regs:top)[o/4]=n;}
static int call(void *v,enum jpeg_call f,uintptr_t a,uint32_t b){(void)v;(void)f;(void)a;(void)b;assert(0);return -1;}
static int call_lut(void *v,enum p11_call f,uintptr_t a,uint32_t b,uint32_t d){(void)v;(void)f;(void)a;(void)b;(void)d;assert(0);return -1;}
static int ctl(void *v,uint32_t c,void *a){(void)v;(void)c;(void)a;assert(0);return -1;}
static int state(void *v,unsigned n){(void)v;(void)n;return 1;}
static int idle(void *v){(void)v;return 1;}
static uint32_t kernel_word(void *v,uint32_t p){(void)v;(void)p;return 0;}
static void checkpoint(void *v,unsigned n){(void)v;(void)n;steps.count=1;}
static void checkpoint_lut(void *v,unsigned n){(void)v;(void)n;lut_steps.count=1;}
static void flags(struct bitb_result *r){
 r->lock_acquired=r->lock_released=r->irq_disabled=r->fresh_completion=r->reset_after=r->registers_restored=r->clocks_restored=r->kernel_restored=r->safe_to_release=r->idle_verified=r->inactive_start_verified=r->backup_skipped=r->restore_verified=1;
 r->translations=r->matches=TEST_PAGES;r->initialized_bytes=BITB_BYTES;r->initialized_verified=1;r->engine_jobs=1;r->local_reset_calls=2;r->cold_restart_required=r->mmio_modified=1;r->wait_return=100;
}
static int jpeg_run_input(const struct bitb_request *q,struct bitb_result *r,volatile unsigned char *m,struct jpeg_snapshot *s,struct jpeg_meta *j,const struct jpeg_ops *o,unsigned source,int preserve){
 struct native_context *c=o->ctx;jpeg_calls++;assert(jpeg_calls<=2);assert(source==(jpeg_calls==1?0u:TEST_OUT));assert(preserve==(jpeg_calls==2));
 if(!r->paused_contract){r->error=BJ_CONTEXT;return 1;}
 if(!full_begin(preserve?FULL_FINAL:FULL_REFERENCE,q,r,m)){r->error=BJ_PREPARE;r->safe_to_release=0;r->quarantine_reason=3;return 1;}
 flags(r);r->source_pa=q->pa+source;r->source_copies=preserve?0:1;
 if(mode==1||(mode==4&&jpeg_calls==2)){r->safe_to_release=0;r->quarantine_reason=1;r->error=BJ_WAIT;return 1;}
 const unsigned char *image=jpeg_calls==1||mode==9?fixture_reference:fixture_effect;unsigned len=jpeg_calls==1||mode==9?sizeof fixture_reference:sizeof fixture_effect;
 memcpy((void*)(m+JPEG_OUT),image,len);j->stream_bytes=len;j->stream_bits=len*8;j->old_selector=3;
 memset(&s->task,0,sizeof s->task);uint32_t *p=s->task.packet;p[0]=TEST_W;p[1]=TEST_H;p[2]=p[3]=2;p[4]=TEST_W;s->task.planes[0]=c->alias_base+source;s->task.planes[1]=s->task.planes[0]+TEST_Y;s->task.planes[2]=c->alias_base+JPEG_OUT;memset(s->task.quant,16,128);j->quant_crc=bitb_crc(s->task.quant,128);
 if(mode==10)m[source]^=1;if(mode==7)m[BITB_BYTES-1]^=1;
 int ok=full_finish(preserve?FULL_FINAL:FULL_REFERENCE,q,r,m,len);j->structural=r->target_ok;j->source_ok=r->source_ok;j->guards_ok=r->guards_ok;j->changed_extent=full_pad_bytes;r->pixel_bytes=len;checkpoint(c,999);
 if(!ok){r->error=BJ_COMPARE;return 1;}return 0;
}
static int p11_run(const struct bitb_request *q,struct bitb_result *r,volatile unsigned char *m,struct p11_snapshot *s,const struct p11_ops *o){
 struct native_context *c=o->ctx;lut_calls++;assert(lut_calls==1&&jpeg_calls==1);
 if(!r->paused_contract){r->error=BJ_CONTEXT;return 1;}
 if(!full_begin(FULL_LUT,q,r,m)){r->error=BJ_PREPARE;r->safe_to_release=0;return 1;}
 flags(r);r->lut_hardware_jobs=1;r->table_operations=r->table_completions=r->table_loaded=1;r->pixel_bytes=TEST_FRAME;
 if(mode==2||mode==3){r->error=BJ_WAIT;r->safe_to_release=0;r->quarantine_reason=1;return 1;}
 for(unsigned y=0;y<TEST_H;y++)for(unsigned x=0;x<TEST_W;x++){
  unsigned yy=m[y*TEST_W+x],pair=TEST_Y+(y/2)*TEST_W+(x/2)*2,cb=m[pair],cr=m[pair+1],k=TEST_TABLE+((yy/16*17+cb/16)*17+cr/16)*4;
  m[TEST_OUT+y*TEST_W+x]=mode==5?yy:m[k+2];
 }
 for(unsigned i=0;i<TEST_FRAME-TEST_Y;i++)m[TEST_OUT+TEST_Y+i]=m[TEST_Y+i];if(mode==6)m[BITB_BYTES-1]^=1;
 int ok=full_finish(FULL_LUT,q,r,m,0);p11_dma(s->rdma,c->alias_base,0);p11_dma(s->wdma,c->alias_base+P11_OUT,1);uint32_t start[9]={2,1,~0u,~0u,0,~0u,~0u,~0u,0};memcpy(s->start,start,sizeof start);memset(s->lut_before,0,40);c->process_call[0]=c->process_call[1]=1;checkpoint_lut(c,999);
 if(!ok){r->error=BJ_COMPARE;return 1;}
 if(mode==8)m[TEST_OUT]^=1;return 0;
}
#include "pipeline_worker.inc"
#include "pipeline_files.inc"
int main(int argc,char **argv){
 assert(argc==2||argc==3);const char *root=argv[1];assert(!mkdir(root,0700));char dir[1200],path[1200];
 for(mode=0;mode<(argc==3?1u:13u);mode++){
  free(full_pad);full_pad=NULL;full_pad_bytes=full_initialized=full_stage=full_prepare_unsafe=0;memset(&saved,0,sizeof saved);memset(&lut_saved,0,sizeof lut_saved);memset(memory,0,sizeof memory);jpeg_calls=lut_calls=0;
  struct native_context c={.top=top,.lut=jpeg_regs,.rtos=rtos_regs,.h3dl=lut_regs,.alias_base=0x90000000,.pa=0xa1c00000};
  snprintf(dir,sizeof dir,"%s/model_%02u",root,mode);assert(!mkdir(dir,0700));
  snprintf(path,sizeof path,"%s/log.txt",dir);c.log=fopen(path,"w");snprintf(path,sizeof path,"%s/p21a_preflight.txt",dir);c.gate_log=fopen(path,"w");assert(c.log&&c.gate_log);
  for(unsigned i=0;i<3;i++){snprintf(path,sizeof path,"%s/%s",dir,p23_dirs[i]);assert(!mkdir(path,0700));}
  struct bitb_request q={BITB_REQUEST_MAGIC,10,c.pa,BITB_BYTES,12345,99,0x91000000,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));struct bitb_result all;p23_stage_init(&all,&q,&c);
  int rc=p23_run_stages(&c,&q,&all,memory,dir);proof.crc=bitb_crc(&proof,offsetof(struct p23_proof,crc));all.route_crc=proof.crc;
  snprintf(path,sizeof path,"%s/p23_pipeline.bin",dir);assert(save_bytes(path,&proof,sizeof proof));
  snprintf(path,sizeof path,"%s/p11_kernel_before.bin",dir);assert(save_bytes(path,kernel_before,sizeof kernel_before));snprintf(path,sizeof path,"%s/p11_rtos_irq_before.bin",dir);assert(save_bytes(path,irq_before,sizeof irq_before));p7_result=all;
  int verified=p7_verify_files(dir,&q);assert((rc==0)==(mode==0));assert(verified==(mode==0));
  if(mode==8){assert(proof.failed==3&&jpeg_calls==2&&lut_calls==1&&!all.safe_to_release);}
  if(mode==1||mode==2||mode==3||mode==4||mode==12)assert(!all.safe_to_release);
  if(mode==5||mode==6)assert(proof.failed==2&&jpeg_calls==1&&all.safe_to_release);
  if(mode==9)assert(proof.failed==3&&rc&&all.safe_to_release);
  assert(jpeg_calls<=2&&lut_calls<=1);fclose(c.log);fclose(c.gate_log);
 }
 puts("PASS actual pipeline orchestration/controller-file models; hardware engines mocked");return 0;
}
