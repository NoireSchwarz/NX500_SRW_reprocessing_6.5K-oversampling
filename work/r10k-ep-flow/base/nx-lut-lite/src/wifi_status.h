/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_WIFI_STATUS_H
#define NXL_WIFI_STATUS_H
struct nxl_wifi_status{int valid,running,connected,ap_valid,ap_state,owned,security;char mode[12],ip[32],url[80],reason[64],ssid[33],password[64];};
static int nxl_wifi_unhex(const char*s,char*out,size_t cap){
 size_t n=strlen(s);if(n%2||n/2>=cap)return 0;
 for(size_t i=0;i<n;i+=2){unsigned v=0;for(unsigned j=0;j<2;j++){char c=s[i+j];if(c>='0'&&c<='9')v=v*16+(unsigned)(c-'0');else if(c>='a'&&c<='f')v=v*16+10+(unsigned)(c-'a');else return 0;}if(v<32||v>126)return 0;out[i/2]=(char)v;}out[n/2]=0;return 1;
}
static int nxl_wifi_status_parse(const char*s,struct nxl_wifi_status*t){
 struct nxl_wifi_status r={0};r.security=-1;char line[160];int begin=0,end=0;unsigned seen=0;
 while(*s){unsigned n=0;while(*s&&*s!='\n'){if(n>=sizeof line-1)return 0;line[n++]=*s++;}if(*s)s++;line[n]=0;
  if(!strcmp(line,"NXL_WIFI3")){if(begin)return 0;begin=1;continue;}if(!begin||end)return 0;
  if(!strcmp(line,"end=NXL_WIFI_END3")){end=1;continue;}
  const char*value=strchr(line,'=');if(!value)return 0;value++;unsigned bit=0;
  if(!strncmp(line,"running=",8)){bit=1;if(strcmp(value,"0")&&strcmp(value,"1"))return 0;r.running=value[0]-'0';}
  else if(!strncmp(line,"connected=",10)){bit=2;if(strcmp(value,"0")&&strcmp(value,"1"))return 0;r.connected=value[0]-'0';}
  else if(!strncmp(line,"ip=",3)){bit=4;if(strlen(value)>=sizeof r.ip)return 0;strcpy(r.ip,value);}
  else if(!strncmp(line,"url=",4)){bit=8;if(strlen(value)>=sizeof r.url)return 0;strcpy(r.url,value);}
  else if(!strncmp(line,"reason=",7)){bit=16;if(strlen(value)>=sizeof r.reason)return 0;strcpy(r.reason,value);}
  else if(!strncmp(line,"mode=",5)){bit=32;if(strcmp(value,"hotspot")&&strcmp(value,"station"))return 0;strcpy(r.mode,value);}
  else if(!strncmp(line,"ap_valid=",9)){bit=64;if(strcmp(value,"0")&&strcmp(value,"1"))return 0;r.ap_valid=value[0]-'0';}
  else if(!strncmp(line,"ap_state=",9)){bit=128;char*e;long n=strtol(value,&e,10);if(!*value||*e||n< -1||n>21)return 0;r.ap_state=(int)n;}
  else if(!strncmp(line,"owned=",6)){bit=256;if(strcmp(value,"0")&&strcmp(value,"1"))return 0;r.owned=value[0]-'0';}
  else if(!strncmp(line,"ssid_hex=",9)){bit=512;if(!nxl_wifi_unhex(value,r.ssid,sizeof r.ssid))return 0;}
  else if(!strncmp(line,"password_hex=",13)){bit=1024;if(!nxl_wifi_unhex(value,r.password,sizeof r.password))return 0;}
  else if(!strncmp(line,"security=",9)){bit=2048;if(!strcmp(value,"none"))r.security=0;else if(!strcmp(value,"wpa2"))r.security=1;else if(strcmp(value,"unknown"))return 0;}
  else return 0;if(seen&bit)return 0;seen|=bit;
 }
 if(!begin||!end||seen!=4095||r.ap_valid!=(r.ap_state>=0))return 0;
 unsigned a,b,c,d;char trailing;if(r.connected&&(sscanf(r.ip,"%u.%u.%u.%u%c",&a,&b,&c,&d,&trailing)!=4||a>255||b>255||c>255||d>255||a==0||a==127))return 0;
 if(r.connected!=!!r.ip[0]||(r.running&&!r.connected))return 0;
 char expected[80]="";if(r.connected)snprintf(expected,sizeof expected,"http://%s:8181/",r.ip);if(strcmp(expected,r.url))return 0;
 if(!strcmp(r.mode,"hotspot")&&r.connected&&(!r.ap_valid||!r.ssid[0]||r.security<0||(r.security==0&&r.password[0])||(r.security==1&&strlen(r.password)<8)))return 0;
 if((!r.connected||strcmp(r.mode,"hotspot"))&&(r.security!=-1||r.password[0]))return 0;
 r.valid=1;*t=r;return 1;
}
#endif
