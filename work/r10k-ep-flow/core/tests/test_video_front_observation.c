#include <assert.h>
#include <stdio.h>
#include "../../common/video_front_observation.h"
#include "../../common/video_sensor_observation.h"
static uint32_t object[VF_WORDS],pointer;static unsigned root_reads,body_reads,mode,body_requested;
static int reader(void*ctx,uint32_t at,void*out,unsigned n){
 (void)ctx;assert(n<=sizeof object);
 if(mode==1)return 0;
 if(at==VF_ROOT){uint32_t p=mode==2?0:mode==3?0x20810000u:pointer;if(mode==6&&root_reads)p+=4;memcpy(out,&p,4);root_reads++;return 1;}
 if(at==VF_SECONDARY_ROOT){uint32_t p=pointer+0x230u+(mode==4?4u:0u);memcpy(out,&p,4);return 1;}
 assert(at==pointer);body_requested++;
 if(n==4){uint32_t v=mode==5?VF_VTABLE+4:VF_VTABLE;memcpy(out,&v,4);return 1;}
 if(mode==8)return 0;assert(n==sizeof object);memcpy(out,object,n);if(mode==7&&body_reads)((uint32_t*)out)[0x288/4]^=1;body_reads++;return 1;
}
static unsigned sensor_reads;
static int sensor_reader(void*ctx,uint32_t at,void*out,unsigned n){(void)ctx;assert(at==VS_OBJECT&&(n==4||n==112));if(mode==1)return 0;uint32_t data[28]={VS_VTABLE};data[2]=0x0a0a;data[4]=4096;data[5]=2160;if(mode==2)data[0]++;if(mode==3&&sensor_reads&&n==112)data[4]++;memcpy(out,data,n);if(n==112)sensor_reads++;return 1;}
int main(void){
 struct vf_sample s;unsigned expected[]={1,3,2,5,4,2,4,4,3};
 for(unsigned address=0;address<2;address++)for(mode=0;mode<9;mode++){
  pointer=address?0x8311a800u:0x821dc800u;root_reads=body_reads=body_requested=0;memset(object,0,sizeof object);object[0]=VF_VTABLE;object[0x288/4]=8;object[0x28c/4]=30;
  int ok=vf_capture(reader,NULL,&s);assert(ok==(mode==0)&&s.status==expected[mode]&&vf_valid(&s));if(mode>=2&&mode<=4)assert(!body_requested);
  if(!mode){for(unsigned i=0;i<sizeof s;i++){struct vf_sample bad=s;((unsigned char*)&bad)[i]^=1;assert(!vf_valid(&bad));}}
 }
 for(mode=0;mode<4;mode++){struct vs_sample s;sensor_reads=0;int ok=vs_capture(sensor_reader,NULL,&s);assert(ok==(mode==0)&&vs_valid(&s)&&s.status==(mode==0?1:mode==1?3:mode==2?2:4));for(unsigned i=0;i<sizeof s;i++){struct vs_sample bad=s;((unsigned char*)&bad)[i]^=1;assert(!vs_valid(&bad));}}
 mode=0;root_reads=body_reads=body_requested=sensor_reads=0;assert(vf_capture(reader,NULL,&s));FILE*f=fopen("/video-front.bin","wb");assert(f&&fwrite(&s,1,sizeof s,f)==sizeof s&&!fclose(f));struct vs_sample sensor;assert(vs_capture(sensor_reader,NULL,&sensor));f=fopen("/video-sensor.bin","wb");assert(f&&fwrite(&sensor,1,sizeof sensor,f)==sizeof sensor&&!fclose(f));
 puts("PASS frontend root/type/range/secondary relation, complete double-read, two heap placements, sensor-cache type/change/unavailable cases and 2552 byte corruption cases; ordinary RAM reads only");return 0;
}
