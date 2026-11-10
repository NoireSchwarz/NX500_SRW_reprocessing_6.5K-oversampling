/* SPDX-License-Identifier: AGPL-3.0-only. Independent persisted stage audits. */
static int shared_manifest_pages(const struct full_manifest*s,const struct bitb_request*q,unsigned stage,unsigned after,const struct full_manifest*before){
 if(!full_manifest_valid(s,q,stage,after))return 0;unsigned char expected[TEST_BLOCK];
 for(unsigned k=0;k<TEST_PAGES;k++){
  unsigned block=k*TEST_BLOCK,at,n;full_expected(expected,block);
  if(stage==FULL_LUT){
   if(full_overlap(block,full_source_offset(stage),full_source_span(stage),&at,&n)){if(after&&s->blocks[k]!=before->blocks[k])return 0;continue;}
   if(after&&full_overlap(block,TEST_OUT,TEST_FRAME,&at,&n))continue;
  }else{
   if(full_overlap(block,full_source_offset(stage),full_source_span(stage),&at,&n)){if(after&&s->blocks[k]!=before->blocks[k])return 0;continue;}
   if(after&&block>=JPEG_OUT&&block-JPEG_OUT<JPEG_CAP)continue;
  }
  if(s->blocks[k]!=bitb_crc(expected,sizeof expected))return 0;
 }
 return 1;
}
static int p7_verify_files(const char*dir,const struct bitb_request*q){
 char path[1200];int ok=0;struct j422_proof p;struct full_manifest lb,la,jb,ja;struct bitb_result stage;
 unsigned char kernel[EP_KERNEL_STATE_BYTES+12],rtos[544],otherk[EP_KERNEL_STATE_BYTES+12],otherr[544],digest[32];unsigned char*pad=NULL;
 snprintf(path,sizeof path,"%s/j422-proof.bin",dir);if(!p7_read(path,&p,sizeof p)||!j422_proof_valid(&p,q)||p.crc!=p7_result.route_crc||
 strcmp(p.input_sha256,full_input_sha256)||p.lut_stage.source_crc!=full_input_crc||strcmp(p.lut_table_sha256,full_table_sha256)||strcmp(p.prefix_sha256,shared_lease.prefix_sha256))goto end;
 full_cache(q->nonce);snprintf(path,sizeof path,"%s/workspace-prefix.bin",dir);if(!p7_read(path,full_prefix,sizeof full_prefix))goto end;
 struct sha256 hash;char hex[65];sha256_init(&hash);sha256_update(&hash,full_prefix,sizeof full_prefix);sha256_final(&hash,digest);digest_hex(digest,hex);if(strcmp(hex,p.prefix_sha256))goto end;
 memset(full_footer,0,sizeof full_footer);memcpy(full_footer,&shared_lease,sizeof shared_lease);
 snprintf(path,sizeof path,"%s/p11_kernel_before.bin",dir);if(!p7_read(path,kernel,sizeof kernel))goto end;
 snprintf(path,sizeof path,"%s/p11_rtos_irq_before.bin",dir);if(!p7_read(path,rtos,sizeof rtos))goto end;
 const char*dirs[2]={"00_lut","01_jpeg"};const struct bitb_result*records[2]={&p.lut_stage,&p.stage};
 for(unsigned i=0;i<2;i++){
  snprintf(path,sizeof path,"%s/%s/result.bin",dir,dirs[i]);if(!p7_read(path,&stage,sizeof stage)||memcmp(&stage,records[i],sizeof stage))goto end;
  snprintf(path,sizeof path,"%s/%s/kernel_after.bin",dir,dirs[i]);if(!p7_read(path,otherk,sizeof otherk)||memcmp(otherk,kernel,sizeof kernel))goto end;
  snprintf(path,sizeof path,"%s/%s/rtos_after.bin",dir,dirs[i]);if(!p7_read(path,otherr,sizeof otherr)||memcmp(otherr,rtos,sizeof rtos)||bitb_crc(otherr,sizeof otherr)!=stage.owner_crc)goto end;
 }
 snprintf(path,sizeof path,"%s/00_lut/memory_before.bin",dir);if(!p7_read(path,&lb,sizeof lb)||!shared_manifest_pages(&lb,q,FULL_LUT,0,NULL))goto end;
 snprintf(path,sizeof path,"%s/00_lut/memory_after.bin",dir);if(!p7_read(path,&la,sizeof la)||!shared_manifest_pages(&la,q,FULL_LUT,1,&lb)||la.source_crc!=full_input_crc||la.output_crc!=p.lut_stage.target_crc)goto end;
 snprintf(path,sizeof path,"%s/01_jpeg/memory_before.bin",dir);if(!p7_read(path,&jb,sizeof jb)||!shared_manifest_pages(&jb,q,FULL_FINAL,0,NULL)||jb.source_crc!=la.output_crc)goto end;
 snprintf(path,sizeof path,"%s/01_jpeg/memory_after.bin",dir);if(!p7_read(path,&ja,sizeof ja)||!shared_manifest_pages(&ja,q,FULL_FINAL,1,&jb)||ja.source_crc!=jb.source_crc)goto end;
 struct p12_capture lut_capture;snprintf(path,sizeof path,"%s/00_lut/steps.bin",dir);if(!p7_read(path,&lut_capture,sizeof lut_capture)||lut_capture.magic!=P12_STEP_MAGIC||lut_capture.count!=p.lut_stage.route_samples||
 bitb_crc(&lut_capture,offsetof(struct p12_capture,crc))!=lut_capture.crc||lut_capture.crc!=p.lut_stage.route_crc)goto end;
 uint32_t dma[61];snprintf(path,sizeof path,"%s/00_lut/dma_parameters.bin",dir);if(!p7_read(path,dma,sizeof dma)||dma[0]!=p.alias_va+TEST_IN||dma[1]!=p.alias_va+TEST_IN+SW_FRONT_UV||dma[5]||dma[26]!=p.alias_va+TEST_OUT||dma[27]!=p.alias_va+TEST_OUT+TEST_Y||dma[31])goto end;
 unsigned char capture[1680];snprintf(path,sizeof path,"%s/01_jpeg/steps.bin",dir);if(!p7_read(path,capture,sizeof capture)||bitb_crc(capture,sizeof capture-4)!=ep_sync_word(capture+sizeof capture-4)||ep_sync_word(capture+sizeof capture-4)!=p.stage.route_crc)goto end;
 unsigned bytes=(p.jpeg.stream_bytes+511u)&~511u;pad=malloc(bytes);if(!pad)goto end;
 snprintf(path,sizeof path,"%s/01_jpeg/padded_stream.bin",dir);if(!p7_read(path,pad,bytes)||!jpeg_parse(pad,p.jpeg.stream_bytes)||bitb_crc(pad,p.jpeg.stream_bytes)!=p.stage.target_crc||ja.pad_bytes!=bytes||ja.pad_crc!=bitb_crc(pad,bytes))goto end;
 const char*names[2]={"native-actual.jpg","native-422.jpg"};
 for(unsigned i=0;i<2;i++){
  snprintf(path,sizeof path,"%s/%s",dir,names[i]);FILE*f=fopen(path,"rb");if(!f)goto end;unsigned got=0;int equal=1;unsigned char data[4096];
  while(got<p.jpeg.stream_bytes){unsigned n=p.jpeg.stream_bytes-got;if(n>sizeof data)n=sizeof data;if(fread(data,1,n,f)!=n||memcmp(data,pad+got,n)){equal=0;break;}got+=n;}if(fgetc(f)!=EOF)equal=0;fclose(f);if(!equal)goto end;
 }
 uint32_t packet[48];snprintf(path,sizeof path,"%s/01_jpeg/native_packet.bin",dir);
 if(!p7_read(path,packet,sizeof packet)||packet[0]!=TEST_W||packet[1]!=TEST_H||packet[2]!=1||packet[3]!=2||packet[4]!=TEST_W||packet[5]||packet[8]||packet[9]||
 packet[10]!=p.alias_va+TEST_OUT||packet[11]!=p.alias_va+TEST_OUT+TEST_Y||packet[12]!=p.alias_va+JPEG_OUT)goto end;
 for(unsigned i=16;i<48;i++)if(packet[i]!=0x10101010u)goto end;ok=1;
end:free(pad);return ok;
}
