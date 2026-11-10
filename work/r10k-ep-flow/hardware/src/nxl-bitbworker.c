/* SPDX-License-Identifier: AGPL-3.0-only */
#define _GNU_SOURCE
#define NXL_ORIGINAL_GLIBC_ABI 1
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "bitb_job.h"
#include "alloc_core.h"
#include "alloc_guards.h"
#include "ep_kernel_guards.h"
#include "jpeg_engine.h"
#include "p11_engine.h"
#include "ldc_engine.h"
#include "../../common/pixel_sample.h"
#include "p12_steps.h"
#include "pipeline_proof.h"
#include "p11_guards.h"
_Static_assert(sizeof p11_native_guards/sizeof *p11_native_guards==NXL_EP_NATIVE_CODE_GUARDS,"native guard count");

#include "p13_irq_guards.h"
#include "rtos_ro_guard.h"
struct native_context {int ep,mem,sma;uint32_t udd;volatile uint32_t *top,*lut,*rtos;volatile const uint32_t *rtos_code;FILE *log,*gate_log;
 unsigned gate_state_calls,gate_idle_calls,gate_rejections;const char *gate_last_type,*gate_last_reason;
 uint32_t process_call[5];
 uint32_t alias_base,pa,origin,memory_initialized,note_overflow,last_dma_kind,last_dma_va,last_dma_pa,last_ioctl,last_errno,last_wait_errno;int last_return;volatile unsigned char *memory;volatile uint32_t *h3dl,*ldc;unsigned lut_mode,stage,ldc_bytes;};
static struct jpeg_snapshot saved;
static struct p11_snapshot lut_saved;
static struct ldc_snapshot ldc_saved;
static struct ldc_config_audit ldc_audit;
static struct p12_capture lut_steps;
static struct j422_proof proof;
static uint32_t irq_before[136],irq_after[136];
static unsigned char kernel_before[EP_KERNEL_STATE_BYTES+12],kernel_after[EP_KERNEL_STATE_BYTES+12];
static unsigned owner_captured;
struct jpeg_step {uint32_t id,ms,clock[2],mask,pending,valid,jpeg[8],kernel_mask,error,selector,queue[3],rtos[5];};
struct jpeg_capture {uint32_t magic,version,count;struct jpeg_step records[16];uint32_t crc;};
static struct jpeg_capture steps;
static int save_bytes(const char *path,const void *p,size_t n){FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(p,1,n,f)==n;if(fclose(f))ok=0;return ok;}
static int save_file(const char *from,const char *to){FILE *in=fopen(from,"r"),*out=NULL;if(!in)return 0;out=fopen(to,"w");if(!out){fclose(in);return 0;}char bytes[4096];size_t n;int ok=1;while((n=fread(bytes,1,sizeof bytes,in)))if(fwrite(bytes,1,n,out)!=n){ok=0;break;}if(ferror(in))ok=0;fclose(in);if(fclose(out))ok=0;return ok;}
static int fingerprint(const char *path,size_t expected,uint32_t hash){FILE *f=fopen(path,"rb");if(!f)return 0;unsigned char b[4096];size_t n,total=0;uint32_t h=2166136261u;while((n=fread(b,1,sizeof b,f))){total+=n;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619u;}int ok=!ferror(f)&&total==expected&&h==hash;fclose(f);return ok;}
static uint32_t library_base(void){FILE *f=fopen("/proc/self/maps","r");if(!f)return 0;char line[512],perms[8];unsigned low,high,off,found=0;
 while(fgets(line,sizeof line,f))if(sscanf(line,"%x-%x %7s %x",&low,&high,perms,&off)==4&&!off&&!strcmp(perms,"r-xp")&&strstr(line," /usr/lib/libudd5.so")){if(found){found=0;break;}found=low;}fclose(f);return found;}
static int kernel_read(struct native_context *c,uint32_t va,void *p,size_t n){if(va<0xc0008000u||(uint64_t)va+n>0xc0800000u)return 0;return pread(c->mem,p,n,(off_t)(va-EP_KERNEL_DELTA))==(ssize_t)n;}
static int fd_target(int fd,const char *expected){char name[80],target[200];snprintf(name,sizeof name,"/proc/self/fd/%d",fd);ssize_t n=readlink(name,target,sizeof target-1);if(n<0)return 0;target[n]=0;return !strcmp(target,expected);}
static int camera_stopped(uint32_t camera){char path[160],line[512];snprintf(path,sizeof path,"/proc/%u/comm",camera);FILE *f=fopen(path,"r");if(!f)return 0;int ok=fgets(line,sizeof line,f)&&!strcmp(line,"di-camera-app\n");fclose(f);if(!ok)return 0;
 snprintf(path,sizeof path,"/proc/%u/task",camera);DIR *d=opendir(path);if(!d)return 0;struct dirent *e;unsigned count=0;ok=1;
 while((e=readdir(d))){unsigned tid=(unsigned)strtoul(e->d_name,0,10);if(!tid)continue;snprintf(path,sizeof path,"/proc/%u/task/%u/stat",camera,tid);f=fopen(path,"r");if(!f){ok=0;break;}int got=fgets(line,sizeof line,f)!=NULL;fclose(f);char *p=got?strrchr(line,')'):NULL;if(!p||p[1]!=' '||(p[2]!='T'&&p[2]!='t')){ok=0;break;}count++;}
 closedir(d);return ok&&count>=6;
}

static void barrier(void *v){(void)v;__asm__ volatile("dsb sy\n\tdmb sy":::"memory");}
static uint32_t millis(void *v){(void)v;struct timespec t;if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return 0;return (uint32_t)t.tv_sec*1000+(uint32_t)t.tv_nsec/1000000;}
static void delay(void *v){(void)v;struct timespec t={0,1000000};nanosleep(&t,0);}
static uint32_t rd(void *v,unsigned b,uint32_t off){struct native_context *c=v;return (b?c->lut:c->top)[off/4];}
static void wr(void *v,unsigned b,uint32_t off,uint32_t val){struct native_context *c=v;(b?c->lut:c->top)[off/4]=val;}
static int ctl(void *v,uint32_t cmd,void *arg){struct native_context *c=v;extern int ioctl(int,unsigned long,...);errno=0;int n=ioctl(c->ep,cmd,arg);c->last_ioctl=cmd;c->last_return=n;c->last_errno=n<0?errno:0;
 if(cmd==0xc00c6808||cmd==0x40086806)c->last_wait_errno=c->last_errno;
 fprintf(c->log,"IOCTL command=0x%x return=%d errno=%u\n",cmd,n,c->last_errno);return n;}

static int call(void *v,enum jpeg_call fn,uintptr_t a,uint32_t b){
 struct native_context *c=v;uint32_t p=c->udd;int n=0;
 switch(fn){
 case J_RESET:n=((int(*)(uint32_t))(uintptr_t)(p+0x2bd28))(a);break;
 case J_CLOCK:((void(*)(uint32_t,uint32_t))(uintptr_t)(p+0x2b7dc))(a,b);break;
 case J_MUX:((void(*)(uint32_t,uint32_t))(uintptr_t)(p+0x2d4b8))(a,b);break;
 case J_ENCODE:n=((int(*)(const void*))(uintptr_t)(p+0x45218))((void*)a);break;
 case J_COUNT:n=((uint32_t(*)(void))(uintptr_t)(p+0x45460))();break;
 case J_VTOP:return ((uint32_t(*)(uint32_t))(uintptr_t)(p+0x1bc30))(a);
 }
 fprintf(c->log,"NATIVE fn=%u arg=0x%x b=%u return=%d\n",fn,(unsigned)a,b,n);return n;
}
static uint32_t kernel_word(void *v,uint32_t va){uint32_t n=~0u;kernel_read(v,va,&n,4);return n;}
static void checkpoint(void *v,unsigned number){
 struct native_context *c=v;if(steps.count>=16){c->note_overflow=1;return;}
 struct jpeg_step *p=steps.records+steps.count++;memset(p,0,sizeof *p);p->id=number;p->ms=millis(c)-c->origin;
 p->clock[0]=c->top[0x1020/4];p->clock[1]=c->top[0x1024/4];p->mask=c->top[0x1080/4];p->pending=c->top[0x10d8/4];
 p->valid=!(p->clock[0]&0x80)&&!(p->clock[1]&0x1e0000);
 const unsigned offsets[8]={0,4,8,0x280,0x800,0x810,0x814,0x818};
 if(p->valid)for(unsigned i=0;i<8;i++)p->jpeg[i]=c->lut[offsets[i]/4];
 p->kernel_mask=kernel_word(c,0xc0755238);p->error=kernel_word(c,0xc07554c4);p->selector=kernel_word(c,0xc07555c4);
 kernel_read(c,0xc0755504,p->queue,12);
 for(unsigned i=0;i<5;i++)p->rtos[i]=c->rtos[0xaac/4+i];
 fprintf(c->log,"JPEG_STEP id=%u ms=%u valid=%u clock=%x,%x top_mask=%x raw=%x kernel_mask=%x error=%u selector=%u queue5=%u stream_bits=%u\n",p->id,p->ms,p->valid,p->clock[0],p->clock[1],p->mask,p->pending,p->kernel_mask,p->error,p->selector,p->queue[0],p->jpeg[3]);
}
#include "jpeg_gate_diag.inc"
static int call_lut(void *v,enum p11_call fn,uintptr_t a,uint32_t b,uint32_t d){struct native_context *c=v;uint32_t p=c->udd;int n=0;
 if(fn==P11_LOAD||fn==P11_SAVE){c->last_dma_kind=fn;c->last_dma_va=(uint32_t)a;c->last_dma_pa=((uint32_t)a>=c->alias_base&&(uint64_t)(uint32_t)a+P11_SLOT<=(uint64_t)c->alias_base+BITB_BYTES)?c->pa+(uint32_t)a-c->alias_base:0;}
 switch(fn){
 case P13_RESET:return ((int(*)(uint32_t))(uintptr_t)(p+0x2bd28))(a);
 case P11_DMA_RESET:n=((int(*)(uint32_t))(uintptr_t)(p+0x2c00c))(a);fprintf(c->log,"LUT_DMA_REARM channel=%u return=%d before_submit=1\n",(unsigned)a,n);return n;
 case P11_SELECT:((void(*)(uint32_t))(uintptr_t)(p+0x2f224))(a);break;
 case P11_CLOCK:((void(*)(uint32_t,uint32_t))(uintptr_t)(p+0x2b7dc))(a,b);break;
 case P11_UPDATE:((void(*)(uint32_t))(uintptr_t)(p+0x2e6a8))(a);break;
 case P11_LOAD:n=((int(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)(p+0x13d10))(a,b,d);break;
 case P11_SAVE:n=((int(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)(p+0x13e14))(a,b,d);break;
 case P11_PROCESS:c->process_call[0]++;memcpy(c->process_call+1,(void*)a,16);n=((int(*)(const void*))(uintptr_t)(p+0x13c2c))((void*)a);break;
 case P11_RDMA:n=((int(*)(uint32_t,const void*,uint32_t))(uintptr_t)(p+0x2c088))(b,(void*)a,d);break;
 case P11_WDMA:n=((int(*)(uint32_t,const void*,uint32_t))(uintptr_t)(p+0x2c6e8))(b,(void*)a,d);break;
 case P11_MUXR:((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)(p+0x2d048))(a,b,d);break;
 case P11_MUXW:((void(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)(p+0x2d1d4))(a,b,d);break;
 case P11_START:((void(*)(const void*))(uintptr_t)(p+0x2e6f0))((void*)a);break;
 case P11_VTOP:return ((uint32_t(*)(uint32_t))(uintptr_t)(p+0x1bc30))(a);
 }
 fprintf(c->log,"NATIVE fn=%u arg=0x%x b=%u c=%u return=%d\n",fn,(unsigned)a,b,d,n);return n;
}
static void checkpoint_lut(void *v,uint32_t number){struct native_context *c=v;if(lut_steps.count>=P12_STEP_MAX){c->note_overflow=1;return;}
 if(number==102)c->memory_initialized=1;
 struct p12_step *p=lut_steps.record+lut_steps.count++;memset(p,0,sizeof *p);p->step=number;p->elapsed_ms=millis(c)-c->origin;
 for(unsigned i=0;i<P12_TOP_COUNT;i++)p->top[i]=c->top[p12_top_offsets[i]/4];
 p->lut_valid=!((c->top[0x1020/4]|c->top[0x1024/4])&16);if(p->lut_valid)for(unsigned i=0;i<5;i++)p->lut[i]=c->h3dl[i];
 unsigned char raw[EP_KERNEL_STATE_BYTES];p->kernel_valid=kernel_read(c,EP_KERNEL_STATE_VA,raw,sizeof raw)&&kernel_read(c,EP_KERNEL_MUTEX_VA,p->mutex,12);
 if(p->kernel_valid){memcpy(p->linux_mask,raw+0x64,20);memcpy(p->acc8_queue,raw+(0xc0755528u-EP_KERNEL_STATE_VA),12);memcpy(p->core5_queue,raw+(0xc0755604u-EP_KERNEL_STATE_VA),12);memcpy(p->acc7_queue,raw+(0xc075551cu-EP_KERNEL_STATE_VA),12);}
 for(unsigned i=0;i<5;i++)p->rtos_mask[i]=c->rtos[0xaac/4+i];
 p->last_dma_kind=c->last_dma_kind;p->last_dma_va=c->last_dma_va;p->last_dma_pa=c->last_dma_pa;p->last_ioctl=c->last_ioctl;p->ioctl_return=c->last_return;p->ioctl_errno=c->last_errno;
 if(c->memory_initialized&&c->last_dma_pa){volatile const uint32_t *m=(volatile const uint32_t*)(c->memory+(c->last_dma_va-c->alias_base));p->memory_sample[0]=m[0];p->memory_sample[1]=m[P11_NODES/4-1];}
 if(c->memory_initialized&&c->memory){volatile const uint32_t *m=(volatile const uint32_t*)(c->memory+P11_OUT);p->memory_sample[2]=m[0];p->memory_sample[3]=m[P11_FRAME/4-1];}
 fprintf(c->log,"STEP id=%u ms=%u lut_valid=%u on=0x%x bank=0x%x access=0x%x load=0x%x save=0x%x expect=0x%x raw_acc=0x%x acc8_done=%u core5_done=%u path1_status=0x%x acc7_done=%u\n",p->step,p->elapsed_ms,p->lut_valid,p->lut[0],p->lut[1],p->lut[2],p->lut[3],p->lut[4],p->last_dma_pa,p->top[13],p->acc8_queue[0],p->core5_queue[0],c->top[0x1200/4],p->acc7_queue[0]);
 fprintf(c->log,"DMA_STATE trigger=0x%x global_fsm=0x%x read_fsm=0x%x write_fsm=0x%x read_ctrl2=0x%x write_ctrl2=0x%x\n",p->top[25],p->top[26],p->top[36],p->top[44],p->top[30],p->top[39]);
}

static uint32_t rd_lut(void *v,unsigned b,uint32_t off){struct native_context *c=v;return (b?c->h3dl:c->top)[off/4];}
static void wr_lut(void *v,unsigned b,uint32_t off,uint32_t val){struct native_context *c=v;(b?c->h3dl:c->top)[off/4]=val;}


static uint32_t rd_ldc(void*v,unsigned b,uint32_t off){struct native_context*c=v;return (b?c->ldc:c->top)[off/4];}
static void wr_ldc(void*v,unsigned b,uint32_t off,uint32_t val){struct native_context*c=v;(b?c->ldc:c->top)[off/4]=val;}
static int call_ldc(void*v,enum ldc_call fn,uintptr_t a,uint32_t b,uint32_t d){
 struct native_context*c=v;uint32_t p=c->udd;int n=0;
 switch(fn){
 case L_CLOCK:((void(*)(uint32_t,uint32_t))(uintptr_t)(p+0x2b7dc))(a,b);break;
 case L_RESET:return ((int(*)(uint32_t))(uintptr_t)(p+0x2bd28))(a);
 case L_DMA_RESET:n=((int(*)(uint32_t))(uintptr_t)(p+0x2c00c))(a);fprintf(c->log,"LDC_DMA_REARM channel=%u return=%d before_submit=1\n",(unsigned)a,n);return n;
 case L_SELECT:((void(*)(uint32_t))(uintptr_t)(p+0x2f224))(a);break;
 case L_UPDATE:((void(*)(uint32_t))(uintptr_t)(p+0x2e6a8))(a);break;
 case L_UPDATE_MODE:((void(*)(uint32_t,uint32_t))(uintptr_t)(p+0x2e6c8))(a,b);break;
 case L_CONFIG:n=((int(*)(const void*,uint32_t))(uintptr_t)(p+0x2b28c))((void*)a,b);break;
 case L_RDMA:n=((int(*)(uint32_t,const void*,uint32_t))(uintptr_t)(p+0x2c088))(b,(void*)a,d);break;
 case L_WDMA:n=((int(*)(uint32_t,const void*,uint32_t))(uintptr_t)(p+0x2c6e8))(b,(void*)a,d);break;
 case L_MUXR:n=((int(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)(p+0x2d048))(a,b,d);break;
 case L_MUXW:n=((int(*)(uint32_t,uint32_t,uint32_t))(uintptr_t)(p+0x2d1d4))(a,b,d);break;
 case L_START:
  n=((int(*)(const void*))(uintptr_t)(p+0x2e6f0))((void*)a);
  fprintf(c->log,"LDC_START_OBSERVATION mode=NATIVE_MEMORY_SINGLE native_return=%d expected_TOP1008_pulses=0x100001,0x10000 module_trigger=0x%x rdma0_trigger=0x%x wdma0_trigger=0x%x update_mode=0x%x trigger_readback_required=0 completion_authority=FRESH_CORE5\n",n,c->top[0x1114/4],c->top[0x132c/4],c->top[0x172c/4],c->top[0x1048/4]);break;
 case L_VTOP:return ((uint32_t(*)(uint32_t))(uintptr_t)(p+0x1bc30))(a);
 case L_STOP:
  /* Decompose original stop_auto for actual channel0 only. Its native
   * WDMA helper accepts-1 and writes a neighbouring channel register;
   * never pass disabled slots through that original off-by-one guard. */
  ((void(*)(uint32_t,uint32_t))(uintptr_t)(p+0x3cffc))(0,0);
  c->top[0x1114/4]=0;
  ((void(*)(uint32_t))(uintptr_t)(p+0x3dc80))(0);
  ((void(*)(uint32_t,uint32_t))(uintptr_t)(p+0x3ce38))(0,0);break;
 }
 fprintf(c->log,"LDC_NATIVE fn=%u arg=0x%x b=%u c=%u return=%d\n",fn,(unsigned)a,b,d,n);return n;
}

#include "ldc_progress.inc"

#include "pipeline_worker.inc"

int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_PIPEWORKER=0.4.0-demo51-422; NATIVE_LUT_JPEG_NV16; SHARED192_DIRECT_HANDOFF_R9F_DMA_REARM");return 0;}
 if(argc==2&&!strcmp(argv[1],"--abi")){printf("BITB_REQUEST_BYTES=%u; BITB_RESULT_BYTES=%u; BITB_RESULT_CRC_OFFSET=%u; STEP_BYTES=%u; CAPTURE_BYTES=%u\n",(unsigned)sizeof(struct bitb_request),(unsigned)sizeof(struct bitb_result),(unsigned)offsetof(struct bitb_result,crc),(unsigned)sizeof(struct jpeg_step),(unsigned)sizeof steps);printf("PIPELINE_PROOF_BYTES=%u; FULL_MANIFEST_BYTES=%u; PHYSICAL_PAGES=%u\n",(unsigned)sizeof proof,(unsigned)sizeof(struct full_manifest),TEST_PAGES);return 0;}
 if(argc!=6)return 2;
 struct bitb_request q;struct bitb_result r={0};r.magic=BITB_RESULT_MAGIC;r.version=BITB_WIRE_VERSION;steps.magic=0x31534a4e;steps.version=1;r.bytes=sizeof r;r.safe_to_release=1;
 struct native_context c={.ep=-1,.mem=-1,.sma=-1};void *memory=MAP_FAILED;FILE *timing_file=NULL;int go=atoi(argv[4]),events=atoi(argv[5]),rc=1,masked=0;sigset_t blocked,oldmask;char path[1200];
 const char*prepare_stage="REQUEST";unsigned native_guards_verified=0,kernel_guards_verified=0;
 FILE *f=fopen(argv[1],"rb");if(!f)goto done;int qok=fread(&q,1,sizeof q,f)==sizeof q&&fgetc(f)==EOF;fclose(f);if(!qok||!bitb_request_valid(&q))goto done;
 snprintf(path,sizeof path,"%s/r6-worker-timing.log",argv[3]);timing_file=fopen(path,"w");r6_timing_set(timing_file);r6_timing("WORKER","BEGIN",0,1);
 snprintf(path,sizeof path,"%s/p11_native.txt",argv[3]);c.log=fopen(path,"wx");if(!c.log)goto done;setvbuf(c.log,NULL,_IOFBF,32768);
 prepare_stage="INPUT_FILES";
 if(!full_open_inputs(argv[3]))goto done;
 r.request_crc=q.crc;r.request_bytes=q.bytes;r.pa=q.pa;c.pa=q.pa;
 prepare_stage="LIBRARY_FINGERPRINT";
 if(!fingerprint("/usr/lib/libudd5.so",ALLOC_UDD_BYTES,ALLOC_UDD_FNV))goto done;c.udd=library_base();r.udd_base=c.udd;if(!c.udd)goto done;
 prepare_stage="LIBRARY_CODE_GUARDS";
 for(unsigned i=0;i<sizeof p11_native_guards/sizeof *p11_native_guards;i++){if(memcmp((void*)(uintptr_t)(c.udd+p11_native_guards[i].address),p11_native_guards[i].words,32))goto done;native_guards_verified++;}
 prepare_stage="READ_ONLY_MEMORY_DEVICE";
 c.mem=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(c.mem<0)goto done;
 prepare_stage="KERNEL_CODE_GUARDS";
 for(unsigned i=0;i<sizeof ep_kernel_guards/sizeof *ep_kernel_guards;i++){uint32_t w[8];if(!kernel_read(&c,ep_kernel_guards[i].va,w,32)||memcmp(w,ep_kernel_guards[i].words,32))goto done;r.code_guards++;kernel_guards_verified++;}
 prepare_stage="RTOS_STATE_PAGE";
 c.rtos=mmap(NULL,4096,PROT_READ,MAP_SHARED,c.mem,(off_t)0x817d8000);if((void*)c.rtos==MAP_FAILED)goto done;
 prepare_stage="RTOS_IRQ_CODE_PAGE";
 c.rtos_code=mmap(NULL,4096,PROT_READ,MAP_SHARED,c.mem,(off_t)0x804bb000);if((void*)c.rtos_code==MAP_FAILED)goto done;
 prepare_stage="RTOS_IRQ_CODE_GUARDS";
 struct p13_irq_guard actual_guards[2];
 for(unsigned i=0;i<2;i++){actual_guards[i].address=p13_irq_guards[i].address;for(unsigned j=0;j<8;j++)actual_guards[i].words[j]=c.rtos_code[(p13_irq_guards[i].address-0x804bb000)/4+j];if(memcmp(actual_guards[i].words,p13_irq_guards[i].words,32))goto done;r.rtos_guards++;}
 prepare_stage="RTOS_IRQ_STATE_LITERAL";
 uint32_t irq_state_literal=c.rtos_code[0x948/4];fprintf(c.log,"WORKER_PREPARE_GUARD name=IRQ_STATE_LITERAL expected=817d8aac actual=%08x\n",irq_state_literal);
 if(irq_state_literal!=0x817d8aac)goto done;
 prepare_stage="RTOS_DMA_STOP_CODE_GUARD";
 static const uint32_t drain_guard[8]={0xe3500000u,0xe52d4004u,0x0a00001bu,0xe3510000u,0xe3a04000u,0xe3424082u,0x03a01001u,0xe5840120u};struct rtos_ro_guard_audit drain_audit;
 fprintf(c.log,"WORKER_PREPARE_BEGIN stage=%s\n",prepare_stage);fflush(c.log);
 int drain_guard_ok=rtos_ro_code_guard(c.mem,0x804b6810u,drain_guard,&drain_audit);rtos_ro_guard_log(c.log,"DMA_STOP",drain_guard,&drain_audit);fflush(c.log);
 if(!drain_guard_ok)goto done;r.rtos_guards++;

 snprintf(path,sizeof path,"%s/p13_rtos_irq_guards.bin",argv[3]);if(!save_bytes(path,actual_guards,sizeof actual_guards))goto done;
 prepare_stage="EP_DEVICE";
 c.ep=open("/dev/drime5_ep",O_RDWR|O_SYNC|O_CLOEXEC);if(c.ep<0||!fd_target(c.ep,"/dev/drime5_ep"))goto done;r.ep_fd=c.ep;
 snprintf(path,sizeof path,"%s/p21a_preflight.txt",argv[3]);c.gate_log=fopen(path,"w");if(!c.gate_log)goto done;setvbuf(c.gate_log,NULL,_IOFBF,65536);
 fputs("P21A_PREFLIGHT_V1; p21b initial completion settling; full16sample window; read-only diagnostics\n",c.gate_log);
 prepare_stage="EP_REGISTER_INFO";
 uint32_t info[20];if(ctl(&c,0x80506864,info)||info[0]!=0x20820000||info[1]!=0x1c00||info[14]!=0x2082a000||info[15]!=0x1000||info[16]!=0x2082b000||info[17]!=0x1000)goto done;
 prepare_stage="EP_REGISTER_MAPPINGS";
 c.top=mmap(NULL,0x2000,PROT_READ|PROT_WRITE,MAP_SHARED,c.ep,(off_t)info[0]);if((void*)c.top==MAP_FAILED)goto done;
 c.lut=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,c.ep,(off_t)info[14]);if((void*)c.lut==MAP_FAILED)goto done;
 c.h3dl=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,c.ep,(off_t)info[16]);if((void*)c.h3dl==MAP_FAILED)goto done;
 uint32_t ldc_pa=0,ldc_bytes=0;for(unsigned i=0;i<20;i+=2)if(info[i]==0x20823000){if(ldc_pa)goto done;ldc_pa=info[i];ldc_bytes=info[i+1];}
 if(!ldc_pa||ldc_bytes<0x400||ldc_bytes>0x1000)goto done;c.ldc_bytes=ldc_bytes;
 c.ldc=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,c.ep,(off_t)ldc_pa);if((void*)c.ldc==MAP_FAILED)goto done;
 prepare_stage="PRIVATE_EP_BINDING";
 if(*(uint32_t*)(uintptr_t)(c.udd+EP_OPEN_COUNT_DATA)||*(int32_t*)(uintptr_t)(c.udd+EP_DEVICE_FD_DATA)!=-1||*(uint32_t*)(uintptr_t)(c.udd+EP_REGISTER_POINTERS_DATA))goto done;
 *(uint32_t*)(uintptr_t)(c.udd+EP_DEVICE_FD_DATA)=c.ep;*(uint32_t*)(uintptr_t)(c.udd+EP_REGISTER_POINTERS_DATA)=(uint32_t)(uintptr_t)c.top;*(uint32_t*)(uintptr_t)(c.udd+EP_REGISTER_POINTERS_DATA+8)=(uint32_t)(uintptr_t)c.ldc;*(uint32_t*)(uintptr_t)(c.udd+EP_REGISTER_POINTERS_DATA+16)=(uint32_t)(uintptr_t)c.h3dl;*(uint32_t*)(uintptr_t)(c.udd+EP_REGISTER_POINTERS_DATA+24)=(uint32_t)(uintptr_t)c.lut;
 prepare_stage="SMA_DEVICE";
 if(((int(*)(void))(uintptr_t)(c.udd+0x1ba24))())goto done;c.sma=*(int32_t*)(uintptr_t)(c.udd+EP_SMA_FD_DATA);if(c.sma<0||!fd_target(c.sma,"/dev/d5_sma"))goto done;r.sma_fd=c.sma;
 int flags=fcntl(c.sma,F_GETFL);if(flags<0||((unsigned)flags&0x101000)!=0x101000)goto done;r.sma_open_flags=flags;
 prepare_stage="SHARED_WORKSPACE_MAPPING";
 memory=mmap(NULL,BITB_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,c.sma,(off_t)(uint64_t)q.pa);if(memory==MAP_FAILED)goto done;r.alias_va=(uint32_t)(uintptr_t)memory;c.alias_base=r.alias_va;c.memory=memory;
 prepare_stage="STAGE_DIRECTORIES";
 snprintf(path,sizeof path,"%s/p11_worker_maps.txt",argv[3]);if(!save_file("/proc/self/maps",path))goto done;
 snprintf(path,sizeof path,"%s/00_ldc",argv[3]);if(mkdir(path,0700))goto done;
 snprintf(path,sizeof path,"%s/00_lut",argv[3]);if(mkdir(path,0700))goto done;
 snprintf(path,sizeof path,"%s/01_jpeg",argv[3]);if(mkdir(path,0700))goto done;
 prepare_stage="READY";r.ready=1;r.phase=2;r6_timing("WORKER_READY","EVENT",0,1);if(write(events,"R",1)!=1)goto done;
 {struct pollfd p={go,POLLIN|POLLHUP,0};char cmd=0;if(poll(&p,1,60000)<=0||read(go,&cmd,1)!=1||cmd!='G'){r.error=BJ_CANCEL;goto done;}}
 r6_timing("WORKER_GO_RECEIVED","EVENT",0,1);r.paused_contract=camera_stopped(q.camera_pid);if(!r.paused_contract){r.error=BJ_CONTEXT;goto done;}
 prepare_stage="ADMITTED";
 sigemptyset(&blocked);sigaddset(&blocked,SIGTERM);sigaddset(&blocked,SIGINT);sigaddset(&blocked,SIGHUP);sigaddset(&blocked,SIGALRM);if(sigprocmask(SIG_BLOCK,&blocked,&oldmask))goto done;masked=1;
 c.origin=millis(&c);
 for(unsigned i=0;i<136;i++)irq_before[i]=c.rtos[0xaac/4+i];
 if(!kernel_read(&c,EP_KERNEL_STATE_VA,kernel_before,EP_KERNEL_STATE_BYTES)||!kernel_read(&c,EP_KERNEL_MUTEX_VA,kernel_before+EP_KERNEL_STATE_BYTES,12))goto done;
 owner_captured=1;
 rc=p23_run_stages(&c,&q,&r,memory,argv[3]);
 for(unsigned i=0;i<136;i++)irq_after[i]=c.rtos[0xaac/4+i];
 if(kernel_read(&c,EP_KERNEL_STATE_VA,kernel_after,EP_KERNEL_STATE_BYTES)&&kernel_read(&c,EP_KERNEL_MUTEX_VA,kernel_after+EP_KERNEL_STATE_BYTES,12))owner_captured=2;
 r.owner_records=owner_captured;r.owner_stable=owner_captured==2&&!memcmp(irq_before,irq_after,sizeof irq_before);r.owner_crc=bitb_crc(irq_after,sizeof irq_after);
 r.kernel_restored=owner_captured==2&&!memcmp(kernel_before,kernel_after,sizeof kernel_before);
 if(!r.owner_stable||!r.kernel_restored){rc=1;r.error=BJ_CONTEXT;r.safe_to_release=0;r.quarantine_reason=3;}
 done:
 if(rc&&!r.error)r.error=BJ_PREPARE;
 if(c.log)fprintf(c.log,"WORKER_PREPARE_RESULT stage=%s ready=%u native_library_guards=%u kernel_guards=%u rtos_guards=%u mmio_modified=%u safe_to_release=%u error=%u\n",prepare_stage,r.ready,native_guards_verified,kernel_guards_verified,r.rtos_guards,r.mmio_modified,r.safe_to_release,r.error);
 r6_timing("WORKER_NATIVE_TERMINAL","EVENT",0,!rc);if(events>=0)write(events,r.ready?"D":"E",1);
 if(c.gate_log){fprintf(c.gate_log,"GATE_SUMMARY state_calls=%u idle_calls=%u rejections=%u last_type=%s last_reason=%s hardware_jobs=%u mmio_modified=%u safe_to_release=%u\n",c.gate_state_calls,c.gate_idle_calls,c.gate_rejections,c.gate_last_type?c.gate_last_type:"NONE",c.gate_last_reason?c.gate_last_reason:"NONE",r.engine_jobs,r.mmio_modified,r.safe_to_release);if(fclose(c.gate_log))rc=1;}
 if(c.log){fprintf(c.log,"PIPELINE_RESULT error=%u failed_stage=%u safe=%u completed=%u jobs=%u\n",r.error,r.failed_stage,r.safe_to_release,proof.completed,r.engine_jobs);if(fclose(c.log))rc=1;}
 if(owner_captured){snprintf(path,sizeof path,"%s/p11_rtos_irq_before.bin",argv[3]);if(!save_bytes(path,irq_before,sizeof irq_before))rc=1;snprintf(path,sizeof path,"%s/p11_rtos_irq_after.bin",argv[3]);if(!save_bytes(path,irq_after,sizeof irq_after))rc=1;snprintf(path,sizeof path,"%s/p11_kernel_before.bin",argv[3]);if(!save_bytes(path,kernel_before,sizeof kernel_before))rc=1;snprintf(path,sizeof path,"%s/p11_kernel_after.bin",argv[3]);if(!save_bytes(path,kernel_after,sizeof kernel_after))rc=1;}
 proof.crc=bitb_crc(&proof,offsetof(struct j422_proof,crc));r.route_crc=proof.crc;r.route_samples=proof.started;r.route_verified=proof.started==3&&proof.completed==3;
 snprintf(path,sizeof path,"%s/j422-proof.bin",argv[3]);if(!save_bytes(path,&proof,sizeof proof))rc=1;

 if(rc&&!r.error)r.error=BJ_SAVE;r.phase=9;r.crc=bitb_crc(&r,offsetof(struct bitb_result,crc));if(!save_bytes(argv[2],&r,sizeof r))rc=1;
#ifdef NXL_FAST_PIPELINE
 full_jpeg_cache_close();
#endif
 if(memory!=MAP_FAILED)munmap(memory,BITB_BYTES);if(c.top&&(void*)c.top!=MAP_FAILED)munmap((void*)c.top,0x2000);if(c.lut&&(void*)c.lut!=MAP_FAILED)munmap((void*)c.lut,0x1000);if(c.rtos&&(void*)c.rtos!=MAP_FAILED)munmap((void*)c.rtos,4096);
 if(c.ldc&&(void*)c.ldc!=MAP_FAILED)munmap((void*)c.ldc,4096);
 if(c.h3dl&&(void*)c.h3dl!=MAP_FAILED)munmap((void*)c.h3dl,4096);
 if(c.rtos_code&&(void*)c.rtos_code!=MAP_FAILED)munmap((void*)c.rtos_code,4096);
 if(c.sma>=0)close(c.sma);if(c.ep>=0)close(c.ep);if(c.mem>=0)close(c.mem);r6_timing("WORKER_EXIT","EVENT",0,!rc);if(timing_file)fclose(timing_file);r6_timing_set(NULL);close(go);close(events);if(masked)sigprocmask(SIG_SETMASK,&oldmask,0);return rc;
}
