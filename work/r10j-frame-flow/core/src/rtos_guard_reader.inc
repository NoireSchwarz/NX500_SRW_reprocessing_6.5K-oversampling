/* SPDX-License-Identifier: AGPL-3.0-only. Read original RTOS code via RO pages.
 * Same /dev/mem mmap path used by physically validated p26-p30 observers.
 * All original byte signatures remain mandatory. Never use pread here.
 */
#ifndef P33_GUARD_MMAP
#define P33_GUARD_MMAP mmap
#define P33_GUARD_MUNMAP munmap
#endif
static unsigned rtos_guard_samples,rtos_guard_mappings,rtos_guard_unmapped,rtos_guard_errno,rtos_guard_crosspage;
/* A fixed 32-byte fingerprint may straddle two physical code pages. Keep
 * the same pinned P7 bounds and RO permissions; only map the needed span. */
static int ro_guard_layout(uint32_t address,uint32_t*page,unsigned*bytes){
 if((address&3u)||address<0x80000000u||(uint64_t)address+32u>0x81000000ULL)return 0;
 *page=address&~4095u;*bytes=(address&4095u)+32u>4096u?8192u:4096u;return 1;
}
static int ro_native_guards(const char *dir){
 rtos_readonly_guards=0;rtos_guard_samples=rtos_guard_mappings=rtos_guard_unmapped=rtos_guard_errno=rtos_guard_crosspage=0;
 char path[1200];snprintf(path,sizeof path,"%s/p39_rtos_code_guards.txt",dir);
 FILE *audit=fopen(path,"w");if(!audit){rtos_guard_errno=(unsigned)errno;bridge_error="RTOS_GUARD_AUDIT_CREATE_FAILED";return 0;}
 int ok=0,flags=fcntl(devfd,F_GETFL);
 fputs("format=NXRTOSCODEGUARD3\nmethod=mmap_readonly\n",audit);
 if(flags<0||(flags&O_ACCMODE)!=O_RDONLY){rtos_guard_errno=(unsigned)(flags<0?errno:EACCES);bridge_error="RTOS_GUARD_FD_NOT_READONLY";goto done;}
 for(unsigned i=0;i<sizeof p33_rtos_guards/sizeof *p33_rtos_guards;i++){
  uint32_t address=p33_rtos_guards[i].offset,page=0,offset=address&4095u;unsigned map_bytes=0;
  if(!ro_guard_layout(address,&page,&map_bytes)){
   fprintf(audit,"guard=%u address=0x%08x bytes=32 range_invalid=1\n",i,address);
   bridge_error="RTOS_GUARD_FIXED_RANGE_INVALID";goto done;
  }
  errno=0;void *map=P33_GUARD_MMAP(NULL,map_bytes,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)page);
  if(map==MAP_FAILED){rtos_guard_errno=(unsigned)errno;fprintf(audit,"guard=%u address=0x%08x page=0x%08x map_bytes=%u map_failed=1 errno=%u\n",i,address,page,map_bytes,rtos_guard_errno);bridge_error="RTOS_GUARD_READONLY_MAP_FAILED";goto done;}
  rtos_guard_mappings++;rtos_guard_crosspage+=map_bytes==8192u;uint32_t observed[8];const volatile unsigned char *p=(const volatile unsigned char*)map+offset;
  for(unsigned w=0;w<8;w++){uint32_t value=0;for(unsigned b=0;b<4;b++)value|=(uint32_t)p[w*4+b]<<(b*8);observed[w]=value;}
  int matched=!memcmp(observed,p33_rtos_guards[i].words,32);
  errno=0;int unmapped=P33_GUARD_MUNMAP(map,map_bytes)==0;if(unmapped)rtos_guard_unmapped++;else rtos_guard_errno=(unsigned)errno;
  fprintf(audit,"guard=%u address=0x%08x page=0x%08x map_bytes=%u bytes=32 matched=%d unmapped=%d errno=%u\n",i,address,page,map_bytes,matched,unmapped,rtos_guard_errno);
  fputs("expected=",audit);for(unsigned w=0;w<8;w++)fprintf(audit,"%s%08x",w?",":"",p33_rtos_guards[i].words[w]);
  fputs("\nobserved=",audit);for(unsigned w=0;w<8;w++)fprintf(audit,"%s%08x",w?",":"",observed[w]);fputc('\n',audit);
  if(!unmapped){bridge_error="RTOS_GUARD_READONLY_UNMAP_FAILED";goto done;}
  if(!matched){bridge_error="RTOS_GUARD_BYTE_MISMATCH";goto done;}
  rtos_guard_samples++;
 }
 ok=1;bridge_error="RTOS_GUARDS_VERIFIED";
done:
 fprintf(audit,"result=%s verified=%u mappings=%u unmapped=%u cross_page=%u errno=%u\n",bridge_error,rtos_guard_samples,rtos_guard_mappings,rtos_guard_unmapped,rtos_guard_crosspage,rtos_guard_errno);
 if(fflush(audit)||fsync(fileno(audit))||ferror(audit))ok=0;if(fclose(audit))ok=0;
 if(!ok&&!strcmp(bridge_error,"RTOS_GUARDS_VERIFIED"))bridge_error="RTOS_GUARD_AUDIT_SAVE_FAILED";
 rtos_readonly_guards=(unsigned)ok;return ok;
}
