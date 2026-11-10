/* SPDX-License-Identifier: AGPL-3.0-only. Legacy metadata layout now used for passive bookmarks.
 * R10j never writes the legacy bridge candidate; no extra RTOS memory. */
#ifndef NXL_VIDEO_COMPARE_STATE_H
#define NXL_VIDEO_COMPARE_STATE_H
#define VP_COMPARE_OFFSET 3968u
#define VP_COMPARE_XOR 0x73a19c46u
enum {VPC_BASELINE=1,VPC_PERFORMANCE=2,VPC_UNAVAILABLE=3,VPC_RESTORE_NEEDED=4,VPC_EXTERNAL=5};
#define VPC_BRIDGE 0x2012f138u
#define VPC_CANDIDATE 0x14au
enum {VPC_OK=1,VPC_NO_GOVERNOR=2,VPC_BUSY=3,VPC_WRITE_FAILED=4,VPC_VERIFY_FAILED=5,
 VPC_EXTERNAL_CHANGE=6,VPC_RESTORE_FAILED=7,VPC_ALREADY_PERFORMANCE=8};
struct vp_compare {
 char magic[8];uint32_t schema,bytes,sequence,phase,original,current,owned,error,request,result,changes,restores;
 uint64_t changed_ns;uint32_t event[3][5],commit;
};
static int vpc_valid(const struct vp_compare*p){return !memcmp(p->magic,"NXLQOSAB",8)&&p->schema==1u&&p->bytes==sizeof*p&&p->phase>=1u&&p->phase<=5u&&p->commit==(vp_hash(p,offsetof(struct vp_compare,commit))^VP_COMPARE_XOR);}
static void vpc_publish(struct vp_compare*out,struct vp_compare*p){
 p->commit=vp_hash(p,offsetof(struct vp_compare,commit))^VP_COMPARE_XOR;
 out->commit=0;__sync_synchronize();memcpy(out,p,offsetof(struct vp_compare,commit));__sync_synchronize();out->commit=p->commit;__sync_synchronize();
}
_Static_assert(sizeof(struct vp_compare)==128,"bounded bridge comparison metadata");
_Static_assert(VP_COMPARE_OFFSET>=VP_BUS_OFFSET+sizeof(struct vp_bus_ring)&&VP_COMPARE_OFFSET+sizeof(struct vp_compare)==4096u,"no extension overlap");
#endif
