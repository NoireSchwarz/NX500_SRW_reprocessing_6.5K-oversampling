#include <assert.h>
#include <stdio.h>
#include "../../common/video_counter_capture.h"
#include "../../common/video_clock_capture.h"
#include "../../common/video_qos_capture.h"
static unsigned bridge_reads,bridge_allowed,changed,missing,reads;
static int read_model(void*c,uint32_t at,void*out,unsigned bytes){
 (void)c;assert(bytes==4);uint32_t v=0;
 if(at==0x8146e438u)v=0x1717;
 else if(at==0x813f1b30u)v=0x1908;
 else if(at==0x813f09bcu)v=0x801cdf6c;
 else{
  unsigned i;for(i=0;i<VP_QOS_REGS;i++)if(vp_qos_address[i]==at)break;assert(i<VP_QOS_REGS);
  if(i==6u){assert(bridge_allowed);bridge_reads++;}
  if(missing&&i==0)return 0;
  v=0x1000u+i;if(changed&&i==0)v+=reads++&1u;
 }
 memcpy(out,&v,4);return 1;
}
int main(void){
 struct vp_qos_ring p;struct vp_record s={0};struct vp_reader r={0};r.physical=read_model;
 s.rtos[0]=0x1717;s.rtos[1]=0x1908;s.rtos[2]=0xe005;s.valid=VP_SOURCE|VP_HEVC;s.hevc[1]=5;
 vp_qos_init(&p);vp_qos_capture(&r,&p,&s,1000,0);assert(!bridge_reads&&p.record[0].status==1&&!(p.record[0].valid_mask&(1u<<6)));
 bridge_allowed=1;vp_qos_capture(&r,&p,&s,6000,1);assert(bridge_reads==2&&p.record[1].status==1&&p.record[1].valid_mask==0x1ffffu);
 bridge_allowed=0;vp_qos_capture(&r,&p,&s,11000,2);assert(bridge_reads==2&&p.phase_mask==7u);
 bridge_allowed=1;vp_qos_capture(&r,&p,&s,16000,1);assert(p.phase_mask==3u);bridge_allowed=0;vp_qos_capture(&r,&p,&s,21000,2);assert(p.phase_mask==7u&&p.record[0].sequence==0&&p.record[1].sequence==3&&p.record[2].sequence==4);
 FILE*f=fopen("/qos-ring.bin","wb");assert(f&&fwrite(&p,1,sizeof p,f)==sizeof p&&!fclose(f));
 s.hevc[1]=0;vp_qos_capture(&r,&p,&s,26000,1);assert(bridge_reads==4&&!(p.record[1].valid_mask&(1u<<6)));
 changed=1;vp_qos_capture(&r,&p,&s,31000,1);assert(p.record[1].status==2);changed=0;missing=1;vp_qos_capture(&r,&p,&s,36000,1);assert(p.record[1].status==3&&!(p.record[1].valid_mask&1));
 puts("PASS native QoS reader: preview/record/stop/restart slots, power-state gating, changed and unavailable reads; fixed existing header capacity");return 0;
}
