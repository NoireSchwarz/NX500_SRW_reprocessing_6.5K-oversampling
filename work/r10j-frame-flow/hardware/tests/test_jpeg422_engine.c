#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/bitb_job.h"
#include "../src/jpeg_engine.h"
struct model {uint32_t top[2048],jpeg[1024];unsigned char *memory,*image;unsigned bytes,mode,encodes,resets,clocks,notes;uint32_t pa,alias;};
static uint32_t rd(void *v,unsigned b,uint32_t o){struct model *m=v;return (b?m->jpeg:m->top)[o/4];}
static void wr(void *v,unsigned b,uint32_t o,uint32_t n){struct model *m=v;(b?m->jpeg:m->top)[o/4]=n;}
static int call(void *v,enum jpeg_call kind,uintptr_t a,uint32_t b){struct model *m=v;(void)b;
 switch(kind){
 case J_VTOP:return m->pa+(uint32_t)a-m->alias;
 case J_RESET:m->resets++;m->jpeg[0]=m->jpeg[0x800/4]=0;return 0;
 case J_CLOCK:m->clocks++;return 0;
 case J_MUX:m->top[0x110c/4]=(uint32_t)a<<20;return 0;
 case J_COUNT:return m->bytes;
 case J_ENCODE:{m->encodes++;struct jpeg_task *t=(void*)a;assert(t->packet[2]==1&&t->packet[3]==2&&t->planes[0]==m->alias&&t->planes[1]==m->alias+TEST_Y&&t->planes[2]==m->alias+JPEG_OUT);
  memcpy(m->memory+JPEG_OUT,m->image,m->bytes);m->memory[JPEG_OUT+((m->bytes+511)&~511u)+8]^=1;
  if(m->mode==2)m->memory[JPEG_OUT+JPEG_CAP]^=1;if(m->mode==3)m->memory[0]^=1;
  m->jpeg[0x810/4]=m->pa;m->jpeg[0x814/4]=m->pa+TEST_Y;m->jpeg[0x818/4]=m->pa+JPEG_OUT;m->jpeg[8/4]=m->jpeg[0x808/4]=(TEST_W<<16)|TEST_H;m->jpeg[0x80c/4]=TEST_W;m->jpeg[0x804/4]=0x10021;m->jpeg[4/4]=0x32;m->jpeg[0x280/4]=m->bytes*8;return 0;}
 }assert(0);return -1;
}
static int ctl(void *v,uint32_t c,void *a){(void)v;(void)a;return c==0xc00c6808?4940:0;}
static int yes(void *v,unsigned n){(void)v;(void)n;return 1;}
static int idle(void *v){(void)v;return 1;}
static void barrier(void *v){(void)v;}
static void note(void *v,unsigned n){(void)n;((struct model*)v)->notes++;}
static uint32_t kernel(void *v,uint32_t a){(void)v;return a==0xc07555c4?3:0;}
static unsigned char *image(const char *p,unsigned *bytes){FILE *f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);*bytes=ftell(f);rewind(f);unsigned char *b=malloc(*bytes);assert(b&&fread(b,1,*bytes,f)==*bytes);fclose(f);return b;}
int main(int argc,char **argv){assert(argc==4&&full_open_inputs(argv[1]));unsigned n422,n420;unsigned char *j422=image(argv[2],&n422),*j420=image(argv[3],&n420),*memory=malloc(TEST_BYTES);assert(memory);
 for(unsigned mode=0;mode<4;mode++){
  full_initialized=full_prepare_unsafe=0;free(full_pad);full_pad=NULL;full_pad_bytes=0;
  struct model m={.memory=memory,.image=mode==1?j420:j422,.bytes=mode==1?n420:n422,.mode=mode,.pa=0x94000000,.alias=0x64000000};
  struct bitb_request q={BITB_REQUEST_MAGIC,10,m.pa,TEST_BYTES,123456,123,m.alias,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));struct bitb_result r={.ready=1,.paused_contract=1,.alias_va=m.alias};struct jpeg_meta j={0};struct jpeg_snapshot s;
  struct jpeg_ops ops={&m,rd,wr,call,ctl,yes,idle,barrier,note,kernel};int rc=jpeg_run_input(&q,&r,memory,&s,&j,&ops,0,0);
  assert(m.encodes==1&&m.resets==2&&m.clocks==1&&r.engine_jobs==1&&!r.lut_hardware_jobs&&!r.table_operations&&!r.table_completions);
  assert(r.lock_released&&r.registers_restored&&r.clocks_restored&&r.fresh_completion&&r.kernel_restored);
  if(mode==0)assert(!rc&&r.safe_to_release&&j.format==422&&j.structural&&r.guards_ok&&r.source_ok);
  if(mode==1)assert(rc&&r.error==BJ_COMPARE&&r.safe_to_release&&j.format==420&&j.structural&&r.guards_ok&&r.source_ok);
  if(mode>=2)assert(rc&&r.error==BJ_COMPARE&&!r.safe_to_release&&full_prepare_unsafe);
 }
 free(memory);free(j422);free(j420);puts("PASS 4 actualJPEGengine models: candidate1 oneJPEG/noLUT,422complete,420mismatchsafe,guard/sourcecorruptionquarantined; DMA/IRQ modeled");return 0;}
