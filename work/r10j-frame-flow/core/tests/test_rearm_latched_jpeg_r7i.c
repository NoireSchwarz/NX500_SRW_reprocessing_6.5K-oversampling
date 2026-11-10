/* Exact failed RESET samples plus independent negative mutations. */
#include <assert.h>
#include <stdio.h>
#include "session_rearm_backend_r7i_legacy.h"
#include "fixtures/field_rearm_r7h.h"
_Static_assert(sizeof(struct r7_backend_sample)==1648,"field sample ABI");
static unsigned checks;
static const uint32_t front=0xa1c00000u,back=0xaf65c000u;
static struct r7_backend_sample first,s,quiet;
#define CHECK(x) do{assert(x);checks++;}while(0)
static void put(unsigned off,uint32_t v){memcpy(s.kernel+off,&v,4);}

int main(void){
 memcpy(&first,field_r7h_backend[0],sizeof first);
 CHECK(first.jpeg[10]==0xba90c000u);
 CHECK(r7_pa_ref(first.jpeg[10],back,201326592u));
 for(unsigned i=0;i<16;i++){
  memcpy(&s,field_r7h_backend[i],sizeof s);
  CHECK(r7_backend_preview_profile(&s));
  CHECK(r7_backend_latched_jpeg_refs(&s,front,back)==4);
  CHECK(!r7_backend_error(&s,front,back));
  CHECK(r7_backend_stable(&first,&s));
 }
 /* A previous configuration may persist in either verified idle profile.
  * All three pointers remain part of the exact stability comparison. */
 for(unsigned selector=0;selector<=5;selector++){
  if(selector!=0&&selector!=3&&selector!=5)continue;
  for(unsigned p=8;p<=10;p++)for(unsigned range=0;range<2;range++){
   s=first;
   if(selector){put(0x400,selector);memset(s.rtos,0,sizeof s.rtos);memset(s.mask,0,sizeof s.mask);s.otf[0]=0;}
   quiet=s;
   s.jpeg[p]=range?back:front;
   CHECK(!r7_backend_error(&s,front,back));
   CHECK(r7_backend_latched_jpeg_refs(&s,front,back)&(1u<<(p-8)));
   CHECK(!r7_backend_stable(&quiet,&s));
   quiet=s;quiet.jpeg[p]++;
   CHECK(!r7_backend_stable(&quiet,&s));
   for(unsigned bank=0;bank<2;bank++)for(unsigned bit=0;bit<2;bit++){
    quiet=s;s.jpeg[bank?4:0]|=1u<<bit;
    CHECK(r7_backend_error(&s,front,back));s=quiet;
   }
  }
 }
 /* Live EP DMA references retain the entire old-range rejection. */
 for(unsigned i=0;i<32;i++)for(unsigned range=0;range<2;range++){
  s=first;s.dma[i]=(range?back+201326592u:front+134217728u)-1;
  CHECK(r7_backend_error(&s,front,back));
 }
 for(unsigned test=0;test<22;test++){
  s=first;
  switch(test){
   case 0:s.kernel_readable=0;break;
   case 1:s.top_readable=0;break;
   case 2:s.rtos_readable=0;break;
   case 3:s.jpeg_readable=0;break;
   case 4:s.mutex[0]=0;break;
   case 5:put(0x74,0x20);break;
   case 6:put(0x31c,1);break;
   case 7:put(0x340,1);break;
   case 8:put(0x300,1);break;
   case 9:put(0x400,13);break;
   case 10:s.owner=1;break;
   case 11:s.raw[4]=0x20;break;
   case 12:s.mask[4]|=0x20;break;
   case 13:s.rtos[0]=0x402;break;
   case 14:s.otf[1]=1;break;
   case 15:s.jpeg[0]=1;break;
   case 16:s.jpeg[4]=1;break;
   case 17:s.dma[18]=front;break;
   case 18:s.dma[26]=back;break;
   case 19:s.dma[19]++;break;
   case 20:s.clock[0]|=0x80;break;
   case 21:s.clock[1]|=0x1e0000;break;
  }
  CHECK(r7_backend_error(&s,front,back));
 }
 printf("PASS R7i: 16 exact field samples, latched config in preview/initialized idle, activity/queues/IRQs/reads/clocks/live DMA/stability rejection; checks=%u\n",checks);
 return 0;
}
