/* SPDX-License-Identifier: AGPL-3.0-only. Narrow, data-only RTOS command protocol. */
#ifndef NXL_RENDER_BRIDGE_CORE_H
#define NXL_RENDER_BRIDGE_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define P33_PACKET_START 2048u
#define P33_PACKET_SLOT 128u
#define P33_MAX_PACKETS 12u
#define P33_CAPTURE_BYTES 65536u
enum p33_command {P33_SEQ_STATUS,P33_MON_STATUS,P33_MON_START,P33_MON_CLEAN,P33_MON_SHOW,P33_MON_STOP};
static const char *p33_command_text(enum p33_command c){
 static const char *s[]={"st cap seq status ","st cap back param status ","st cap back param run still ","st cap back param clean still ","st cap back param show still ipc ","st cap back param stop still "};
 return (unsigned)c<sizeof s/sizeof *s?s[c]:NULL;
}
static int p33_command_allowed(const char *text){
 if(!text)return 0;for(unsigned i=0;i<2;i++)if(!strcmp(text,p33_command_text((enum p33_command)i)))return 1;return 0;
}
static int p33_packet_extent(unsigned slot,unsigned length,unsigned *offset){
 if(slot>=P33_MAX_PACKETS||!length||length>=P33_PACKET_SLOT)return 0;
 unsigned p=P33_PACKET_START+slot*P33_PACKET_SLOT;
 if(p+length+1>16384)return 0;*offset=p;return 1;
}
static int p33_ring_span(uint32_t before,uint32_t after,uint32_t capacity,uint32_t *first,uint32_t *second){
 if(capacity!=0xbfff8u||before>=capacity||after>=capacity)return 0;
 *first=after>=before?after-before:capacity-before;*second=after>=before?0:after;
 return (uint64_t)*first+*second<=P33_CAPTURE_BYTES;
}
static int p33_normalize_console(const unsigned char *raw,size_t n,char *out,size_t capacity,size_t *length){
 if(!length)return 0;*length=0;if(!out||!capacity||(!raw&&n))return 0;out[0]=0;
 size_t used=0;for(size_t i=0;i<n;i++){
  unsigned char c=raw[i];if(!c)continue;
  if(c=='\r'){size_t next=i+1;while(next<n&&!raw[next])next++;if(next<n&&raw[next]=='\n')continue;c='\n';}
  if(used+1>=capacity){out[0]=0;return 0;}out[used++]=(char)c;
 }out[used]=0;*length=used;return 1;
}
static int p33_monitor_state(const char *text){
 if(!text)return -1;int found=-1;
 for(const char *line=text;*line;){
  const char *end=strchr(line,'\n');if(!end)end=line+strlen(line);
  const char *p=line;while(p<end&&(*p==' '||*p=='\t'))p++;
  static const char label[]="1.Still Status";
  if((size_t)(end-p)>=sizeof label-1&&!memcmp(p,label,sizeof label-1)){
   p+=sizeof label-1;while(p<end&&(*p==' '||*p=='\t'))p++;if(p==end||*p!='=')return -1;p++;
   while(p<end&&(*p==' '||*p=='\t'))p++;const char *last=end;while(last>p&&(last[-1]==' '||last[-1]=='\t'||last[-1]=='\r'))last--;
   size_t n=(size_t)(last-p);if(!n||n>8)return -1;char word[9];
   for(size_t i=0;i<n;i++)word[i]=p[i]>='a'&&p[i]<='z'?(char)(p[i]-'a'+'A'):p[i];word[n]=0;
   int state=(!strcmp(word,"STOP")||!strcmp(word,"DEACTIVE"))?0:(!strcmp(word,"RUN")||!strcmp(word,"ACTIVE"))?1:-1;
   if(state<0||(found>=0&&found!=state))return -1;found=state;
  }
  if(!*end)break;line=end+1;
 }return found;
}
#endif
