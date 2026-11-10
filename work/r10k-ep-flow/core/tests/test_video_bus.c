#include <assert.h>
#include <stdio.h>
#include "../../common/video_counter_capture.h"
#include "../../common/video_clock_capture.h"
#include "../../common/video_bus_capture.h"
static unsigned mode,calls,changes;
static int read_model(void*c,uint32_t at,void*out,unsigned bytes){
 (void)c;assert(bytes==4);uint32_t v=0;
 if(at==0x8146e438u)v=mode==3?0x1716:0x1717;
 else if(at==0x813f1b30u)v=0x1908;
 else if(at==0x813ebf60u)v=0xe005;
 else if(at==0x817dbbe8u)v=50+((mode==4)?changes++:0);
 else{
  unsigned i;for(i=0;i<VP_BUS_REGS;i++)if(vp_bus_address[i]==at)break;assert(i<VP_BUS_REGS);
  assert(at<0x20810400u||at>=0x20810500u);calls++;
  if(mode==2&&i==135)return 0;
  v=0x80001000u+i;if(mode==1&&i==33)v+=changes++&1u;
 }
 memcpy(out,&v,4);return 1;
}
int main(void){
 struct vp_bus_ring p;struct vp_record s={0};struct vp_reader r={0};r.physical=read_model;
 s.rtos[0]=0x1717;s.rtos[1]=0x1908;s.rtos[2]=0xe005;
 vp_bus_init(&p);vp_bus_capture(&r,&p,&s,1000,0);assert(p.record[0].status==1&&calls==272);
 vp_bus_capture(&r,&p,&s,6000,1);vp_bus_capture(&r,&p,&s,11000,2);assert(p.phase_mask==7&&p.total==3);
 vp_bus_capture(&r,&p,&s,16000,1);assert(p.phase_mask==3&&p.record[0].sequence==0);
 vp_bus_capture(&r,&p,&s,21000,2);assert(p.phase_mask==7);
 FILE*f=fopen("/bus-ring.bin","wb");assert(f&&fwrite(&p,1,sizeof p,f)==sizeof p&&!fclose(f));
 mode=1;changes=0;vp_bus_capture(&r,&p,&s,26000,1);assert(p.record[1].status==2&&p.record[1].changed[1]==2);
 mode=2;vp_bus_capture(&r,&p,&s,31000,1);assert(p.record[1].status==3&&!(p.record[1].valid[4]&128));
 mode=3;vp_bus_capture(&r,&p,&s,36000,1);assert(p.record[1].status==4);
 mode=4;changes=0;vp_bus_capture(&r,&p,&s,41000,1);assert(p.record[1].status==1&&p.record[1].sequence_after!=p.record[1].sequence_before);
 unsigned total=p.total;vp_bus_capture(&r,&p,&s,46000,3);assert(p.total==total);
 puts("PASS DMA snapshots: exact whitelist, masks across words, phase replacement, unavailable/config-change/frame-crossing, bounded header");return 0;
}
