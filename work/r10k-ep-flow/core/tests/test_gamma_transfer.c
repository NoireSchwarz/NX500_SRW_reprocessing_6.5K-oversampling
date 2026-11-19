#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/gamma_contract.h"
static unsigned checks;
static void seal(uint32_t*q){q[63]=p48_batch_crc(q,252);}
static void make(uint32_t*q,unsigned mode){memset(q,0,2048);q[0]=GT_MAGIC;q[1]=mode>=GT_LINEAR?2:1;q[2]=123;q[3]=mode;q[4]=456;q[5]=q[6]=1;q[7]=789;
 for(unsigned i=0;i<19;i++){q[16+i]=gt_profile_x(mode,i);q[35+i]=mode>=GT_LINEAR?gt_transfer_y(mode,i):(gt_transfer_x[i]*1023u+8191u)/16383u;}seal(q);}
static void expect(uint32_t*q,int wanted){assert(gt_request_valid(q,123,456)==wanted);checks++;}
int main(void){uint32_t q[512],saved;
 for(unsigned mode=0;mode<=GT_MODE_MAX;mode++){make(q,mode);expect(q,1);q[1]^=3;seal(q);expect(q,0);make(q,mode);q[2]++;seal(q);expect(q,0);make(q,mode);q[4]++;seal(q);expect(q,0);make(q,mode);q[63]^=1;expect(q,0);}
 for(unsigned mode=3;mode<=GT_MODE_MAX;mode++){
  make(q,mode);assert(q[35]==gt_transfer_y(mode,0)&&q[53]==gt_transfer_y(mode,18));
  for(unsigned i=16;i<54;i++){saved=q[i];q[i]^=1;seal(q);expect(q,0);q[i]=saved;seal(q);}expect(q,1);
  q[GT_READY]=q[GT_CALLED]=q[GT_STATUS]=q[GT_TARGET_MATCH]=q[GT_RESTORE_CALLED]=q[GT_RESTORED]=q[GT_BANK0_SAME]=q[GT_BEFORE_LOAD]=q[GT_CONFIG_RETURN]=q[GT_REFRESH_CALLED]=q[GT_REFRESH_RETURN]=q[GT_FINAL_LOAD_CALLED]=1;q[GT_CONFIG_LOADS]=2;assert(gt_audit_valid(q,123,456));checks++;
  const unsigned fields[]={GT_RESTORED,GT_BANK0_SAME,GT_TARGET_MATCH,GT_REFRESH_CALLED};for(unsigned i=0;i<4;i++){saved=q[fields[i]];q[fields[i]]=0;assert(!gt_audit_valid(q,123,456));checks++;q[fields[i]]=saved;}
  q[GT_RESTORE_STATUS]=1;assert(!gt_audit_valid(q,123,456));checks++;q[GT_RESTORE_STATUS]=0;
  q[GT_AFTER_START]=q[GT_AFTER_COMPLETE]=0;assert(gt_audit_valid(q,123,456));checks++; /* no false hardware adoption claim */
 }
 make(q,GT_MODE_MAX+1u);expect(q,0);printf("PASS %u exact-profile/schema/nonce/CRC/restore/bank guards; no ISP simulation\n",checks);return 0;}
