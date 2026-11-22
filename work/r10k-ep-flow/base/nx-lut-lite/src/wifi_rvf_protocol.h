/* SPDX-License-Identifier: AGPL-3.0-only
 * NX500 1.12 stock network protocol. No code injection, MMIO or encoder SETs.
 * Bounds and names pinned against the original firmware, not the NX300 wiki.
 */
#ifndef NXL_RVF_PROTOCOL_H
#define NXL_RVF_PROTOCOL_H
#define RVF_FRAME_CAP (512u*1024u)
struct rvf_url { char ip[32],path[160]; unsigned port; };
static int rvf_smp_path(const char*s){
 if(strncmp(s,"/smp_",5))return 0;const char*p=s+5;unsigned n=0;while(*p>='0'&&*p<='9'){p++;if(++n>5)return 0;}return n&&*p=='_'&&!p[1];
}
static int rvf_url_parse(const char*s,const char*ip,struct rvf_url*u,int streaming){
 memset(u,0,sizeof*u);if(strncmp(s,"http://",7))return 0;char host[32],tail[200],extra;unsigned port=0;
 if(sscanf(s+7,"%31[0-9.]:%u%199s%c",host,&port,tail,&extra)!=3||strcmp(host,ip)||port<1024||port>65535||strlen(tail)>=sizeof u->path)return 0;
 if(streaming){if(strcmp(tail,"/livestream_mjpg.avi")&&strcmp(tail,"/qvga_livestream_mjpg.avi"))return 0;}else if(!rvf_smp_path(tail))return 0;
 strcpy(u->ip,host);strcpy(u->path,tail);u->port=port;return 1;
}
static int rvf_xml_value(const char*xml,const char*tag,char*out,size_t cap){
 char a[96],b[96];if(strlen(tag)>70)return 0;snprintf(a,sizeof a,"<%s>",tag);snprintf(b,sizeof b,"</%s>",tag);
 const char*p=strstr(xml,a);if(!p)return 0;p+=strlen(a);const char*end=strstr(p,b);if(!end||end==p||(size_t)(end-p)>=cap||strstr(end+strlen(b),a))return 0;
 memcpy(out,p,(size_t)(end-p));out[end-p]=0;return 1;
}
static int rvf_xml_unescape(char*s){
 char*d=s;for(char*p=s;*p;p++){if(*p=='&'){const char*names[]={"&lt;","&gt;","&amp;","&quot;","&apos;"};const char values[]="<>&\"'";unsigned i;
   for(i=0;i<5;i++)if(!strncmp(p,names[i],strlen(names[i]))){*d++=values[i];p+=strlen(names[i])-1;break;}if(i==5)return 0;
  }else *d++=*p;}*d=0;return 1;
}
static int rvf_control_path(const char*xml,char*out,size_t cap){
 const char*p=xml;unsigned found=0;while((p=strstr(p,"<service>"))){const char*end=strstr(p,"</service>");if(!end||end-p>8192)return 0;
  char service[8193];memcpy(service,p,(size_t)(end-p));service[end-p]=0;
  if(strstr(service,"<serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>")){if(++found!=1||!rvf_xml_value(service,"controlURL",out,cap)||!rvf_smp_path(out))return 0;}p=end+10;
 }return found==1;
}
static int rvf_http_status(const char*s){int code;char version[16];return sscanf(s,"%15s %d",version,&code)==2&&(!strcmp(version,"HTTP/1.1")||!strcmp(version,"HTTP/1.0"))?code:0;}
static int rvf_header(const char*s,const char*key,char*out,size_t cap){
 size_t k=strlen(key);unsigned found=0;const char*p=strstr(s,"\r\n");if(!p)return 0;p+=2;
 while(*p&&strncmp(p,"\r\n",2)){const char*e=strstr(p,"\r\n");if(!e)return 0;if((size_t)(e-p)>k+1&&!strncasecmp(p,key,k)&&p[k]==':'){
   const char*v=p+k+1;while(v<e&&(*v==' '||*v=='\t'))v++;const char*t=e;while(t>v&&(t[-1]==' '||t[-1]=='\t'))t--;
   if(++found!=1||t==v||(size_t)(t-v)>=cap)return 0;memcpy(out,v,(size_t)(t-v));out[t-v]=0;
  }p=e+2;}return found==1;
}
static uint32_t rvf_le32(const unsigned char*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
struct rvf_avi { unsigned width,height,usec,main_usec,scale,rate; char codec[5]; };
/* Walk the AVI header by chunk lengths. Do not search compressed payload for
 * header names. The OEM's zero-sized outer RIFF/movi lengths are streaming. */
static int rvf_avi_header(const unsigned char*p,size_t n,struct rvf_avi*out,size_t*offset){
 memset(out,0,sizeof*out);if(n<12)return 0;if(memcmp(p,"RIFF",4)||memcmp(p+8,"AVI ",4))return -1;
 size_t pos=12,ends[8];unsigned depth=0,vids=0,format=0;
 for(unsigned chunks=0;chunks<128;chunks++){
  while(depth&&pos==ends[depth-1])depth--;if(depth&&pos>ends[depth-1])return -1;
  if(pos+8>n)return 0;uint32_t len=rvf_le32(p+pos+4);if(len>65536&&!(!memcmp(p+pos,"LIST",4)&&pos+12<=n&&!memcmp(p+pos+8,"movi",4)))return -1;
  if(!memcmp(p+pos,"LIST",4)){
   if(pos+12>n)return 0;if(!memcmp(p+pos+8,"movi",4)){
    /* cDlnaWriter updates strh for each encoder mode, leaving avih at 66666. */
    if(out->scale||out->rate){
     if(!out->scale||!out->rate)return -1;
     uint64_t period=(1000000ull*out->scale+out->rate/2u)/out->rate;
     if(period<10000||period>200000)return -1;
     out->usec=(unsigned)period;
    }else out->usec=out->main_usec;
    if(!vids||!format||strcmp(out->codec,"MJPG")||!out->width||!out->height||out->width>720||out->height>480||out->usec<10000||out->usec>200000)return -1;
    *offset=pos+12;return 1;
   }
   if(len<4||depth>=8||(depth&&pos+8+len>ends[depth-1]))return -1;ends[depth++]=pos+8+len+(len&1);pos+=12;continue;
  }
  if(depth&&pos+8+len+(len&1)>ends[depth-1])return -1;if(pos+8+len+(len&1)>n)return 0;
  const unsigned char*d=p+pos+8;
  if(!memcmp(p+pos,"avih",4)){if(len<40||out->width)return -1;out->main_usec=rvf_le32(d);out->width=rvf_le32(d+32);out->height=rvf_le32(d+36);}
  if(!memcmp(p+pos,"strh",4)){if(len<48||memcmp(d,"vids",4)||++vids!=1)return -1;memcpy(out->codec,d+4,4);out->codec[4]=0;out->scale=rvf_le32(d+20);out->rate=rvf_le32(d+24);}
  if(!memcmp(p+pos,"strf",4)){if(len<40||!vids||++format!=1||rvf_le32(d)!=40||rvf_le32(d+4)!=out->width||rvf_le32(d+8)!=out->height||memcmp(d+16,"MJPG",4))return -1;}
  pos+=8+len+(len&1);
 }return -1;
}
/* Validate JPEG framing and SOF dimensions, including stuffed entropy bytes,
 * restart markers and multiple scans. This is validation, never decoding. */
static int rvf_jpeg(const unsigned char*p,size_t n,unsigned w,unsigned h){
 if(n<16||n>RVF_FRAME_CAP||p[0]!=255||p[1]!=216)return 0;size_t at=2;int sof=0,sos=0,entropy=0;
 while(at<n){if(entropy){while(at<n&&p[at]!=255)at++;if(at>=n)return 0;}
  if(p[at++]!=255)return 0;while(at<n&&p[at]==255)at++;if(at>=n)return 0;unsigned m=p[at++];
  if(entropy&&(m==0||(m>=208&&m<=215)))continue;if(m==217)return sof&&sos&&at==n;
  entropy=0;if(m==0||m==216||m==1||(m>=208&&m<=215)||at+2>n)return 0;size_t len=(size_t)p[at]*256+p[at+1];if(len<2||len>n-at)return 0;
  if(m==192||m==193||m==194){if(sof++||len<11||p[at+2]!=8||((unsigned)p[at+3]*256+p[at+4])!=h||((unsigned)p[at+5]*256+p[at+6])!=w)return 0;}
  if(m==218){if(!sof||len<6)return 0;sos=1;entropy=1;}at+=len;
 }return 0;
}
#endif
