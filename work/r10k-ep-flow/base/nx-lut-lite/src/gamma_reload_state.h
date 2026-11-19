/* Strict native Gamma protocol, independent of HEVC probe schema. */
#ifndef NXL_GAMMA_RELOAD_STATE_H
#define NXL_GAMMA_RELOAD_STATE_H
struct nxl_gamma_status {int valid,mode,phase;unsigned epoch,queued_epoch,qualified,applies,invalidations,queues,fault;char reason[96];};
static int nxl_gamma_parse(const char*t,struct nxl_gamma_status*s){
 memset(s,0,sizeof*s);const char*head="NXL_GAMMA_RELOAD1\n";if(strncmp(t,head,strlen(head)))return 0;t+=strlen(head);unsigned fields=0;
 while(*t){const char*end=strchr(t,'\n');if(!end||end-t>160)return 0;char line[161];memcpy(line,t,(size_t)(end-t));line[end-t]=0;t=end+1;
  if(!strcmp(line,"end=NXL_GAMMA_RELOAD_END1")){if(*t||fields!=1023u||s->queued_epoch>s->epoch)return 0;
   if((s->phase==3||s->phase==4)&&(!s->epoch||s->queued_epoch!=s->epoch||!s->queues))return 0;
   if(s->phase==4&&s->mode)return 0;s->valid=1;return 1;}
  if(!strncmp(line,"mode=",5)){if(fields&1)return 0;const char*v=line+5;if(!strcmp(v,"FACTORY"))s->mode=0;else if(!strcmp(v,"LIFT"))s->mode=1;else if(!strcmp(v,"LOG"))s->mode=2;else return 0;fields|=1;}
  else if(!strncmp(line,"phase=",6)){if(fields&2)return 0;const char*names[]={"OFF","PREPARED","WAIT_NATIVE","NATIVE_QUEUED","FACTORY_QUEUED","FAILED"};unsigned i;for(i=0;i<6;i++)if(!strcmp(line+6,names[i]))break;if(i==6)return 0;s->phase=(int)i;fields|=2;}
  else if(!strncmp(line,"reason=",7)){size_t n=strlen(line+7);if(fields&4||!n||n>=sizeof s->reason||strspn(line+7,"ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789")!=n)return 0;strcpy(s->reason,line+7);fields|=4;}
  else{
   const char*keys[]={"epoch=","queued_epoch=","qualified=","applies=","invalidations=","queues=","fault="};unsigned*dest[]={&s->epoch,&s->queued_epoch,&s->qualified,&s->applies,&s->invalidations,&s->queues,&s->fault};unsigned i;for(i=0;i<7;i++)if(!strncmp(line,keys[i],strlen(keys[i])))break;if(i==7)return 0;unsigned bit=8u<<i;if(fields&bit)return 0;const char*v=line+strlen(keys[i]);size_t n=strlen(v);if(!n||n>9||strspn(v,"0123456789")!=n)return 0;unsigned x=0;for(size_t k=0;k<n;k++)x=x*10u+(unsigned)(v[k]-'0');*dest[i]=x;fields|=bit;
  }
 }return 0;
}
/* A failed command always offers a guarded factory restore next. */
static int nxl_gamma_next_command(const struct nxl_gamma_status*s,int command_failed){
 if(command_failed||s->phase==1||s->phase==2||s->phase==5)return 22;
 return s->mode==2?22:21;
}
#endif
