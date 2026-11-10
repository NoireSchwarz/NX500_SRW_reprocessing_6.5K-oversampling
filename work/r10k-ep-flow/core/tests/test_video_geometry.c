#include <assert.h>
#include <stdio.h>
#include "../../common/video_geometry_observation.h"
static unsigned mode,window_reads,mode_reads,body_reads;
static int reader(void*ctx,uint32_t at,void*out,unsigned bytes){(void)ctx;
 if(mode==1)return 0;
 if(at==VS_OBJECT){assert(bytes==4);uint32_t v=mode==2?VS_VTABLE+4:VS_VTABLE;memcpy(out,&v,4);return 1;}
 if(at==VS_OBJECT+8){uint32_t v=0x1717;if(mode==3&&mode_reads)v++;mode_reads++;memcpy(out,&v,4);return 1;}
 assert(at==VS_OBJECT+VG_OFFSET&&bytes==88);uint32_t b[VG_WORDS]={0};b[10]=3664;b[11]=6600;if(mode==4&&window_reads)b[10]++;window_reads++;body_reads++;memcpy(out,b,bytes);return 1;
}
int main(void){unsigned checks=0;for(mode=0;mode<5;mode++){window_reads=mode_reads=body_reads=0;struct vg_sample s;assert(vg_capture(reader,NULL,&s)==(mode==0)&&vg_valid(&s));assert(s.status==(mode==0?1:mode==1?3:mode==2?2:4));if(mode==2)assert(!body_reads);for(unsigned i=0;i<sizeof s;i++){struct vg_sample bad=s;((unsigned char*)&bad)[i]^=1;assert(!vg_valid(&bad));checks++;}}
 mode=0;window_reads=mode_reads=0;struct vg_sample s;assert(vg_capture(reader,NULL,&s));FILE*f=fopen("/video-window.bin","wb");assert(f&&fwrite(&s,1,sizeof s,f)==sizeof s&&!fclose(f));printf("PASS guarded sensor window ABI, changed mode/window/type and read failures; %u corruption cases\n",checks);return 0;}
