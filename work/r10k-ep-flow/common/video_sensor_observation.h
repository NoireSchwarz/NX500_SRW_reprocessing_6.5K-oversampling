/* NX500 1.12 CSensorDriverNx control cache; no sensor register access.
 * Original constructor/getter establish this fixed object and vtable.
 * Requested/applied mode bytes and geometry candidates remain RAM shadows.
 */
#ifndef NX_VIDEO_SENSOR_OBSERVATION_H
#define NX_VIDEO_SENSOR_OBSERVATION_H
#include "video_front_observation.h"
#define VS_MAGIC 0x3153564eu
#define VS_OBJECT 0x8146e430u
#define VS_VTABLE 0x80587f08u
#define VS_WORDS 28u
struct vs_sample {uint32_t magic,schema,status,mask,address,vtable_before,vtable_after;uint32_t control[VS_WORDS],checksum,commit;};
static int vs_capture(vf_reader read,void*ctx,struct vs_sample*s){
 uint32_t again[VS_WORDS];memset(s,0,sizeof *s);s->magic=VS_MAGIC;s->schema=1;s->address=VS_OBJECT;
 if(!read(ctx,VS_OBJECT,&s->vtable_before,4)){s->status=3;goto finish;}
 if(s->vtable_before!=VS_VTABLE){s->status=2;goto finish;}
 if(!read(ctx,VS_OBJECT,s->control,sizeof s->control)){s->status=3;goto finish;}s->mask|=1;
 if(!read(ctx,VS_OBJECT,again,sizeof again)||!read(ctx,VS_OBJECT,&s->vtable_after,4)){s->status=3;goto finish;}s->mask|=2;
 if(s->vtable_after!=VS_VTABLE||s->control[0]!=VS_VTABLE||memcmp(s->control,again,sizeof again)){s->status=4;goto finish;}s->status=1;
finish:s->checksum=vf_hash(s,sizeof *s-8);s->commit=VS_MAGIC^s->status^s->checksum;return s->status==1;
}
static int vs_valid(const struct vs_sample*s){return s->magic==VS_MAGIC&&s->schema==1&&s->status>=1&&s->status<=4&&s->mask<=3&&s->address==VS_OBJECT&&s->checksum==vf_hash(s,sizeof *s-8)&&s->commit==(VS_MAGIC^s->status^s->checksum);}
_Static_assert(sizeof(struct vs_sample)==148,"bounded sensor control observation");
#endif
