#define main rearm_entry
#include "../src/nxl-session-rearm.c"
#undef main
int main(int argc,char**argv){if(argc!=3)return 2;previous=argv[1];backend_attempt=1;snprintf(front,sizeof front,"%s/front_1/front",previous);int ok=fast_rearm_metadata();printf("valid=%d bytes=%u pa=%08x\n",ok,old_bytes,old_pa);return ok==atoi(argv[2])?0:1;}
