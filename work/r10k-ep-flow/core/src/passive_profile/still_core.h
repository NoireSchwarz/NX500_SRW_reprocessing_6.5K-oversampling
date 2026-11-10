/* SPDX-License-Identifier: AGPL-3.0-only. Bounded metadata reads, never image planes. */
#ifndef NXL_STILL_CORE_H
#define NXL_STILL_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define P34_TASK 0x8153e6d4u
#define P34_OWNER 0x81543d40u
#define P34_WORDS 107u
#define P34_DURATION_MS 60000u
#define P34_INTERVAL_US 2000u
struct p34_snapshot {uint32_t w[P34_WORDS];};
struct p34_header {char magic[8];uint32_t version,header_bytes,record_bytes,words,duration_ms,crc;};
struct p34_record {uint32_t us,sequence,flags;struct p34_snapshot data;uint32_t crc;};
typedef int (*p34_read_fn)(void*,uint32_t,uint32_t*,unsigned);
static int p34_ram_range(uint32_t p,unsigned bytes){return !(p&3u)&&p>=0x81000000u&&(uint64_t)p+bytes<=0x94000000ULL;}
/* Header state: 0=null, 1=verified CCapImage header, 2=out-of-range,
 * 3=unsupported vptr (only that word read), 4=inactive task (not followed). */
static int p34_image(p34_read_fn rd,void *ctx,uint32_t p,int active,uint32_t *state,uint32_t *out){
 *state=0;if(!active){*state=4;return 1;}if(!p)return 1;
 if(!p34_ram_range(p,128)){*state=2;return 1;}
 if(!rd(ctx,p,out,1))return 0;if(out[0]!=0x80588910u){*state=3;return 1;}
 if(!rd(ctx,p,out,32))return 0;if(out[0]!=0x80588910u)return 1;*state=1;return 1;
}
static int p34_capture(p34_read_fn rd,void *ctx,struct p34_snapshot *s){
 memset(s,0,sizeof *s);uint32_t *w=s->w;
 if(!rd(ctx,P34_TASK,w,1))return -1;
 if(!w[0])return 1; /* Lazy singleton: wait for the first normal photo. */
 if(w[0]!=0x80592640u)return 0; /* Constructor in progress / unknown class. */
 if(!rd(ctx,P34_TASK+3364,w+1,1)||!rd(ctx,0x81540f38u,w+2,1)||!rd(ctx,0x81089a94u,w+3,1)||
 !rd(ctx,P34_TASK+3428,w+4,3)||!rd(ctx,P34_TASK+3284,w+7,2)||!rd(ctx,P34_TASK+1604,w+9,1)||
 !rd(ctx,P34_TASK+1684,w+10,1)||!rd(ctx,P34_TASK+3204,w+11,2)||!rd(ctx,P34_TASK+3388,w+13,6)||
 !rd(ctx,0x8153cbdcu,w+19,3)||!rd(ctx,P34_OWNER,w+22,3))return -1;
 if(w[22]&&p34_ram_range(w[22],56)){if(!rd(ctx,w[22],w+26,14))return -1;w[25]=1;}
 else w[25]=w[22]?2:0;
 unsigned state=w[1]&255u;int active=state==2||state==3;
 if(!p34_image(rd,ctx,w[15],active,w+40,w+41)||!p34_image(rd,ctx,w[17],active,w+73,w+74))return -1;
 if(w[6]&&p34_ram_range(w[6],4)&&!rd(ctx,w[6],w+106,1))return -1;
 return 1;
}
static int p34_stable(p34_read_fn rd,void *ctx,struct p34_snapshot *s){
 struct p34_snapshot a,b;int x=p34_capture(rd,ctx,&a),y=p34_capture(rd,ctx,&b);if(x<0||y<0)return -1;if(!x||!y)return 0;
 if(memcmp(&a,&b,sizeof a))return 0;*s=b;return 1;
}
static int p34_native_active(const struct p34_snapshot *s){const uint32_t *w=s->w;unsigned st=w[1]&255u;
 return (st==2||st==3)&&w[13]==0x04000001u&&w[6]==w[4]&&w[106]==0x805926c8u&&w[25]==1&&(w[26]&255u)==1;
}
static uint32_t p34_crc(const void *data,size_t n){const unsigned char *p=data;uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;}
#endif
