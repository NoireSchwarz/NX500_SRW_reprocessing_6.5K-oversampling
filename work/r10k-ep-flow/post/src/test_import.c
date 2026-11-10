#define main production_main
#include "hw-post.c"
#undef main
#include <assert.h>
int main(int argc,char **argv){
 assert(argc==2);char y[1200],uv[1200],out[1200];snprintf(y,sizeof y,"%s/y",argv[1]);snprintf(uv,sizeof uv,"%s/uv",argv[1]);snprintf(out,sizeof out,"%s/out",argv[1]);
 unsigned char a[32*4],b[32*4],got[16*4*2];memset(a,0xee,sizeof a);memset(b,0xdd,sizeof b);
 for(unsigned row=0;row<4;row++)for(unsigned x=0;x<16;x++){a[row*32+x]=row*16+x;b[row*32+x]=(x&1)?200-row*7:20+row*9;}
 assert(write_new(y,a,sizeof a)&&write_new(uv,b,sizeof b));int fy=open_regular(y,sizeof a),fu=open_regular(uv,sizeof b);assert(fy>=0&&fu>=0);
 struct cfg c={.w=16,.h=4,.stride=32};assert(import422(fy,fu,&c,out));assert(!import422(fy,fu,&c,out));
 assert(exact_file(out,got,sizeof got));for(unsigned i=0;i<64;i++)assert(got[i]==i);
 for(unsigned row=0;row<4;row++)for(unsigned x=0;x<16;x++)assert(got[64+row*16+x]==b[row*32+x]);
 char hash[65];assert(fd_hash(fy,hash,NULL)&&strlen(hash)==64);assert(open_regular(y,sizeof a+1)==-1);
 puts("PASS padding removed from both planes; all4/4 UV rows and CbCr bytes preserved; no averaging; collision and wrong input size rejected");close(fy);close(fu);return 0;
}
