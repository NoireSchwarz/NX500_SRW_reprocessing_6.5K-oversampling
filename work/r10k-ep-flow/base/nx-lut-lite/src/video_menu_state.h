#ifndef NXL_VIDEO_MENU_STATE_H
#define NXL_VIDEO_MENU_STATE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct nxl_video_status {int valid,mode,busy;unsigned width,height;char reason[96];};
static int nxl_video_parse(const char*t,struct nxl_video_status*s){
 memset(s,0,sizeof *s);s->busy=-1;const char*header="NXL_VIDEO_MODE1\n";if(strncmp(t,header,strlen(header)))return 0;t+=strlen(header);unsigned fields=0,ended=0;
 while(*t){const char*end=strchr(t,'\n');if(!end||end-t>200)return 0;char line[201];memcpy(line,t,(size_t)(end-t));line[end-t]=0;t=end+1;
  if(!strcmp(line,"end=NXL_VIDEO_MODE_END1")){if(*t||fields!=31)return 0;ended=1;break;}
  if(!strncmp(line,"state=",6)){if(fields&1)return 0;if(!strcmp(line+6,"UHD"))s->mode=1;else if(!strcmp(line+6,"2K"))s->mode=2;else if(!strcmp(line+6,"FULL"))s->mode=3;else if(!strcmp(line+6,"LEGACY"))s->mode=4;else if(strcmp(line+6,"UNKNOWN"))return 0;fields|=1;}
  else if(!strncmp(line,"width=",6)){if(fields&2||strlen(line+6)>4||strspn(line+6,"0123456789")!=strlen(line+6)||!line[6])return 0;s->width=(unsigned)strtoul(line+6,NULL,10);fields|=2;}
  else if(!strncmp(line,"height=",7)){if(fields&4||strlen(line+7)>4||strspn(line+7,"0123456789")!=strlen(line+7)||!line[7])return 0;s->height=(unsigned)strtoul(line+7,NULL,10);fields|=4;}
  else if(!strncmp(line,"recording_or_capture=",21)){if(fields&8)return 0;if(!strcmp(line+21,"-1"))s->busy=-1;else if(!strcmp(line+21,"0"))s->busy=0;else if(!strcmp(line+21,"1"))s->busy=1;else return 0;fields|=8;}
  else if(!strncmp(line,"reason=",7)){size_t n=strlen(line+7);if(fields&16||!n||n>=sizeof s->reason||strspn(line+7,"ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789")!=n)return 0;strcpy(s->reason,line+7);fields|=16;}
  else if(strncmp(line,"pid=",4)&&strncmp(line,"writes=",7)&&strncmp(line,"rollback_verified=",18)&&strncmp(line,"threads_detached=",17))return 0;
 }
 if(!ended||fields!=31||((s->mode==1||s->mode==3||s->mode==4)&&(s->width!=3840||s->height!=2160))||(s->mode==2&&(s->width!=2560||s->height!=1440))||(!s->mode&&(s->width||s->height)))return 0;s->valid=1;return 1;
}
static int nxl_sidebar_at(int y){return y>=104&&y<388?(y-104)/95:-1;}
#endif
