#define main rearm_real_entry
#include "../src/nxl-session-rearm.c"
#undef main
#include <assert.h>
int main(int argc,char**argv){
 previous="/mnt/mmc/NXDEMO51/JOB_MODEL";strcpy(front,"/mnt/mmc/NXDEMO51/JOB_MODEL/front_1/front");backend_attempt=1;
 int ok=fast_rearm_metadata();if(argc==2&&!strcmp(argv[1],"reject"))assert(!ok);
 else{assert(ok&&backend_info());assert(old_pa==backend_pa&&old_va==backend_va&&old_bytes==167772160u&&old_nonce==777);}
 puts(ok?"PASS compact RESET metadata, same front/backend160MiB range, no image files required":"PASS modified compact RESET evidence rejected");return 0;
}
