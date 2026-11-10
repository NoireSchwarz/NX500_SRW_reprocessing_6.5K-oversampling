#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../common/video_clock_capture.h"
static unsigned clock_read,unstable,unavailable;
static int phys(void*c,uint32_t at,void*out,unsigned bytes){
 (void)c;if(bytes!=4)return 0;uint32_t value;
 if(at==0x8146e438u)value=0x1717u;
 else if(at==0x813f1b30u)value=0x1908u;
 else {unsigned i;for(i=0;i<9&&vp_clock_address[i]!=at;i++){}if(i==9||unavailable)return 0;value=0x100u+i;if(unstable&&++clock_read==10)value^=1u;}
 memcpy(out,&value,4);return 1;
}
static int proc(void*c,uint32_t a,void*b,unsigned n){(void)c;(void)a;(void)b;(void)n;return 0;}
int main(void){
 struct vp_reader reader={NULL,phys,proc,0,0};struct vp_clock_ring ring;vp_clock_init(&ring);
 struct vp_record s;memset(&s,0,sizeof s);struct vp_recording_watch watch={0};
 assert(!vp_recording_active(&watch,&s));s.valid=VP_SOURCE;s.source[0]=0x123400;s.source[2]=0;assert(!vp_recording_active(&watch,&s));
 s.source[2]=25;assert(vp_recording_active(&watch,&s)&&watch.recorded);assert(!vp_recording_active(&watch,&s)&&watch.recorded);
 s.source[0]+=4;s.source[2]=50;assert(!vp_recording_active(&watch,&s));s.source[2]=70;assert(vp_recording_active(&watch,&s));
 s.valid|=VP_CHANGED;assert(!vp_recording_active(&watch,&s));s.valid=VP_SOURCE;assert(!vp_recording_active(&watch,&s));
 vp_clock_capture(&reader,&ring,1234,700000000u,1u);assert(ring.total==1&&ring.record[0].sequence==0&&ring.record[0].elapsed_ms==1234&&ring.record[0].status==1&&ring.record[0].scene==0x1908u);
 for(unsigned i=0;i<9;i++)assert(ring.record[0].clock[i]==0x100u+i);
 assert(ring.record[0].commit==(vp_hash(&ring.record[0],offsetof(struct vp_clock_record,commit))^VP_CLOCK_XOR));
 unstable=1;clock_read=0;vp_clock_capture(&reader,&ring,2234,700000000u,1u);assert(ring.record[1].status==2);
 unstable=0;unavailable=1;vp_clock_capture(&reader,&ring,3234,700000000u,1u);assert(ring.record[2].status==3);
 unavailable=0;vp_clock_capture(&reader,&ring,4234,700000000u,1u);assert(ring.total==4&&ring.record[0].sequence==3&&ring.record[0].status==1);
 puts("PASS recording-watch and native selector snapshots: stable, changed, unavailable, checksummed wrap.");
 return 0;
}
