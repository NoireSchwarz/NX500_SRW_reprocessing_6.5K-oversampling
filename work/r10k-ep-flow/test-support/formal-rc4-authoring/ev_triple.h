/* SPDX-License-Identifier: AGPL-3.0-only. Pure timing recognizer; no key injection. */
#ifndef NXL_EV_TRIPLE_H
#define NXL_EV_TRIPLE_H
#define NXL_EV_GAP_MS 420UL
#define NXL_EV_HOLD_MS 400UL
struct nxl_ev_taps {unsigned count;int held,cancelled;unsigned long pressed,released;};
static unsigned nxl_ev_expire(struct nxl_ev_taps*s,unsigned long now){
 if(!s->held&&s->count&&now-s->released>NXL_EV_GAP_MS){unsigned n=s->count;s->count=0;return n;}return 0;
}
static int nxl_ev_feed(struct nxl_ev_taps*s,unsigned code,int value,unsigned long now){
 if(code!=173){if(value==1){s->count=0;if(s->held)s->cancelled=1;}return 0;}
 if(value==2)return 0;
 if(value==1){if(s->held)return 0;if(s->count&&now-s->released>NXL_EV_GAP_MS)s->count=0;s->held=1;s->cancelled=0;s->pressed=now;return 0;}
 if(value!=0||!s->held)return 0;
 s->held=0;if(s->cancelled||now-s->pressed>NXL_EV_HOLD_MS){s->count=0;s->cancelled=0;return 0;}
 s->released=now;if(++s->count==3){s->count=0;return 1;}return 0;
}
#endif
