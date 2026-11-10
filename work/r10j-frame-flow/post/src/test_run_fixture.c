/* Host-only full main exercise. The production build does not include this file. */
#define execl post_test_execl
#include "hw-post.c"
#undef execl
#include <stdarg.h>
int post_test_execl(const char *path,const char *arg,...){
 (void)arg;if(strcmp(path,"/opt/usr/nx-lut-lite/bin/nxl-hw-controller.arm")){errno=EPERM;return -1;}
 va_list ap;va_start(ap,arg);const char *dir=va_arg(ap,const char*);va_end(ap);
 const char *tool=getenv("NXPOST_TEST_CONTROLLER"),*jpeg=getenv("NXPOST_TEST_JPEG");if(!tool||!jpeg||!dir){errno=EINVAL;return -1;}
 char *argv[]={(char*)tool,"controller",(char*)dir,(char*)jpeg,NULL};return execv(tool,argv);
}
