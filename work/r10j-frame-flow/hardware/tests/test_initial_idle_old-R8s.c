
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "../src/ep_sync_core.h"
struct native_context {volatile uint32_t *top,*lut,*rtos;unsigned lut_mode;FILE *log,*gate_log;unsigned gate_state_calls,gate_idle_calls,gate_rejections;const char *gate_last_type,*gate_last_reason;};
static struct native_context c;
static unsigned char kernel_before[EP_KERNEL_STATE_BYTES+12],raw_state[EP_KERNEL_STATE_BYTES];
static unsigned char report_kernel[EP_KERNEL_STATE_BYTES+12];
static uint32_t mutex_words[3],top[2048],jpeg[1024],rtos[1024];
static unsigned owner_captured,read_failure,sleeps;static int mutation_bank;static unsigned mutation_offset,mutation_value,sleep_failure;
static int kernel_read(struct native_context *v,uint32_t va,void *out,size_t n){
 (void)v;if((va==EP_KERNEL_STATE_VA&&read_failure==1)||(va==EP_KERNEL_MUTEX_VA&&read_failure==2))return 0;
 if(va==EP_KERNEL_STATE_VA){assert(n==sizeof raw_state);memcpy(out,raw_state,n);return 1;}
 assert(va==EP_KERNEL_MUTEX_VA&&n==12);memcpy(out,mutex_words,n);return 1;
}
static unsigned mutation_when=1;
static int sample_sleep(const struct timespec *a,struct timespec *b){
 (void)a;(void)b;sleeps++;
 if(sleep_failure){errno=EINTR;return -1;}
 if(sleeps==mutation_when&&mutation_bank>=0){uint32_t *p=mutation_bank==0?top:mutation_bank==1?jpeg:rtos;p[mutation_offset/4]^=mutation_value;}
 return 0;
}
#define nanosleep sample_sleep

static uint32_t settled_value;static unsigned settle_changed,change_clock_at_settle,change_config_at_settle;
static uint32_t settle_read(struct native_context*v){
 if(settle_changed)jpeg[0]=settled_value;
 if(change_clock_at_settle)top[0x1020/4]|=0x80;
 if(change_config_at_settle)jpeg[4/4]^=0x100;
 return v->lut[0];
}
#define JPEG_GATE_SETTLE_READ(c) settle_read(c)

#define EXPECT_NEW 0
#include "../../../r8s-recorder-yield/hardware/src/jpeg_gate_diag.inc"

struct testcase{const char*name;uint32_t seed,next;unsigned change;int initial_bank;unsigned initial_offset,initial_value;int late_bank;unsigned late_offset,late_value,clock_at_settle,config_at_settle,old_pass,new_pass;};
static const struct testcase cases[]={{"stable_zero",0,0,0,-1,0,0,-1,0,0,0,0,1,1},
{"prior_four_to_zero",4,0,1,-1,0,0,-1,0,0,0,0,1,1},
{"actual_R8s_field_to_zero",78918864,0,1,-1,0,0,-1,0,0,0,0,0,1},
{"inactive_all_other_bits_to_zero",4294967292,0,1,-1,0,0,-1,0,0,0,0,0,1},
{"stable_same_field_value",78918864,0,0,-1,0,0,-1,0,0,0,0,1,1},
{"initial_core_start",1,0,1,-1,0,0,-1,0,0,0,0,0,0},
{"initial_core_reset",2,0,1,-1,0,0,-1,0,0,0,0,0,0},
{"initial_core_start_field",78918865,0,1,-1,0,0,-1,0,0,0,0,0,0},
{"initial_core_reset_field",78918866,0,1,-1,0,0,-1,0,0,0,0,0,0},
{"new_nonzero_status",0,4,1,-1,0,0,-1,0,0,0,0,0,0},
{"nonzero_to_other_nonzero",4,8,1,-1,0,0,-1,0,0,0,0,0,0},
{"DMA_start",0,0,0,1,2048,1,-1,0,0,0,0,0,0},
{"DMA_reset",0,0,0,1,2048,2,-1,0,0,0,0,0,0},
{"initial_JPEG_clock0_gated",0,0,0,0,4128,128,-1,0,0,0,0,0,0},
{"initial_JPEG_clock1_gated",0,0,0,0,4132,131072,-1,0,0,0,0,0,0},
{"clock_gates_during_initial_settle",4,0,1,-1,0,0,-1,0,0,1,0,1,0},
{"clock0_gates_during_window",0,0,0,-1,0,0,0,4128,128,0,0,1,0},
{"clock1_gates_during_window",0,0,0,-1,0,0,0,4132,131072,0,0,1,0},
{"new_completion_inside_window",4,0,1,-1,0,0,1,0,4,0,0,0,0},
{"new_core_start_inside_window",0,0,0,-1,0,0,1,0,1,0,0,0,0},
{"new_DMA_start_inside_window",0,0,0,-1,0,0,1,2048,1,0,0,0,0},
{"new_JPEG_config_inside_window",0,0,0,-1,0,0,1,8,256,0,0,0,0},
{"config_changes_during_settle",4,0,1,-1,0,0,-1,0,0,0,1,0,0},
{"initial_OTF_read",0,0,0,0,4376,1,-1,0,0,0,0,0,0},
{"initial_OTF_write",0,0,0,0,4632,1,-1,0,0,0,0,0,0},
{"new_OTF",0,0,0,-1,0,0,0,4376,1,0,0,0,0},
{"initial_RTOS_owner",0,0,0,2,3224,1,-1,0,0,0,0,0,0},
{"changed_DMA_address",0,0,0,-1,0,0,0,4864,4,0,0,0,0},
{"sample_sleep_interrupted",0,0,0,-2,0,0,-1,0,0,0,0,0,0},
{"final_clock_gates",0,0,0,-1,0,0,0,4128,128,0,0,1,0},
{"final_JPEG_completion",0,0,0,-1,0,0,1,0,4,0,0,1,0},
{"final_DMA_address_change",0,0,0,-1,0,0,0,4864,4,0,0,1,0},
{"final_raw_pending",0,0,0,-1,0,0,0,4296,1,0,0,1,0},
{"initial_TOP_mask_0",0,0,0,0,4208,1,-1,0,0,0,0,0,0},
{"initial_TOP_raw_0",0,0,0,0,4296,1,-1,0,0,0,0,0,0},
{"initial_RTOS_mask_0",0,0,0,2,2732,1,-1,0,0,0,0,0,0},
{"late_TOP_raw_0",0,0,0,-1,0,0,0,4296,1,0,0,0,0},
{"initial_TOP_mask_1",0,0,0,0,4212,1,-1,0,0,0,0,0,0},
{"initial_TOP_raw_1",0,0,0,0,4300,1,-1,0,0,0,0,0,0},
{"initial_RTOS_mask_1",0,0,0,2,2736,1,-1,0,0,0,0,0,0},
{"late_TOP_raw_1",0,0,0,-1,0,0,0,4300,1,0,0,0,0},
{"initial_TOP_mask_2",0,0,0,0,4216,1,-1,0,0,0,0,0,0},
{"initial_TOP_raw_2",0,0,0,0,4304,1,-1,0,0,0,0,0,0},
{"initial_RTOS_mask_2",0,0,0,2,2740,1,-1,0,0,0,0,0,0},
{"late_TOP_raw_2",0,0,0,-1,0,0,0,4304,1,0,0,0,0},
{"initial_TOP_mask_3",0,0,0,0,4220,1,-1,0,0,0,0,0,0},
{"initial_TOP_raw_3",0,0,0,0,4308,1,-1,0,0,0,0,0,0},
{"initial_RTOS_mask_3",0,0,0,2,2744,1,-1,0,0,0,0,0,0},
{"late_TOP_raw_3",0,0,0,-1,0,0,0,4308,1,0,0,0,0},
{"initial_TOP_mask_4",0,0,0,0,4224,1,-1,0,0,0,0,0,0},
{"initial_TOP_raw_4",0,0,0,0,4312,1,-1,0,0,0,0,0,0},
{"initial_RTOS_mask_4",0,0,0,2,2748,1,-1,0,0,0,0,0,0},
{"late_TOP_raw_4",0,0,0,-1,0,0,0,4312,1,0,0,0,0},};
static void reset(void){
 memset(top,0,sizeof top);memset(jpeg,0,sizeof jpeg);memset(rtos,0,sizeof rtos);c.lut_mode=0;c.top=top;c.lut=jpeg;c.rtos=rtos;
 c.gate_rejections=c.gate_state_calls=c.gate_idle_calls=0;c.gate_last_reason=NULL;owner_captured=1;read_failure=sleeps=sleep_failure=0;mutation_bank=-1;
 memcpy(kernel_before,report_kernel,sizeof kernel_before);memcpy(raw_state,kernel_before,sizeof raw_state);memcpy(mutex_words,kernel_before+EP_KERNEL_STATE_BYTES,12);
}
int main(void){
 FILE*f=fopen("/mnt/mmc/kernel.bin","rb");assert(f&&fread(report_kernel,1,sizeof report_kernel,f)==sizeof report_kernel&&!fclose(f));
 c.log=fopen("/mnt/mmc/gate-main.log","w");c.gate_log=fopen("/mnt/mmc/gate-details.log","w");assert(c.log&&c.gate_log);
 for(unsigned i=0;i<sizeof cases/sizeof *cases;i++){
  const struct testcase*t=cases+i;reset();mutation_when=!strncmp(t->name,"final_",6)?16:1;jpeg[0]=t->seed;settled_value=t->next;settle_changed=t->change;change_clock_at_settle=t->clock_at_settle;change_config_at_settle=t->config_at_settle;
  if(t->initial_bank>=0)(t->initial_bank==0?top:t->initial_bank==1?jpeg:rtos)[t->initial_offset/4]=t->initial_value;
  if(t->initial_bank==-2)sleep_failure=1;mutation_bank=t->late_bank;mutation_offset=t->late_offset;mutation_value=t->late_value;
  int result=idle(&c);unsigned want=EXPECT_NEW?t->new_pass:t->old_pass;
  if(result!=(int)want){printf("FAIL %s got=%d wanted=%u reason=%s\n",t->name,result,want,c.gate_last_reason?c.gate_last_reason:"PASS");return 1;}
  if(result)assert(sleeps==16);
  printf("CASE %s pass=%d sleeps=%u reason=%s\n",t->name,result,sleeps,c.gate_last_reason?c.gate_last_reason:"PASS");
 }
 unsigned count=0;
 for(unsigned kind=0;kind<2;kind++)for(unsigned locked=0;locked<(kind?4u:3u);locked++)for(unsigned fault=0;fault<(kind?15u:3u);fault++){
  reset();c.lut_mode=kind;mutex_words[0]=locked?0:1;uint32_t core=kind&&locked==3?0x20:0,acc=locked==2?(kind?0x80:0x24):0;memcpy(raw_state+0x64,&core,4);memcpy(raw_state+0x74,&acc,4);
  if(fault==1)raw_state[0x64]^=1;if(fault==2)raw_state[0x74]^=1;
  if(fault>=3&&fault<12){unsigned q=(fault-3)/3,part=(fault-3)%3,va=(uint32_t[]){0xc075551c,0xc0755528,0xc0755604}[q],off=va-EP_KERNEL_STATE_VA+part*4;uint32_t bad=part?0:1;memcpy(raw_state+off,&bad,4);memcpy(kernel_before+off,&bad,4);}
  if(fault==12){raw_state[0x68]^=1;kernel_before[0x68]^=1;}if(fault==13)mutex_words[1]^=4;if(fault==14)owner_captured=0;
  assert(state(&c,locked)==(fault==0));count++;
 }
 assert(!fclose(c.log)&&!fclose(c.gate_log));printf("PASS %u idle scenarios and %u original ownership/queue scenarios; model RAM only\n",(unsigned)(sizeof cases/sizeof *cases),count);return 0;
}
