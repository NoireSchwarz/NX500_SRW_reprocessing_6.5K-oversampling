/* Explicit modeled read-only worker transport. No hardware or process stop. */
static int p7_copy_control(const char *dir){
 const char *mode=getenv("NXL_SIM_CASE");if(!mode)mode="success";
 if(!fingerprint("/mnt/mmc/nx-lut-alloc-update/nxl-source-p10.arm",BITB_WORKER_BYTES,BITB_WORKER_FNV)){p7_error="WORKER_PREPARATION_FAILED";return 0;}
 p7_worker_ready=1;if(!strcmp(mode,"p7_pause_failed")){p7_error="CAMERA_PAUSE_REHEARSAL_FAILED";return 0;}
 p7_pauses=p7_resumes=p7_resumed_verified=p7_resume_window=1;
 struct alloc_regs regs;trace_phase="copy_resume_getter";
 if(!invoke(udd_base+EP_OPEN_COUNT_FN,0,3000)||!get_regs(&regs)||regs.r[0]!=ep_open_count){p7_error="NATIVE_WORKER_AFTER_RESUME_UNCONFIRMED";return 0;}
 struct bitb_request q={BITB_REQUEST_MAGIC,4,response[1],BITB_BYTES,pattern_nonce,(uint32_t)pid,response[0],0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));
 char path[1200];snprintf(path,sizeof path,"%s/copy_request.bin",dir);p7_save(path,&q,sizeof q);
 struct h3dl_capture *c=calloc(1,sizeof *c);if(!c)return 0;h3dl_init(c);c->count=H3DL_SAMPLES;
 for(unsigned i=0;i<H3DL_SAMPLES;i++){
  struct h3dl_sample *s=c->sample+i;s->elapsed_ms=i*20;s->clock_on=1;
  for(unsigned j=0;j<H3DL_TOP_COUNT;j++)s->top[j]=h3dl_top_offsets[j]>=0x1100?0xf0f0f0f0u:0;
  s->lut[0]=1;s->lut[1]=0x100;s->lut[3]=0x81115200;ep_kernel_read(EP_KERNEL_STATE_VA,s->kernel_raw,sizeof s->kernel_raw);ep_kernel_read(EP_KERNEL_MUTEX_VA,s->kernel_mutex,12);
 }
 memset(&p7_result,0,sizeof p7_result);p7_result.magic=BITB_RESULT_MAGIC;p7_result.version=4;p7_result.bytes=sizeof p7_result;p7_result.request_crc=q.crc;p7_result.phase=9;p7_result.pa=q.pa;p7_result.alias_va=0x90000000;p7_result.udd_base=0xb0e21000;p7_result.ep_fd=4;p7_result.sma_fd=5;p7_result.sma_open_flags=0x101000;
 p7_result.ready=p7_result.paused_contract=p7_result.safe_to_release=1;p7_result.code_guards=11;p7_result.rtos_guards=12;p7_result.translations=p7_result.matches=5;p7_result.request_bytes=BITB_BYTES;
 p7_result.route_samples=H3DL_SAMPLES;p7_result.route_verified=h3dl_capture_valid(c);p7_result.route_crc=bitb_crc(c,sizeof *c);p7_result.route_flags=h3dl_capture_flags(c);
 if(!strcmp(mode,"p9_capture_corrupt"))c->sample[0].lut[1]^=1;
 snprintf(path,sizeof path,"%s/h3dl_samples.bin",dir);p7_save(path,c,sizeof *c);free(c);
 uint32_t addresses[5][3];const unsigned offsets[5]={0,16384,32768,49152,65535};
 for(unsigned i=0;i<5;i++){addresses[i][0]=offsets[i];addresses[i][1]=p7_result.alias_va+offsets[i];addresses[i][2]=q.pa+offsets[i];}
 if(!strcmp(mode,"p9_address_mismatch"))addresses[4][2]^=1;
 snprintf(path,sizeof path,"%s/h3dl_addresses.bin",dir);p7_save(path,addresses,sizeof addresses);
 struct h3dl_rtos_guard guards[sizeof h3dl_rtos_guards/sizeof h3dl_rtos_guards[0]];memcpy(guards,h3dl_rtos_guards,sizeof guards);if(!strcmp(mode,"p9_guard_corrupt"))guards[0].words[0]^=1;
 snprintf(path,sizeof path,"%s/h3dl_rtos_guards.bin",dir);p7_save(path,guards,sizeof guards);
 snprintf(path,sizeof path,"%s/h3dl_worker_maps.txt",dir);char maps[1100];int n=snprintf(maps,sizeof maps,"90000000-90010000 r--s %08x 00:00 8 /dev/d5_sma\n70000000-70002000 r--s 20820000 00:00 8 /dev/drime5_ep\n70003000-70004000 r--s 2082b000 00:00 8 /dev/drime5_ep\nb0e21000-b0e6e000 r-xp 00000000 00:00 5 /usr/lib/libudd5.so\n",q.pa);
 const uint32_t pages[6]={0x803fb000,0x804b6000,0x804bb000,0x804cf000,0x817d6000,0x817d8000};
 for(unsigned i=0;i<6;i++)n+=snprintf(maps+n,sizeof maps-(unsigned)n,"%08x-%08x r--s %08x 00:00 8 /dev/mem\n",0x70010000+i*0x2000,0x70011000+i*0x2000,pages[i]);p7_save(path,maps,n);
 snprintf(path,sizeof path,"%s/h3dl_rtos_access.txt",dir);FILE *access=fopen(path,"w");if(!access)return 0;
 fprintf(access,"RTOS_ACCESS version=1 mechanism=FIXED_PROT_READ_SHARED_SYNC pages=6\nKERNEL_BOUNDS available=1 errno=0 phys_offset=0x86000000 pv_offset=0x3a000000\nPREAD_PROBE address=0x803fb3b8 bytes=32 returned=-1 errno=14\n");
 for(unsigned i=0;i<6;i++)fprintf(access,"MAP index=%u physical=0x%08x bytes=4096 prot=READ ok=1 errno=0\n",i,pages[i]);
 for(unsigned i=0;i<6;i++){fprintf(access,"GUARD index=%u physical=0x%08x read_ok=1 equal=1",i,h3dl_rtos_guards[i].address);for(unsigned j=0;j<8;j++)fprintf(access," word%u=0x%08x",j,h3dl_rtos_guards[i].words[j]);fputc('\n',access);}fclose(access);
 unsigned char table[P10_SOURCE_BYTES];for(unsigned i=0;i<P10_SOURCE_BYTES;i++)table[i]=(unsigned char)(i*17u+q.nonce);
 p7_result.source_pa=0x81115200;p7_result.source_copies=2;p7_result.source_stable=1;p7_result.table_source_crc=bitb_crc(table,sizeof table);
 snprintf(path,sizeof path,"%s/lut_source_before.bin",dir);p7_save(path,table,sizeof table);
 if(!strcmp(mode,"p10_source_corrupt"))table[100]^=1;
 snprintf(path,sizeof path,"%s/lut_source_after.bin",dir);p7_save(path,table,sizeof table);
 if(!strcmp(mode,"p10_source_pointer"))p7_result.source_pa+=256;
 struct p10_owner owners[2];memset(owners,0,sizeof owners);
 owners[0].magic=P10_OWNER_MAGIC;owners[0].version=1;owners[0].bytes=sizeof owners[0];owners[0].count=2;owners[0].table_pa=P10_OBSERVER_TABLE;owners[0].object_bytes=owners[0].code_bytes=32;owners[0].table[3]=0x81560000;
 struct p10_owner_entry *e=owners[0].entry;e->selector=7;e->present=1;e->object_pa=0x81560000;e->object[0]=e->vtable_pa=0x80a00000;e->notify_fn=e->vtable[2]=0x80100000;e->object[1]=123;for(unsigned i=0;i<8;i++)e->code[i]=0xe1a00000;owners[0].entry[1].selector=8;
 owners[1]=owners[0];owners[1].entry[0].object[1]++;
 p7_result.owner_records=2;p7_result.owner_stable=1;p7_result.owner_crc=bitb_crc(owners,sizeof owners);
 if(!strcmp(mode,"p10_owner_badptr"))owners[1].entry[0].object_pa=0x90000000;
 if(!strcmp(mode,"p10_owner_codechange"))owners[1].entry[0].code[0]^=1;
 snprintf(path,sizeof path,"%s/rtos_owner_before.bin",dir);p7_save(path,owners,sizeof owners[0]);snprintf(path,sizeof path,"%s/rtos_owner_after.bin",dir);p7_save(path,owners+1,sizeof owners[1]);
 struct h3dl_rtos_guard extra_guards[sizeof p10_owner_guards/sizeof p10_owner_guards[0]];memcpy(extra_guards,p10_owner_guards,sizeof extra_guards);if(!strcmp(mode,"p10_owner_guard_corrupt"))extra_guards[0].words[0]^=1;
 snprintf(path,sizeof path,"%s/rtos_owner_guards.bin",dir);p7_save(path,extra_guards,sizeof extra_guards);
 snprintf(path,sizeof path,"%s/h3dl_worker_maps_after.txt",dir);FILE *after=fopen(path,"w");if(!after)return 0;fwrite(maps,1,n,after);
 fprintf(after,"70100000-70105000 r--s 81115000 00:00 8 /dev/mem\n70106000-70107000 r--s 8153c000 00:00 8 /dev/mem\n70108000-70109000 r--s 81560000 00:00 8 /dev/mem\n7010a000-7010b000 r--s 80a00000 00:00 8 /dev/mem\n7010c000-7010d000 r--s 80100000 00:00 8 /dev/mem\n7010e000-7010f000 r--s 801ca000 00:00 8 /dev/mem\n70110000-70111000 r--s 801cc000 00:00 8 /dev/mem\n70112000-70113000 r--s 8017b000 00:00 8 /dev/mem\n70114000-70115000 r--s 804a5000 00:00 8 /dev/mem\n");fclose(after);
 snprintf(path,sizeof path,"%s/p10_read_ranges.txt",dir);p7_save(path,"MODELED_READ_RANGES\n",20);
 p7_result.crc=bitb_crc(&p7_result,offsetof(struct bitb_result,crc));snprintf(path,sizeof path,"%s/copy_worker.bin",dir);p7_save(path,&p7_result,sizeof p7_result);
 p7_go_sent=p7_worker_exited=p7_rehearsal_verified=p7_result_valid=p7_safe_to_release=1;p7_pauses=p7_resumes=p7_resumed_verified=2;p7_peer_scans=3;p7_peer_pids=9;
 snprintf(path,sizeof path,"%s/copy_peer_audit.txt",dir);p7_save(path,"MODELED_PEER_SCAN\n",18);
 p7_verified=p7_result.route_verified&&p7_verify_files(dir,&q);p7_error=p7_verified?"":"H3DL_CAPTURE_INVALID";return p7_verified;
}
