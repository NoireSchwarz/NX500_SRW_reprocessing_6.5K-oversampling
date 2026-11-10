/* Original NX500 sensor window-program cache, ordinary RAM only.
 * The native setter consumes these fields through 0x80164d04. This snapshot
 * is not proof that sensor writes or physical pixel readout have completed.
 */
#ifndef NX_VIDEO_GEOMETRY_OBSERVATION_H
#define NX_VIDEO_GEOMETRY_OBSERVATION_H
#include "video_sensor_observation.h"
#define VG_MAGIC 0x3147564eu
#define VG_OFFSET 0x1e0u
#define VG_WORDS 22u
struct vg_sample {uint32_t magic,schema,status,mask,address,vtable_before,vtable_after,modes_before,modes_after,offset,words;uint32_t window[VG_WORDS],checksum,commit;};
static int vg_capture(vf_reader read,void*ctx,struct vg_sample*s){
 uint32_t again[VG_WORDS];memset(s,0,sizeof *s);s->magic=VG_MAGIC;s->schema=1;s->address=VS_OBJECT;s->offset=VG_OFFSET;s->words=VG_WORDS;
 if(!read(ctx,VS_OBJECT,&s->vtable_before,4)||!read(ctx,VS_OBJECT+8,&s->modes_before,4)){s->status=3;goto done;}if(s->vtable_before!=VS_VTABLE){s->status=2;goto done;}
 if(!read(ctx,VS_OBJECT+VG_OFFSET,s->window,sizeof s->window)){s->status=3;goto done;}s->mask|=1;
 if(!read(ctx,VS_OBJECT+VG_OFFSET,again,sizeof again)||!read(ctx,VS_OBJECT,&s->vtable_after,4)||!read(ctx,VS_OBJECT+8,&s->modes_after,4)){s->status=3;goto done;}s->mask|=2;
 if(s->vtable_after!=VS_VTABLE||s->modes_before!=s->modes_after||memcmp(again,s->window,sizeof again)){s->status=4;goto done;}s->status=1;
done:s->checksum=vf_hash(s,sizeof *s-8);s->commit=VG_MAGIC^s->status^s->checksum;return s->status==1;
}
static int vg_valid(const struct vg_sample*s){return s->magic==VG_MAGIC&&s->schema==1&&s->status>=1&&s->status<=4&&s->mask<=3&&s->address==VS_OBJECT&&s->offset==VG_OFFSET&&s->words==VG_WORDS&&s->checksum==vf_hash(s,sizeof *s-8)&&s->commit==(VG_MAGIC^s->status^s->checksum);}
_Static_assert(sizeof(struct vg_sample)==140,"fixed sensor window observation");
#endif
