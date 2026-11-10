/* SPDX-License-Identifier: AGPL-3.0-only. Bind the live owner to attested pixels. */
#include "../../common/shared_workspace_io.h"
static int post_shared_inputs(const char*root,const struct cfg*c,const struct attestation*a){
 const char*suffix="/case_00/post-input.txt";size_t n=strlen(c->producer),tail=strlen(suffix);
 if(n<=tail||strcmp(c->producer+n-tail,suffix)||n-tail>=1100)return 0;
 char front[1100],path[1200],dest[1200];memcpy(front,c->producer,n-tail);front[n-tail]=0;
 struct sw_lease lease,ready;snprintf(path,sizeof path,"%s/shared-workspace.bin",front);
 if(!sw_file_read(path,&lease,sizeof lease)||!sw_valid(&lease)||lease.phase!=SW_FRONT_READY||!sw_owner_alive(&lease)||lease.nonce!=a->nonce||strcmp(lease.source_sha256,c->source))return 0;
 snprintf(path,sizeof path,"%s/workspace-owner.ready",front);if(!sw_file_read(path,&ready,sizeof ready)||memcmp(&ready,&lease,sizeof lease))return 0;
 unsigned char table[LUT_SLOT_BYTES];snprintf(path,sizeof path,"%s/srw-lut.bin",front);if(!sw_file_read(path,table,sizeof table)||!lut_table_valid(table,sizeof table))return 0;
 char hex[65];struct sha256 hash;unsigned char digest[32];sha256_init(&hash);sha256_update(&hash,table,sizeof table);sha256_final(&hash,digest);digest_hex(digest,hex);if(strcmp(hex,lease.lut_sha256))return 0;
 snprintf(dest,sizeof dest,"%s/shared-workspace.bin",root);if(!sw_file_new(dest,&lease,sizeof lease))return 0;
 snprintf(dest,sizeof dest,"%s/srw-lut.bin",root);return sw_file_new(dest,table,sizeof table)&&dir_sync(root);
}
