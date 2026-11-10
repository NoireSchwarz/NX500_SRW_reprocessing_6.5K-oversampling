/* SPDX-License-Identifier: AGPL-3.0-only. Actual predicates + exact R7c field
 * samples, with independent rejection mutations. No modeled hardware jobs. */
#include <assert.h>
#include <stdio.h>
#include "session_rearm_backend_r7i_legacy.h"
#include "rearm_front.h"
#include "fixtures/field_rearm_r7c.h"
_Static_assert(sizeof(struct r7_backend_sample)==1648,"field ARM sample layout");
_Static_assert(sizeof(struct fr_snapshot)==268,"field front sample layout");
static unsigned count;
#define CHECK(x) do{assert(x);count++;}while(0)
static const uint32_t front=0xa1c00000u,back=0xaf65c000u;
static struct r7_backend_sample first,s,next;
static void put(unsigned off,uint32_t v){memcpy(s.kernel+off,&v,4);}

int main(void){
 memcpy(&first,field_backend[0],sizeof first);
 CHECK(ep_sync_word(first.kernel+0x400)==0);CHECK(r7_backend_preview_profile(&first));
 for(unsigned i=0;i<16;i++){
  memcpy(&s,field_backend[i],sizeof s);
  CHECK(!r7_backend_error(&s,front,back));CHECK(r7_backend_stable(&first,&s));
 }
 for(unsigned i=0;i<3;i++){
  struct fr_snapshot fr;unsigned need=0;memcpy(&fr,field_front[i],sizeof fr);
  CHECK(fr.failure_mask==(1u<<9)); /* unchanged renderer must still refuse */
  CHECK(r7_front_retired(&fr,&need)&&need==1);
  fr.field[9].value=1;fr.field[9].passed=1;fr.failure_mask=0;
  CHECK(r7_front_retired(&fr,&need)&&!need);
 }
 for(unsigned test=0;test<15;test++){
  struct fr_snapshot fr;unsigned need=0;memcpy(&fr,field_front[0],sizeof fr);
  if(test==0)fr.field[9].value=2;
  if(test==1)fr.read_mask&=~1u;
  if(test==2)fr.stable_pair=0;
  if(test>=3){unsigned i=test-3;if(i==9)i=15;fr.failure_mask|=1u<<i;fr.field[i].passed=0;}
  CHECK(!r7_front_retired(&fr,&need));
 }
 CHECK(!r7_front_retired(NULL,NULL));
 for(unsigned test=0;test<17;test++){
  s=first;
  switch(test){
   case 0:s.kernel_readable=0;break;
   case 1:s.mutex[0]=0;break;
   case 2:put(0x74,0x20);break;
   case 3:put(0x31c,1);break;
   case 4:put(0x300,1);break;
   case 5:put(0x400,13);break;
   case 6:s.owner=1;break;
   case 7:s.raw[4]=0x20;break;
   case 8:s.mask[4]|=0x20;break;
   case 9:s.rtos[0]=0x402;break;
   case 10:s.otf[1]=1;break;
   case 11:s.jpeg[0]=1;break;
   case 12:s.jpeg[4]=1;break;
   case 13:s.dma[18]=front;break;
   case 14:s.dma[26]=back;break;
   case 15:s.dma[19]++;break;
   case 16:s.clock[0]|=0x80;break;
  }
  CHECK(r7_backend_error(&s,front,back)!=NULL);
 }
 /* All other controls and addresses retain the exact stability contract. */
 const unsigned rotating[]={18,19,26,27};
 for(unsigned i=0;i<32;i++){
  next=first;next.dma[i]^=0x40;
  unsigned allowed=0;for(unsigned k=0;k<4;k++)allowed|=i==rotating[k];
  CHECK(r7_backend_stable(&first,&next)==(int)allowed);
 }
 next=first;next.kernel[0x510]^=1;CHECK(!r7_backend_stable(&first,&next));
 next=first;next.jpeg[5]^=0x100;CHECK(!r7_backend_stable(&first,&next));
 next=first;next.clock[1]^=1;CHECK(!r7_backend_stable(&first,&next));
 next=first;next.mask[0]^=2;CHECK(!r7_backend_stable(&first,&next));
 /* Existing initialized, quiet JPEG profile remains valid and fully exact. */
 s=first;put(0x400,3);memset(s.rtos,0,sizeof s.rtos);memset(s.mask,0,sizeof s.mask);s.otf[0]=0;
 CHECK(!r7_backend_preview_profile(&s));CHECK(!r7_backend_error(&s,front,back));
 next=s;next.dma[18]^=0x40;CHECK(!r7_backend_stable(&s,&next));
 puts("PASS R7d real field retirement: all 16 liveview snapshots and 3 uninitialized-owner snapshots accepted; renderer still requires initialization");
 printf("checks=%u; active JPEG, old buffer references, unknown masks, invalid reads and unrelated changes still rejected\n",count);
 return 0;
}
