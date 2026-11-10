#define main recorder_entry
#include "../../lens/nxl-handoff-watch.c"
#undef main
#include <assert.h>
int main(void){
 FILE*f=fopen("/schema.json","wb");assert(f);schema_write(f);assert(!ferror(f)&&!fclose(f));
 f=fopen("/native-gamma-schema.json","wb");assert(f);native_gamma_schema(f);assert(!ferror(f)&&!fclose(f));return 0;
}
