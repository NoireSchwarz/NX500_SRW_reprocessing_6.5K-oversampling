/* Strict independent protocol for the passive sampler. */
#ifndef NXL_VIDEO_PROBE_STATE_H
#define NXL_VIDEO_PROBE_STATE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct nxl_probe_status {int valid,state,protocol,experiment;unsigned samples,period_ms,experiment_epoch,experiment_reason;char reason[96];};
static int nxl_probe_uint(const char*t,unsigned max,unsigned*out){size_t n=strlen(t);if(!n||n>8||strspn(t,"0123456789")!=n)return 0;unsigned long v=strtoul(t,0,10);if(v>max)return 0;*out=(unsigned)v;return 1;}
static int nxl_probe_parse(const char*t,struct nxl_probe_status*s){
 memset(s,0,sizeof*s);const char*head="NXL_VIDEO_PROBE1\n";s->protocol=1;
 if(!strncmp(t,"NXL_VIDEO_PROBE3\n",17)){head="NXL_VIDEO_PROBE3\n";s->protocol=3;}
 if(!strncmp(t,"NXL_VIDEO_PROBE2\n",17)){head="NXL_VIDEO_PROBE2\n";s->protocol=2;}
 if(strncmp(t,head,strlen(head)))return 0;t+=strlen(head);unsigned fields=0;
 while(*t){const char*end=strchr(t,'\n');if(!end||end-t>270)return 0;char line[271];memcpy(line,t,(size_t)(end-t));line[end-t]=0;t=end+1;
  if(!strcmp(line,s->protocol==3?"end=NXL_VIDEO_PROBE_END3":s->protocol==2?"end=NXL_VIDEO_PROBE_END2":"end=NXL_VIDEO_PROBE_END1")){
   if(*t||fields!=(s->protocol>=2?255u:31u))return 0;
   if(s->protocol==1){s->experiment=!strcmp(s->reason,"TRACE_MEMORY_TEST_UNAVAILABLE")?5:!strcmp(s->reason,"TRACE_MEMORY_TEST_ABORTED")?4:0;}
   if(s->experiment_epoch>(s->protocol==3?3u:32u)||(s->experiment==3&&s->experiment_epoch!=(s->protocol==3?3u:32u))||
    ((s->experiment==1||s->experiment==5||s->experiment==6||s->experiment==0)&&s->experiment_epoch)||
    (s->experiment==2&&!s->experiment_epoch))return 0;
   s->valid=1;return 1;
  }
  if(!strncmp(line,"state=",6)){if(fields&1)return 0;if(!strcmp(line+6,"OFF"))s->state=0;else if(!strcmp(line+6,"ON"))s->state=1;else if(!strcmp(line+6,"SAVING"))s->state=2;else if(!strcmp(line+6,"PENDING"))s->state=3;else if(!strcmp(line+6,"FULL"))s->state=4;else return 0;fields|=1;}
  else if(!strncmp(line,"reason=",7)){size_t n=strlen(line+7);if(fields&2||!n||n>=sizeof s->reason||strspn(line+7,"ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789")!=n)return 0;strcpy(s->reason,line+7);fields|=2;}
  else if(!strncmp(line,"report=",7)){if(fields&4)return 0;const char*p=line+7;if(*p){const char*prefix="/mnt/mmc/NXDEMO51/VIDEO_STAGE_";size_t n=strlen(prefix);if(strncmp(p,prefix,n)||!p[n]||strspn(p+n,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_0123456789")!=strlen(p+n))return 0;}fields|=4;}
  else if(!strncmp(line,"samples=",8)){if(fields&8||!nxl_probe_uint(line+8,6000,&s->samples))return 0;fields|=8;}
  else if(!strncmp(line,"period_ms=",10)){if(fields&16||!nxl_probe_uint(line+10,1000,&s->period_ms)||(s->period_ms!=100&&s->period_ms!=200&&s->period_ms!=500&&s->period_ms!=1000))return 0;fields|=16;}
  else if(s->protocol>=2&&!strncmp(line,"experiment=",11)){
   static const char*names[]={"UNKNOWN","READY","RUNNING","DONE","ABORTED","UNAVAILABLE","NONE"};unsigned i;
   if(fields&32)return 0;for(i=0;i<7;i++)if(!strcmp(line+11,names[i]))break;if(i==7)return 0;
   s->experiment=(int)i;fields|=32;
  }
  else if(s->protocol>=2&&!strncmp(line,"experiment_epoch=",17)){
   if(fields&64||!nxl_probe_uint(line+17,32,&s->experiment_epoch))return 0;fields|=64;
  }
  else if(s->protocol>=2&&!strncmp(line,"experiment_reason=",18)){
   if(fields&128||!nxl_probe_uint(line+18,255,&s->experiment_reason))return 0;fields|=128;
  }
  else return 0;
 }return 0;
}
#endif
