/* SPDX-License-Identifier: AGPL-3.0-only. Bounded active LDC parameter survey.
 * data[19] is a POINTER CANDIDATE observed in prior reports, not an assertion
 * of its coefficient type. Reads remain inside ordinary RTOS RAM. */
static void lens_follow_sample(unsigned sequence,const struct p36_snapshot*s){
 const struct p36_blob*b=&s->extra.ldc;
 if(b->state!=1||b->words!=21||!p34_ram_range(b->pointer,108))return;
 uint64_t begin=now();uint32_t data[6+27+64]={b->pointer,0,0,0,27,64};
 if(!p36_words(typed_reader,NULL,b->pointer,data+6,27)||memcmp(data+6,b->data,84))return;
 data[1]=1;data[2]=data[6+19];
 if(p34_ram_range(data[2],256)&&p36_words(typed_reader,NULL,data[2],data+6+27,64))data[3]=1;
 uint32_t again;if(!typed_reader(NULL,0x8142efc8u+548u,&again,1)||again!=b->pointer)return;
 static uint32_t previous[6+27+64];static uint64_t last;
 if(!last||memcmp(data,previous,sizeof data)||begin-last>=1000000000u){
  frame(context_file,8,sequence,begin,now(),data,sizeof data);memcpy(previous,data,sizeof data);last=begin;
 }
}
