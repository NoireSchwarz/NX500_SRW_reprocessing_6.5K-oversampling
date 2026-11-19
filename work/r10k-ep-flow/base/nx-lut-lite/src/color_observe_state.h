/* Strict menu protocol for the independent passive observer. */
#ifndef NXL_COLOR_OBSERVE_STATE_H
#define NXL_COLOR_OBSERVE_STATE_H
struct nxl_color_status {unsigned state,samples,valid;char reason[96];};
static int nxl_color_parse(const char*t,struct nxl_color_status*s){
 memset(s,0,sizeof*s);const char*head="NXL_COLOR_OBSERVE1\n";if(strncmp(t,head,strlen(head)))return 0;t+=strlen(head);unsigned mask=0;
 while(*t){const char*end=strchr(t,'\n');if(!end||end-t>160)return 0;char line[161];memcpy(line,t,(size_t)(end-t));line[end-t]=0;t=end+1;
  if(!strcmp(line,"end=NXL_COLOR_OBSERVE_END1"))return !*t&&mask==15&&s->state<=6&&s->samples<=600&&s->valid<=s->samples;
  if(!strncmp(line,"reason=",7)){size_t n=strlen(line+7);if(mask&8||!n||n>=sizeof s->reason||strspn(line+7,"ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789")!=n)return 0;strcpy(s->reason,line+7);mask|=8;continue;}
  const char*keys[]={"state=","samples=","valid="};unsigned*dest[]={&s->state,&s->samples,&s->valid};unsigned i;for(i=0;i<3;i++)if(!strncmp(line,keys[i],strlen(keys[i])))break;if(i==3||mask&(1u<<i))return 0;
  const char*v=line+strlen(keys[i]);size_t n=strlen(v);if(!n||n>6||strspn(v,"0123456789")!=n)return 0;unsigned x=0;for(size_t k=0;k<n;k++)x=x*10u+(unsigned)(v[k]-'0');*dest[i]=x;mask|=1u<<i;
 }return 0;
}
#endif
