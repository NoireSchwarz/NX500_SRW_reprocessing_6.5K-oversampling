/* Exercise the actual production streaming verifier against all selected SRWs. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "p48_case.h"
static volatile sig_atomic_t stop_requested;
static unsigned ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000);}
#include "bayer_front.h"
struct sink {unsigned rows;uint64_t bytes;};
static int accept(void *u,uint32_t row,const unsigned char *p,size_t n){
 struct sink *s=u;if(!p||row!=s->rows||n!=13216)return 0;s->rows++;s->bytes+=n;return 1;
}
int main(int argc,char **argv){
 if(argc!=4)return 2;
 unsigned i=(unsigned)strtoul(argv[1],NULL,10);if(i>=7)return 2;
 p48_source=&p48_sources[i];char src[1024],ref[1024];
 snprintf(src,sizeof src,"%s/%s",argv[2],p48_source->asset_file);
 snprintf(ref,sizeof ref,"%s/%s",argv[2],p48_source->row_file);
 struct p32_front f;struct sink s={0};
 if(!p32_front_open(&f,src,ref,argv[3],stdout)){fprintf(stderr,"open:%s\n",f.error);return 1;}
 int ok=p32_front_write(&f,&s,accept);
 if(!ok)fprintf(stderr,"write:%s\n",f.error);
 ok=ok&&f.verified&&s.rows==4384&&s.bytes==57938944;
 if(!p32_front_close(&f))ok=0;
 printf("SOURCE=%s rows=%u bytes=%llu success=%d\n",p48_source->id,s.rows,(unsigned long long)s.bytes,ok);
 return ok?0:1;
}
