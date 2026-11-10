#include "../../common/jpeg_ticket.h"
/* Read only the final JPEG plus compact native status, never YUV/proof planes. */
static int p7_verify_files(const char*dir,const struct bitb_request*q){
 char path[1200],hex[65];struct j422_proof p;snprintf(path,sizeof path,"%s/j422-proof.bin",dir);
 if(!p7_read(path,&p,sizeof p)||p.magic!=J422_PROOF_MAGIC||p.version!=4||p.bytes!=sizeof p||p.request_crc!=q->crc||p.pa!=q->pa||p.started!=3||p.completed!=3||p.failed||!p.stream_saved||!p.direct_handoff||p.handoff_bytes||p.allocation_requests||p.protected_prefix_bytes!=SW_CONTROL||p.crc!=bitb_crc(&p,offsetof(struct j422_proof,crc))||p.crc!=p7_result.route_crc||strcmp(p.lut_table_sha256,full_table_sha256)||strcmp(p.prefix_sha256,shared_lease.prefix_sha256))return 0;
 if(!j422_stage_valid(&p.lut_stage,q,p.alias_va,1)||!j422_stage_valid(&p.stage,q,p.alias_va,0)||!p.lut_stage.table_loaded||!j422_stage_valid(&p.ldc_stage,q,p.alias_va,2)||!p.jpeg_quant_verified)return 0;
 struct jpeg_meta*j=&p.jpeg;if(j->magic!=JPEG_META_MAGIC||j->width!=TEST_W||j->height!=TEST_H||j->format!=422||!j->structural||!j->saved||j->native_return||j->error_before||j->error_after||j->stream_bits!=j->stream_bytes*8||j->stream_bytes<32||j->stream_bytes>JPEG_CAP-JPEG_PAD||j->crc!=bitb_crc(j,offsetof(struct jpeg_meta,crc)))return 0;
 FILE*f=fopen(full_output_target,"rb");if(!f)return 0;struct stat before,after;if(fstat(fileno(f),&before)||!S_ISREG(before.st_mode)||(uint64_t)before.st_size!=j->stream_bytes){fclose(f);return 0;}unsigned char b[65536],d[32],head[65536];unsigned used=0;uint32_t crc=~0u;struct sha256 h;sha256_init(&h);size_t n;int ok=1;
 while((n=fread(b,1,sizeof b,f))){if(!used)memcpy(head,b,n);if(n>j->stream_bytes-used){ok=0;break;}sha256_update(&h,b,n);crc=bitb_crc_step(crc,b,n);used+=(unsigned)n;}
 if(ferror(f)||fstat(fileno(f),&after)||!jt_same_stat(&before,&after))ok=0;if(fclose(f))ok=0;if(!ok||used!=j->stream_bytes||(~crc)!=p.stage.target_crc)return 0;
 /* The original hardware stream was structurally checked in the worker;
  * read-back binds these exact bytes before the CAP is returned. */
 if(used<4||head[0]!=0xff||head[1]!=0xd8)return 0;
 sha256_final(&h,d);digest_hex(d,hex);snprintf(path,sizeof path,"%s/final-jpeg.sha256",dir);if(!sw_file_new(path,hex,64))return 0;
 snprintf(path,sizeof path,"%s/final-jpeg-ticket.bin",dir);
 if(!jt_write(path,&after,shared_lease.nonce,shared_lease.pa,shared_lease.owner_pid,hex,shared_lease.source_sha256))fprintf(stderr,"JPEG_READBACK_TICKET unavailable=1 full_readback_passed=1\n");
 return 1;
}
