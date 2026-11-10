/* Diagnostic receive proof uses mappings and the actual SVC instruction.
 * It does not depend on a libc return offset or on a particular saved LR.
 * The native call injector retains its independent stricter ptrace checks. */
static int shell_receive_context(void*x,const struct shell_frame*f,uint32_t libc,uint32_t shell){
 (void)x;(void)shell;char path[100],linebuf[768];snprintf(path,sizeof path,"/proc/%d/maps",camera_pid);FILE*maps=fopen(path,"r");if(!maps)return 0;
 int stack=0,code=0;while(fgets(linebuf,sizeof linebuf,maps)){
  unsigned lo,hi,off;char perms[8],name[256]={0};int n=sscanf(linebuf,"%x-%x %7s %x %*s %*u %255s",&lo,&hi,perms,&off,name);if(n<4||lo>=hi)continue;
  if(perms[0]=='r'&&perms[1]=='w'&&f->sp>=lo&&f->sp+4<=hi&&f->arg[1]>=lo&&f->arg[1]+212<=hi)stack=1;
  if(n==5&&!strcmp(name,"/lib/libc-2.13.so")&&perms[0]=='r'&&perms[2]=='x'&&f->pc>=libc&&f->pc>=lo+8u&&f->pc+4<=hi)code=1;
 }
 int ok=!ferror(maps);if(fclose(maps))ok=0;if(!ok||!stack||!code)return 0;
 uint32_t words[3];if(!procread((uint32_t)f->pc-8,words,sizeof words))return 0;
 return words[0]==0xef000000u||words[1]==0xef000000u||words[2]==0xef000000u;
}
