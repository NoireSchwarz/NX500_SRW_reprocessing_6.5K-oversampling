/* Actual production observer and address admission, modeled read endpoints. */
#define main collector_main
#include "../src/nxl-video-probe.c"
#undef main
#include <assert.h>
struct scripted {unsigned calls,headers,groups[4],change,fail,header_change,mode_change;};
static int fake_read(void*opaque,uint32_t a,void*out,unsigned n){
 struct scripted*c=opaque;uint32_t*w=out;c->calls++;
 if(a==VP_LATENCY_DATA&&n==32){
  memset(w,0,n);w[0]=VP_LATENCY_MAGIC;w[1]=4;w[2]=90;w[3]=3;
  if(c->header_change&&c->headers)w[4]=99;
  c->headers++;return 1;
 }
 if(a==0x8146e438u&&n==4){*w=c->mode_change?0x1818:0x1717;return 1;}
 if(a==0x813ebf60u&&n==4){*w=0xe005;return 1;}
 for(unsigned g=0;g<4;g++)if(a==vpe_base[g]&&n==20){
  unsigned pass=c->groups[g]++;
  if(c->fail==g+1&&pass)return 0;
  for(unsigned i=0;i<5;i++)w[i]=g*100+i;
  w[2]=(720u<<12)|0x51u;
  if(c->change&&g*5+(c->change-1)%5==c->change-1&&pass)w[(c->change-1)%5]^=1;
  return 1;
 }
 assert(!"unexpected physical read");return 0;
}
static uint64_t fake_now(void*c){uint64_t*v=c;*v+=1000;return *v;}
static struct vp_record observe(struct scripted*c){
 struct vp_record s;memset(&s,0,sizeof s);s.valid=VP_POLICY|VP_BACKEND|VP_PROGRESS;
 s.rtos[0]=0x1717;s.rtos[2]=0xe005;s.progress[1]=0x3eu<<16;
 for(unsigned i=0;i<5;i++)s.progress[19+i]=i+1000;
 struct vp_reader r={c,fake_read,NULL,0,0};uint64_t tick=0;
 vp_ep_readback_capture(&r,&s,fake_now,&tick);return s;
}
int main(void){
 for(unsigned g=0;g<4;g++){
  assert(physical_allowed(vpe_base[g],20));
  assert(!physical_allowed(vpe_base[g]-4,20));
  assert(!physical_allowed(vpe_base[g],24));
 }
 for(unsigned a=0x20821050u;a<=0x20821060u;a+=4){assert(!physical_allowed(a,4));assert(!physical_allowed(a,20));}
 assert(!physical_allowed(0x06002060,4));assert(!physical_allowed(0x06002064,4));
 struct scripted c={0};struct vp_record s=observe(&c);
 assert(s.valid&VP_EP_READBACK);assert((s.ep_readback[1]&0xfff)==0xf0f);
 assert(s.ep_readback[31]==1&&s.ep_readback[26]==1000&&s.ep_readback[30]==1004);
 for(unsigned field=1;field<=20;field++){
  memset(&c,0,sizeof c);c.change=field;s=observe(&c);
  assert(s.ep_readback[1]&(1u<<(12+field-1)));
  assert(s.ep_readback[1]&(1u<<(4+(field-1)/5)));
 }
 for(unsigned group=1;group<=4;group++){
  memset(&c,0,sizeof c);c.fail=group;s=observe(&c);
  assert(!(s.ep_readback[1]&(1u<<(group-1))));
  for(unsigned i=0;i<5;i++)assert(s.ep_readback[6+(group-1)*5+i]==VP_UNKNOWN);
 }
 memset(&c,0,sizeof c);c.header_change=1;s=observe(&c);assert(!(s.ep_readback[1]&1024));
 memset(&c,0,sizeof c);c.mode_change=1;s=observe(&c);assert(!(s.ep_readback[1]&2048));
 memset(&c,0,sizeof c);memset(&s,0,sizeof s);s.rtos[0]=0x1717;s.rtos[2]=0xe005;
 struct vp_reader r={&c,fake_read,NULL,0,0};uint64_t tick=0;
 vp_ep_readback_capture(&r,&s,fake_now,&tick);assert(!c.calls&&!(s.valid&VP_EP_READBACK));
 puts("PASS actual EP capture: bounded ranges, 20 field races, failed reads, frame/mode changes and missing prerequisites");return 0;
}
