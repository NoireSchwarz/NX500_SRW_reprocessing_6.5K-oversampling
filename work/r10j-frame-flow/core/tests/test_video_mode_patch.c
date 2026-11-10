#include <assert.h>
#include <stdio.h>
#include "../../common/video_mode_patch.h"
#include "../../base/nx-lut-lite/src/video_menu_state.h"
static struct model {uint32_t words[2];unsigned mode,reads,writes,freezes,thaws,readies;} m;
static int get(void*v,uint32_t a,uint32_t*out){(void)v;assert(a==VM_HEIGHT||a==VM_WIDTH);m.reads++;if(m.mode==5&&m.reads==6)return 0;*out=m.words[a==VM_WIDTH];return 1;}
static int put(void*v,uint32_t a,uint32_t n){(void)v;assert(a==VM_HEIGHT||a==VM_WIDTH);m.writes++;if((m.mode==3&&m.writes==1)||(m.mode==4&&m.writes==2)||(m.mode==8&&m.writes>=2))return 0;m.words[a==VM_WIDTH]=n;return 1;}
static int freeze(void*v){(void)v;m.freezes++;return m.mode!=2;}
static int thaw(void*v){(void)v;m.thaws++;return m.mode!=7;}
static int ready(void*v){(void)v;m.readies++;return m.mode!=1&&!(m.mode==6&&m.readies==2);}
int main(void){
 struct vm_ops o={NULL,freeze,thaw,ready,get,put};struct vm_result r;unsigned cases=0;
 for(unsigned direction=0;direction<2;direction++)for(unsigned mode=0;mode<9;mode++){
  memset(&m,0,sizeof m);m.mode=mode;m.words[0]=direction?0xe3a08e5au:0xe3a08e87u;m.words[1]=direction?0xe3a0ac0au:0xe3a0ac0fu;uint32_t original[2];memcpy(original,m.words,8);
  int ok=vm_apply(&o,direction?VM_UHD:VM_25K,&r);assert(ok==(mode==0));assert(m.thaws==m.freezes);if(mode==1||mode==2||mode==6)assert(!m.writes);if(mode==3||mode==4||mode==5)assert(r.rollback_verified&&!memcmp(m.words,original,8));if(mode==8)assert(r.rollback_attempted&&!r.rollback_verified);if(mode==0)assert(vm_pair(m.words)==(direction?VM_UHD:VM_25K));cases++;
 }
 memset(&m,0,sizeof m);m.words[0]=0xe3a08e87u;m.words[1]=0xe3a0ac0fu;assert(vm_apply(&o,VM_UHD,&r)&&!m.freezes&&!m.writes);assert(!vm_apply(&o,99,&r)&&!m.writes);
 for(unsigned i=0;i<2;i++)for(unsigned bit=0;bit<32;bit++){m.words[0]=0xe3a08e87u;m.words[1]=0xe3a0ac0fu;m.words[i]^=1u<<bit;m.writes=m.freezes=m.thaws=0;assert(!vm_apply(&o,VM_25K,&r)&&!m.writes&&!m.freezes);cases++;}
 struct nxl_video_status s;const char*good="NXL_VIDEO_MODE1\nstate=2K\nwidth=2560\nheight=1440\npid=123\nrecording_or_capture=0\nreason=CURRENT_CONSTRUCTOR_MAPPING\nwrites=0\nrollback_verified=0\nthreads_detached=0\nend=NXL_VIDEO_MODE_END1\n";assert(nxl_video_parse(good,&s)&&s.valid&&s.mode==2&&s.busy==0);char b[500];strcpy(b,good);*strstr(b,"end=")=0;assert(!nxl_video_parse(b,&s));strcpy(b,good);memcpy(strstr(b,"2560"),"3840",4);assert(!nxl_video_parse(b,&s));assert(!nxl_video_parse("state=2K\n",&s));
 for(int y=0;y<480;y++){int at=nxl_sidebar_at(y);assert(at==(y<104||y>=388?-1:(y-104)/95));}
 printf("PASS %u transactional mapping cases, no writes for busy/unknown/current pair, rollback/detach failure, strict status and three sidebar hit regions\n",cases);return 0;
}
