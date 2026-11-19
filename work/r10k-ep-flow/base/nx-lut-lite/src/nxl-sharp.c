/* SPDX-License-Identifier: AGPL-3.0-only.
 * Fixed stock-P7 sharpness route from metol pwsend.c, external0x111.
 * A2-only hue/RGB channels are deliberately absent. This updates a spatial
 * ISP parameter, never pretends that a per-pixel LUT sharpens an image.
 * Library hashes and live readback are enforced by lut.sh around this call.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
extern int mcb_init(void);
extern int mcb_set_ignore_init_checking(void);
extern int SetVariableDataMCB(unsigned short,void*,unsigned short);
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXSHARP1 stock external0x111 only; one send; caller verifies live ISP readback");return 0;}
 if(argc!=2||strlen(argv[1])!=8)return 2;char*end=0;unsigned long n=strtoul(argv[1],&end,16);if(!end||*end||n>0xffffffffUL)return 2;
 uint32_t value=(uint32_t)n;int raw=(int16_t)(value>>16);
 if((value&65535)!=0xd80a||raw< -145||raw>175||(raw-15)%16)return 2;
 if(mcb_init())return 3;
 /* Original metol client initialization: this flag belongs to this fresh
  * Linux client, not camera resource/RTOS readiness. Sending is not evidence
  * of consumption; lut.sh compares the actual runtime variable afterward. */
 mcb_set_ignore_init_checking();
 int rc=SetVariableDataMCB(0x111,&value,4);printf("NXSHARP1 submitted=%08x returned=%d\n",value,rc);return rc?4:0;
}
