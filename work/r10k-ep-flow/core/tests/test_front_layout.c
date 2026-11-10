#include <assert.h>
#include <stdio.h>
#include "handoff_core.h"
#include "alloc_core.h"
#include "rtos_front_blob.h"
#include "srw_iq.h"
static unsigned char buffer[P39_PROOF_BYTES];
int main(int argc,char **argv){
 assert(argc==4);assert(ALLOC_BYTES==P39_CAP_BYTES&&ALLOC_SPAN_BLOCKS==8192);
 uint32_t pa=0xa1c00000,nonce=0x12345678;
 for(unsigned i=0;i<sizeof buffer;i++)buffer[i]=(unsigned char)((i*37u+(i>>5)+0x53u)^(nonce>>((i&3)*8)));
 memcpy(buffer,p39_hook_code,sizeof p39_hook_code);p39_images(buffer,pa,nonce,P39_IRQ_OFFSET);
 FILE *iqf=fopen(argv[3],"rb");uint32_t iq[256];assert(iqf&&p41_srw_info(iqf,iq)&&!fclose(iqf));iq[2]=nonce;iq[63]=p41_checksum(iq,63);memcpy(buffer+P41_OFFSET,iq,sizeof iq);
 assert(p39_extent(pa)&&!p39_extent(0xbfffc000));
 assert(((uint32_t*)(buffer+0x5100))[1]==6608&&((uint32_t*)(buffer+0x5100))[18]==13216);
 assert(((uint32_t*)(buffer+0x5200))[7]==464&&((uint32_t*)(buffer+0x5200))[8]==1472);
 assert(((uint32_t*)(buffer+0x5200))[18]==6496&&buffer[0x5200+60]==2&&buffer[0x5200+64]==1);
 FILE *f=fopen(argv[2],"rb");assert(f&&fread(buffer+P39_SOURCE_OFFSET,1,P39_WINDOW_BYTES-P39_SOURCE_OFFSET,f)==P39_WINDOW_BYTES-P39_SOURCE_OFFSET&&!fclose(f));
 memset(buffer+P39_TARGET_PROOF_OFFSET,0xa5,P39_WINDOW_BYTES);
 const char *cmd="st cap back param status ";for(unsigned slot=0;slot<6;slot++){unsigned off=0x7000+slot*128;
  if(slot==0||slot==2||slot==5)memcpy(buffer+off,cmd,strlen(cmd)+1);
  else{uint32_t payload[3]={nonce,pa+P39_SOURCE_OFFSET,P39_SOURCE_BYTES};if(slot!=4){payload[0]^=slot==1?0x37a10001u:0x37a20002u;payload[1]=P39_SLOT_PAGE;payload[2]=4096;}memcpy(buffer+off,payload,12);}}
 f=fopen(argv[1],"wb");assert(f&&fwrite(buffer,1,sizeof buffer,f)==sizeof buffer&&!fclose(f));
 puts("PASS full real-frame descriptor/extent:6480x4320,14x3 native grid, independent Y/UV planes");return 0;
}
