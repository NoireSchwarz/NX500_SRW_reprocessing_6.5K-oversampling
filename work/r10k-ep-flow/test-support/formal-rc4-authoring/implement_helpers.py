from context import *
import re
U=T/'core/src'
# Port only Metol's one-shot preview loader, never its video watcher.
p=W/'research/Metol/test_server/lutpipe/nx_lutload.c'
s=p.read_text('utf8').split('static unsigned fingerprint(')[0]
s='/* SPDX-License-Identifier: AGPL-3.0-only\n * One-shot preview reload adapted from WApaperplane/metol, commit\n * b10ec8413ce5f9a03079288cb9880e4d0fac983f. Original source in SOURCE.\n */\n'+s
s=s.replace('#include <media/drime5/ep/d5_ep_ioctl.h>','#include "lutload_ep_abi.h"')
s=s.replace('static void barrier(void)', '#if defined(__clang__)\n#pragma clang diagnostic ignored "-Winline-asm"\n#endif\nstatic void barrier(void)')
s=s.replace('static int load_main(','''static int slot_ok(unsigned a){return a==0x810fd100u||a==0x81101e00u||a==0x81106b00u||a==0x81115200u||a==0x81119f00u||a==0x8111ec00u;}
static int source_main(void){
 if(!camera_ok())return 3;
 int f=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(f<0)return 1;
 volatile const unsigned*r=mmap(0,4096,PROT_READ,MAP_SHARED,f,0x2082b000);
 close(f);if(r==MAP_FAILED)return 1;
 unsigned a=r[3];int ok=slot_ok(a);munmap((void*)r,4096);
 if(ok)printf("0x%08x\\n",a);return ok?0:2;
}
static int load_main(''')
s=s.replace('int fd=-1,mem=-1,lock=-1,rc=1,started=0,wait_only;','int fd=-1,mem=-1,lock=-1,rc=1,started=0,wait_only,current=0;')
s=s.replace('if(argc==2 && !strcmp(argv[1],"wait-test"))wait_only=1;','if(argc==2 && !strcmp(argv[1],"current")){wait_only=0;current=1;}\n    else if(argc==2 && !strcmp(argv[1],"wait-test"))wait_only=1;')
s=s.replace('/tmp/nxwa_lutload.lock','/tmp/nx-lut-lite/lutload.fd').replace('O_RDWR|O_CREAT,0600','O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600').replace('open("/dev/drime5_ep",O_RDWR)','open("/dev/drime5_ep",O_RDWR|O_CLOEXEC)').replace('open("/dev/mem",O_RDWR|O_SYNC)','open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC)')
s=s.replace('started=1;r[0]|=1;', 'if(current)addr=r[3];\n        if(!slot_ok(addr)){rc=2;goto out;}\n        if(r[1]!=0 && r[1]!=0x100){rc=5;goto out;}\n        started=1;r[0]|=1;')
s+='''int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_LUTLOAD_RC4 Metol EOF14 DMA7 one-shot; no watcher");return 0;}
 if(argc==2&&!strcmp(argv[1],"source"))return source_main();
 return load_main(argc,argv,1);
}
'''
(U/'nxl-lutload.c').write_text(s,'utf8',newline='\n')
(U/'lutload_ep_abi.h').write_text('''/* Samsung DRIMe5 public EP ABI; source headers retained with Metol evidence. */
#ifndef NXL_LUTLOAD_EP_ABI_H
#define NXL_LUTLOAD_EP_ABI_H
#include <sys/ioctl.h>
enum ep_k_core_int_sig { EP_K_INT_CORE_RSZ_OUT1_EOF=14 };
enum ep_k_acc_int_sig { EP_K_INT_ACC_3DLUT_RD_FINISH=7 };
struct ep_k_core_intr_wait_info {int timeout_ms;enum ep_k_core_int_sig intr;};
struct ep_k_acc_intr_wait_info {int timeout_ms;unsigned jpeg_err;enum ep_k_acc_int_sig intr;};
#define EP_IOCTL_CORE_INTR_WAIT _IOW('h',6,struct ep_k_core_intr_wait_info)
#define EP_IOCTL_CORE_INTR_WAIT_QUEUE_INIT _IOW('h',7,struct ep_k_core_intr_wait_info)
#define EP_IOCTL_ACC_INTR_WAIT _IOWR('h',8,struct ep_k_acc_intr_wait_info)
#define EP_IOCTL_ACC_INTR_WAIT_QUEUE_INIT _IOW('h',9,struct ep_k_acc_intr_wait_info)
#define EP_IOCTL_CORE_INTR_ENABLE _IOW('h',14,enum ep_k_core_int_sig)
#define EP_IOCTL_ACC_INTR_ENABLE _IOW('h',16,enum ep_k_acc_int_sig)
_Static_assert(sizeof(struct ep_k_core_intr_wait_info)==8,"core ABI");
_Static_assert(sizeof(struct ep_k_acc_intr_wait_info)==12,"acc ABI");
#endif
''','utf8',newline='\n')

# Reuse only the read-only, already deployed identity and shared-page readers.
vm=(U/'nxl-video-mode.c').read_text('utf8');vp=(U/'video_native_profile.inc').read_text('utf8')
def func(text,name):
 m=re.search(r'^static [^\n]*\b'+re.escape(name)+r'\([^;\n]*\)\{',text,re.M);assert m,name
 i=text.index('{',m.start());depth=1;j=i+1
 while depth:
  if text[j]=='{':depth+=1
  elif text[j]=='}':depth-=1
  j+=1
 return text[m.start():j]+'\n'
prefix=vm[:vm.index('static volatile')].replace('#include <sys/ptrace.h>\n','')
prefix=prefix.replace('#include "../../common/video_mode_patch.h"\n','').replace('#include "../../common/video_native_24p.h"\n','')
prefix+='struct camera {int pid,mem;uint64_t start;uint32_t movie_base,user_base;};\n'
for n in ['ms','text','identity','camera_pid','firmware','busy']:prefix+=func(vm,n)
prefix+=vp[vp.index('#define VP_MOVIE'):vp.index('static int vp_digest')]
for n in ['vp_digest','vp_base','vp_rate_context','vp_preference_mapping','vp_preferences','vp_library_context']:prefix+=func(vp,n)
prefix=prefix.replace('vp_raw_preferences[3]','vp_raw_preferences[4]').replace('pointer_after,a[3],b[3]','pointer_after,a[4],b[4]').replace('offsets[3]={0x58u,0x250u,0x254u}','offsets[4]={0x58u,0x250u,0x254u,0x138u}').replace('i<3;i++','i<4;i++')
prefix=prefix.replace('vp_preferences_valid,vp_rate,vp_ntsc,vp_size,vp_quality,vp_writes,vp_rollback,vp_thawed','vp_preferences_valid,vp_ntsc,vp_size,vp_quality')
photo=json.loads((W/'research/photo-resolution-table.json').read_text('utf8'))
photo=[x for x in photo if x['words'][4]!=0]
prefix+='struct photo_spec {unsigned value;const char*name;};\nstatic const struct photo_spec photos[]={'+','.join('{'+str(x['words'][4])+',"'+x['name']+'"}'for x in photo)+'};\n'
(U/'native_refresh_readonly.inc').write_text(prefix,'utf8',newline='\n')
print('PASS generated isolated loader and read-only OEM readers')
