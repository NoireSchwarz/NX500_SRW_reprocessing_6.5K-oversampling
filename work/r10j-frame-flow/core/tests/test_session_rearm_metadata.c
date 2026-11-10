#define main rearm_production_main
#include "../src/nxl-session-rearm.c"
#undef main
int main(int argc,char**argv){if(argc!=2)return 2;previous=argv[1];int m=metadata(),b=m&&backend_info();printf("metadata=%d backend=%d front_pa=%u backend_pa=%u front_nonce=%u\n",m,b,old_pa,backend_pa,old_nonce);return m&&b?0:1;}
