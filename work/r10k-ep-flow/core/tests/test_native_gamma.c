/* Read-only metadata recorder behavior, not a mock native camera pipeline. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../common/native_gamma.h"
static uint32_t manager[5],parents[2][93],heads[2][8],tails[2][78],inputs[44],widths[5];
static unsigned mode,reads,bytes,tail_reads,input_reads,root_reads,parent_reads,head_reads,base_reads;
static int load(void*ctx,uint32_t at,void*out,unsigned n){
 (void)ctx;reads++;bytes+=n;assert(n&&!(at&3)&&!(n&3)&&n<=4096);
 uint32_t roots[4]={0x80592640,3,0x8072dd80,0};const uint32_t ra[4]={0x8153e6d4,0x8153e6d4+3364,0x81543d40,0x8153e6d4+3428};
 for(unsigned i=0;i<4;i++)if(at==ra[i]&&n==4){if(mode==9&&++root_reads>4)roots[i]^=1;memcpy(out,roots+i,4);return 1;}
 if(at==GC_MANAGER&&n==20){memcpy(out,manager,20);return 1;}
 if(at==0x813ebf50&&n==8){uint32_t v[2]={0xbb71b300,0xbb71d300};if(mode==15||(mode==16&&++base_reads==2))v[1]+=4096;memcpy(out,v,8);return 1;}
 if(at==0x82200000&&n==20){memcpy(out,widths,20);return 1;}
 if(at==0x82300000&&n==sizeof inputs){memcpy(out,inputs,n);if(mode==8&&++input_reads==2)((uint32_t*)out)[3]^=1;return 1;}
 for(unsigned k=0;k<2;k++){
  uint32_t parent=0x82000000+k*4096,gamma=0x82100000+k*8192;
  if(at>=parent&&(uint64_t)at+n<=parent+sizeof parents[k]){memcpy(out,(unsigned char*)parents[k]+at-parent,n);if(mode==10&&at==parent+0x170&&++parent_reads==2)((uint32_t*)out)[0]^=4;return 1;}
  if(at==gamma&&n==32){memcpy(out,heads[k],n);if(mode==11&&++head_reads==2)((uint32_t*)out)[1]^=32;return 1;}
  if(at==gamma+0xe3c&&n==312){if(mode==6)return 0;memcpy(out,tails[k],n);if(mode==7&&++tail_reads==2)((uint32_t*)out)[4]^=1;return 1;}
 }assert(!"Unexpected metadata pointer traversal");return 0;
}
static void setup(unsigned m){
 mode=m;reads=bytes=tail_reads=input_reads=root_reads=parent_reads=head_reads=base_reads=0;
 memset(manager,0,sizeof manager);memset(parents,0,sizeof parents);memset(heads,0,sizeof heads);memset(tails,0,sizeof tails);memset(inputs,0,sizeof inputs);
 manager[0]=GC_MANAGER_VPTR;manager[1]=2;manager[2]=0x82000000;manager[3]=0x82001000;
 const uint32_t w[5]={3,5,4,13,2};memcpy(widths,w,20);
 for(unsigned k=0;k<2;k++){
  uint32_t*p=parents[k],*h=heads[k];p[0]=GC_PARENT_VPTR;p[3]=k<<8;p[92]=0x82100000+k*8192;
  h[0]=GC_GAMMA_VPTR;h[1]=5u<<29;h[2]=0x82300000;h[3]=manager[k+2];h[6]=0x82200000;h[7]=5;
  tails[k][0]=h[1];for(unsigned i=0;i<19;i++){tails[k][i+1]=i*900;tails[k][i+20]=i*50;tails[k][i+40]=i*900;tails[k][i+59]=i*50;}
 }inputs[0]=2;
 if(m==1)manager[0]^=4;
 if(m==2)parents[0][92]=0;
 if(m==3)parents[0][92]=0x20810000;
 if(m==4)heads[0][0]^=4;
 if(m==5)widths[3]=12;
 if(m==12)heads[0][2]=0xbb71c000;
 if(m==13)heads[0][2]=0;
 if(m==14)parents[0][3]=0x100;
}
int main(void){
 const unsigned expected[17]={1,0,2,5,2,6,3,4,4,0,4,4,5,1,2,0,0};
 for(unsigned m=0;m<17;m++){
  struct gc_sample s;setup(m);(void)gc_capture(load,NULL,&s);
  assert(s.magic==GC_MAGIC&&s.schema==1&&s.bytes==sizeof s&&s.checksum==ic_hash(&s,sizeof s-8)&&s.commit==(GC_MAGIC^s.checksum));
  if(m==1)assert(s.status==2&&reads==5);else if(m==15)assert(s.status==6);else if(m==9||m==16)assert(s.status==4);else assert(s.channel[0].status==expected[m]);
  assert(bytes<4096&&reads<48);
  if(!m){FILE*f=fopen("/native-gamma.bin","wb");assert(f&&fwrite(&s,1,sizeof s,f)==sizeof s&&!fclose(f));assert(s.channel[1].status==1&&s.channel[1].mask==15);}
  if(m==13)assert(s.channel[0].mask==7);
  printf("case=%u global=%u ch0=%u bytes=%u reads=%u\n",m,s.status,s.channel[0].status,bytes,reads);
 }puts("PASS17 passive Gamma guards, repeated data, null context and bounded reads");return 0;
}
