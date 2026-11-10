#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "session_rearm_backend_r7i_legacy.h"
static struct r7_backend_sample good,now;
static void put(unsigned off,uint32_t v){memcpy(now.kernel+off,&v,4);}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f&&fread(good.kernel,1,sizeof good.kernel,f)==sizeof good.kernel&&fread(good.mutex,1,12,f)==12&&fgetc(f)==EOF&&!fclose(f));good.kernel_readable=good.top_readable=good.rtos_readable=good.jpeg_readable=1;unsigned count=0;uint32_t front=0xa1c00000u,back=0xaf65c000u;
#define PASS(x) do{assert(x);count++;}while(0)
#define BAD(code,why) do{now=good;code;PASS(r7_backend_error(&now,front,back)&&!strcmp(r7_backend_error(&now,front,back),why));}while(0)
PASS(!r7_backend_error(&good,front,back));PASS(sizeof good.kernel==0x554u);PASS(r7_backend_stable(&good,&good));
BAD(now.kernel_readable=0,"BACKEND_KERNEL_UNREADABLE");BAD(put(0,0),"BACKEND_KERNEL_DEVICE_CONTEXT_INVALID");BAD(put(60,0x20827000),"BACKEND_REGISTER_LAYOUT_INVALID");BAD(now.mutex[0]=0,"BACKEND_MUTEX_NOT_IDLE");BAD(put(0x74,0x24),"BACKEND_KERNEL_INTERRUPT_ENABLED");BAD(put(0x31c,1),"BACKEND_KERNEL_QUEUE_NOT_EMPTY");BAD(put(0x340,1),"BACKEND_KERNEL_QUEUE_NOT_EMPTY");BAD(put(0x300,1),"BACKEND_KERNEL_ERROR_NONZERO");BAD(put(0x400,13),"BACKEND_KERNEL_SELECTOR_UNPROVEN");BAD(now.top_readable=0,"BACKEND_TOP_UNREADABLE");BAD(now.clock[0]=0x80,"BACKEND_JPEG_CLOCK_GATED");BAD(now.clock[1]=0x1e0000,"BACKEND_JPEG_CLOCK_GATED");BAD(now.rtos_readable=0,"BACKEND_RTOS_STATE_UNREADABLE");BAD(now.owner=1,"BACKEND_RTOS_OWNER_BUSY");for(unsigned k=0;k<5;k++){BAD(now.rtos[k]=1,"BACKEND_IRQ_NOT_QUIET");BAD(now.mask[k]=1,"BACKEND_IRQ_NOT_QUIET");BAD(now.raw[k]=1,"BACKEND_IRQ_NOT_QUIET");}BAD(now.otf[0]=1,"BACKEND_OTF_ACTIVE");BAD(now.otf[1]=1,"BACKEND_OTF_ACTIVE");BAD(now.jpeg_readable=0,"BACKEND_JPEG_UNREADABLE");BAD(now.jpeg[0]=1,"BACKEND_JPEG_ENGINE_ACTIVE");BAD(now.jpeg[4]=1,"BACKEND_JPEG_ENGINE_ACTIVE");
for(unsigned i=0;i<32;i++){BAD(now.dma[i]=front,"BACKEND_DMA_REFERENCES_OLD_PA");BAD(now.dma[i]=back+201326592u-1,"BACKEND_DMA_REFERENCES_OLD_PA");}
for(unsigned i=8;i<=10;i++){
 now=good;now.jpeg[i]=front+134217728u-1;PASS(!r7_backend_error(&now,front,back));PASS(r7_backend_latched_jpeg_refs(&now,front,back)==(1u<<(i-8)));
 now=good;now.jpeg[i]=back;PASS(!r7_backend_error(&now,front,back));PASS(r7_backend_latched_jpeg_refs(&now,front,back)==(1u<<(i-8)));
 BAD(now.jpeg[i]=back;now.jpeg[0]=1,"BACKEND_JPEG_ENGINE_ACTIVE");BAD(now.jpeg[i]=back;now.jpeg[4]=1,"BACKEND_JPEG_ENGINE_ACTIVE");
}
now=good;now.dma[0]=0x94800000u;PASS(!r7_backend_error(&now,front,back));PASS(!r7_backend_stable(&now,&good));now=good;now.jpeg[8]=front+134217728u;PASS(!r7_backend_error(&now,front,back));now=good;put(0x400,5);PASS(!r7_backend_error(&now,front,back));printf("PASS backend retirement: exact current kernel fixture + readable/layout/queue/IRQ/clock/engine/DMA/typedPA/stability; checks=%u\n",count);return 0;}
