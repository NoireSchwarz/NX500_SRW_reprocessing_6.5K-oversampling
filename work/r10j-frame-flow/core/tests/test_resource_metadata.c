#define main rearm_unused_main
#include "../src/nxl-session-rearm.c"
#undef main
#include <assert.h>
int main(int argc,char**argv){
 assert(argc==3);job=previous=argv[1];int ok=resource_metadata();
 if(!strcmp(argv[2],"reject")){assert(!ok);puts("PASS incomplete or inconsistent resource facts rejected");return 0;}
 assert(ok&&inventory.operations==1&&inventory.count==1&&inventory.item[0].mode==RI_UNLOCATED&&inventory.item[0].bytes==218103808u);
 uint32_t*t=malloc(RI_BLOCKS*16);assert(t);FILE*f=fopen("/mnt/mmc/allocator-latest.bin","rb");assert(f&&fread(t,16,RI_BLOCKS,f)==RI_BLOCKS&&!fclose(f));
 unsigned free_count=0;for(unsigned i=0;i<RI_BLOCKS;){if(!(t[4*i]&255u)){free_count++;i++;}else i+=t[4*i+1];}
 assert(ri_partition(&inventory,t,free_count)&&ri_retired(&inventory));
 memset(t,0,RI_BLOCKS*16);unsigned at=(0xa1c00000u-0x94000000u)/16384u;t[4*at]=35;t[4*at+1]=13312;
 assert(ri_partition(&inventory,t,RI_BLOCKS-13312)&&!ri_retired(&inventory));
 puts("PASS actual failed job accepted for current audit without reason/boot whitelist; actual allocator proves208MiB shape absent; matching shape remains held");free(t);return 0;
}
