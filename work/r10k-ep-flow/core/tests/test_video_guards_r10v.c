#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "../../common/video_fullwidth_contract.h"
#include "../src/video_fullwidth_guards.h"
static int image;static unsigned variant;
static int physical_read(void*context,uint32_t at,void*out,unsigned bytes){
 (void)context;
 if(!bytes||at<0x80000000u||(uint64_t)at+bytes>0x80600000ULL)return 0;
 if(variant==7&&at==0x80036be0u)return 0;
 if(pread(image,out,bytes,(off_t)(at-0x80000000u))!=(ssize_t)bytes)return 0;
 if(variant==1||variant>=4)for(unsigned i=0;i<VFW_BLOCK_COUNT;i++){
  const struct vfw_block*b=vfw_blocks+i;
  uint32_t lo=at>b->address?at:b->address,hi=at+bytes<b->address+b->bytes?at+bytes:b->address+b->bytes;
  if(lo<hi)memcpy((unsigned char*)out+lo-at,(const unsigned char*)vfw_full_code(b)+lo-b->address,hi-lo);
 }
 uint32_t corrupt=variant==2||variant==4?0x80036be0u:variant==3||variant==5?0x80036c70u:variant==6?0x80036c00u:variant==8?0x804e1fbcu:0u;
 if(corrupt&&at<=corrupt&&(uint64_t)at+bytes>corrupt)((unsigned char*)out)[corrupt-at]^=1;
 return 1;
}
static int code_guards(void){
 int replacement=vfw_block_state(physical_read,NULL);if(replacement<0)return 0;
 unsigned char b[2048];for(unsigned i=0;i<sizeof vfw_code_guards/sizeof*vfw_code_guards;i++){
  const struct vfw_code_guard*g=vfw_code_guards+i;if(g->bytes>sizeof b||!physical_read(NULL,g->address,b,g->bytes))return 0;
  if(g->address<=VFW_MAP_WORD&&(uint64_t)g->address+g->bytes>VFW_MAP_WORD){uint32_t w;memcpy(&w,b+VFW_MAP_WORD-g->address,4);if(w!=VFW_MAP_CROP&&w!=VFW_MAP_FULL)return 0;w=VFW_MAP_CROP;memcpy(b+VFW_MAP_WORD-g->address,&w,4);}
  if(g->address<=VFW_BACKEND_WORD&&(uint64_t)g->address+g->bytes>VFW_BACKEND_WORD){uint32_t w;memcpy(&w,b+VFW_BACKEND_WORD-g->address,4);if(w!=VFW_BACKEND_UD&&w!=VFW_BACKEND_SCALE)return 0;w=VFW_BACKEND_UD;memcpy(b+VFW_BACKEND_WORD-g->address,&w,4);}
  if(g->address<=VFW_LDC_POLICY_WORD&&(uint64_t)g->address+g->bytes>VFW_LDC_POLICY_WORD){uint32_t w;memcpy(&w,b+VFW_LDC_POLICY_WORD-g->address,4);if(w!=VFW_LDC_POLICY_UD&&w!=VFW_LDC_POLICY_SCALE)return 0;w=VFW_LDC_POLICY_UD;memcpy(b+VFW_LDC_POLICY_WORD-g->address,&w,4);}
  const uint32_t at[2]={VFW_LCAC_MASK_WORD,VFW_LCAC_OR_WORD},stock[2]={VFW_LCAC_MASK_STOCK,VFW_LCAC_OR_STOCK},full[2]={VFW_LCAC_MASK_FULL,VFW_LCAC_OR_FULL};for(unsigned k=0;k<2;k++)if(g->address<=at[k]&&(uint64_t)g->address+g->bytes>at[k]){uint32_t w;memcpy(&w,b+at[k]-g->address,4);if(w!=stock[k]&&w!=full[k])return 0;memcpy(b+at[k]-g->address,stock+k,4);}
  for(unsigned j=0;j<VFW_BLOCK_COUNT;j++){const struct vfw_block*x=vfw_blocks+j;uint32_t lo=g->address>x->address?g->address:x->address,hi=g->address+g->bytes<x->address+x->bytes?g->address+g->bytes:x->address+x->bytes;if(lo<hi)memcpy(b+lo-g->address,(const unsigned char*)vfw_stock(x)+lo-x->address,hi-lo);}
  if(vfw_hash(b,g->bytes)!=g->hash)return 0;
 }return 1;
}

int main(void){
 image=open("/P7.bin",O_RDONLY);if(image<0)return 30;
 for(variant=0;variant<9;variant++){
  int valid=code_guards(),expected=variant<2;
  printf("guard-case-%u result=%d expected=%d\n",variant,valid,expected);
  if(valid!=expected)return 40+variant;
 }
 close(image);puts("PASS9 exact production Linux code guards; stock/full and corruption/read failure");return 0;
}
