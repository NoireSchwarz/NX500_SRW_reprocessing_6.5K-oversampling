/* SPDX-License-Identifier: AGPL-3.0-only. Borrow only from a continuously live
 * front owner; no second CAP allocation is performed; guarded native release follows retirement. */
static struct sw_lease shared_lease;
static volatile unsigned char *shared_alias;
static int shared_sma=-1;
static const char*shared_adopt_error="SHARED_FRONT_ALLOCATION_NOT_VERIFIED";
static int shared_front_live(void){
 if(!shared_borrowed||!shared_alias||!sw_owner_alive(&shared_lease)||!still_owned())return 0;
 struct sw_lease now;memcpy(&now,(const void*)(shared_alias+SW_LEASE),sizeof now);
 return sw_same_allocation(&now,&shared_lease)&&now.phase==shared_lease.phase;
}
static int shared_adopt(const char*dir){
 shared_lease=full_workspace_original;uint32_t pa=0,index=0,meta[4];
 if(!sw_owner_alive(&shared_lease)){shared_adopt_error=sw_owner_error;return 0;}
 shared_adopt_error="SHARED_FRONT_ALLOCATION_NOT_VERIFIED";
 if(shared_lease.phase!=SW_FRONT_READY||
    !translate(shared_lease.va,&pa,&index)||pa!=shared_lease.pa||index!=shared_lease.index||
    !entry(index,meta)||memcmp(meta,shared_lease.metadata,sizeof meta)||!region_ok(shared_lease.va,SW_BYTES,1,0))return 0;
 response[0]=shared_lease.va;response[1]=shared_lease.pa;response[2]=SW_BYTES;response[3]=2;response[4]=0;
 index_owned=index;memcpy(owned_meta,meta,sizeof meta);ownership_verified=1;
 shared_sma=open("/dev/d5_sma",O_RDWR|O_SYNC|O_CLOEXEC);if(shared_sma<0)return 0;
 int flags=fcntl(shared_sma,F_GETFL);if(flags<0||((unsigned)flags&0x101000u)!=0x101000u)return 0;
 void*p=mmap(NULL,SW_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,shared_sma,(off_t)(uint64_t)pa);if(p==MAP_FAILED)return 0;shared_alias=p;
 struct sw_lease observed;memcpy(&observed,(const void*)(shared_alias+SW_LEASE),sizeof observed);
 if(memcmp(&observed,&shared_lease,sizeof observed))return 0;
 unsigned char prefix[SW_CONTROL],d[32];char hash[65];memcpy(prefix,(const void*)shared_alias,sizeof prefix);
 struct sha256 h;sha256_init(&h);sha256_update(&h,prefix,sizeof prefix);sha256_final(&h,d);digest_hex(d,hash);
 if(strcmp(hash,shared_lease.prefix_sha256))return 0;
 memcpy(full_prefix,prefix,sizeof prefix);
 const uint32_t*r=(const uint32_t*)(prefix+0x4000u);
 if(r[0]!=0x3833584eu||r[2]!=shared_lease.nonce||r[3]!=7||r[12]!=1||r[14]!=1||r[20]||r[21]!=~shared_lease.nonce||r[24]!=pa)return 0;
 char path[1200];snprintf(path,sizeof path,"%s/workspace-prefix.bin",dir);if(!sw_file_new(path,prefix,sizeof prefix))return 0;
 shared_borrowed=1;return pool_head(&free_owned)&&shared_front_live();
}
static int shared_claim(const char*dir){
 if(!shared_front_live()||!sw_transition(&shared_lease,SW_FRONT_READY,SW_BACKEND_ACTIVE))return 0;
 memcpy((void*)(shared_alias+SW_LEASE),&shared_lease,sizeof shared_lease);__sync_synchronize();
 if(!shared_front_live())return 0;char path[1200];snprintf(path,sizeof path,"%s/workspace-active.bin",dir);
 if(!sw_file_new(path,&shared_lease,sizeof shared_lease))return 0;shared_claimed=1;return 1;
}
static int shared_finish(unsigned success){
 if(!shared_claimed)return 1;if(!shared_front_live())return 0;
 if(!sw_transition(&shared_lease,SW_BACKEND_ACTIVE,success?SW_BACKEND_DONE:SW_QUARANTINED))return 0;
 memcpy((void*)(shared_alias+SW_LEASE),&shared_lease,sizeof shared_lease);__sync_synchronize();shared_completed=success;
 return shared_front_live();
}
