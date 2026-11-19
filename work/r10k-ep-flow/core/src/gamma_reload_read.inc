/* Production read paths shared with ARM regression fixtures. */
static int physical_read(void*c,uint32_t at,void*out,unsigned bytes){
 (void)c;if(!bytes||bytes>8192u||at<0x80000000u||(uint64_t)at+bytes>0x94000000ULL)return 0;
 uint32_t page=at&~4095u;unsigned extent=((at&4095u)+bytes+4095u)&~4095u;
 void*p=mmap(NULL,extent,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)page);if(p==MAP_FAILED)return 0;
 const volatile unsigned char*s=(const volatile unsigned char*)p+(at&4095u);for(unsigned i=0;i<bytes;i++)((unsigned char*)out)[i]=s[i];video_publish_barrier();return !munmap(p,extent);
}
/* Only the two fixed Gamma DDR shadows are additional readable extents.
 * Verify the native identity mapping and both registered parameter bases
 * before and after the bounded read. No writable mapping or MMIO access. */
static int gamma_shadow_mapping(void){
 uint32_t section,bases[2];
 if(!physical_read(NULL,0x81002edcu,&section,4)||!physical_read(NULL,0x813ebf50u,bases,8))return 0;
 if(section!=0xbb701402u||bases[0]!=0xbb71b300u||bases[1]!=0xbb71d300u){errno=ESTALE;return 0;}return 1;
}
static int gamma_shadow_read(uint32_t at,void*out,unsigned bytes){
 if(!out||bytes!=NG_BYTES||(at!=NG_DDR0&&at!=NG_DDR1)){errno=EINVAL;return 0;}
 if(!gamma_shadow_mapping())return 0;
 uint32_t page=at&~4095u;unsigned extent=((at&4095u)+bytes+4095u)&~4095u;
 void*p=mmap(NULL,extent,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)page);if(p==MAP_FAILED)return 0;
 const volatile unsigned char*s=(const volatile unsigned char*)p+(at&4095u);
 for(unsigned i=0;i<bytes;i++)((unsigned char*)out)[i]=s[i];video_publish_barrier();
 if(munmap(p,extent))return 0;return gamma_shadow_mapping();
}
