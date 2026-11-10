#include <stdio.h>
#include <assert.h>
#include "../../common/video_counter_capture.h"
#include "../../common/video_latency_capture.h"
#include "../../common/video_progress_capture.h"
static unsigned kind,pass,headers,debug_reads;
static int read_native(void*c,uint32_t a,void*out,unsigned n){
 (void)c;uint32_t value=0;
 assert(a!=0x20810440u&&!(a>=0x20821050u&&a<=0x20821060u));
 assert(!(a>=0x06002060u&&a<0x06002068u));
 if(a==VP_LATENCY_DATA){assert(n==32);uint32_t h[8]={VP_LATENCY_MAGIC,3,42,3u|(1u<<16),100,200,0,0};if(kind==2u&&headers)h[2]++;memcpy(out,h,32);headers++;return 1;}
 assert(n==4u);
 if(a==0x208210b0u)value=kind==3u?0u:kind==4u&&headers>1u?0u:2u;
 else{
  unsigned i;for(i=0;i<8;i++)if(vpg_address[i]==a)break;assert(i<8u);
  if(i==0u)pass++;
  if(kind==5u&&i==2u)return 0;
  if(i==7u)debug_reads++;
  value=(i+1u)*0x10u+(kind==1u?pass:0u);
 }
 memcpy(out,&value,4);return 1;
}
static uint64_t now(void*c){(void)c;static uint64_t ticks;ticks+=1000;return ticks;}
int main(void){
 FILE*f=fopen("/progress.bin","wb");assert(f);
 for(kind=0;kind<6;kind++){
  pass=headers=debug_reads=0;struct vp_record r;memset(&r,0,sizeof r);r.sequence=kind;r.valid=VP_POLICY|VP_BACKEND;r.rtos[0]=0x1717;r.rtos[2]=0xe005;
  struct vp_reader reader={NULL,read_native,read_native,0,0};vp_progress_capture(&reader,&r,now,NULL);
  assert(r.valid&VP_PROGRESS);assert((r.progress[1]&1u)==(kind==2u?0u:1u));
  if(kind==3u||kind==4u){assert(!(r.progress[1]&4u)&&r.progress[17]==VP_UNKNOWN&&r.progress[25]==VP_UNKNOWN);}
  if(kind==3u)assert(debug_reads==0);
  if(kind==4u)assert(debug_reads==2);
  if(kind==5u)assert(!(r.progress[1]&(1u<<10))&&!(r.progress[1]&(1u<<18)));
  assert(fwrite(&r,sizeof r,1,f)==1);
 }
 return fclose(f)!=0;
}
