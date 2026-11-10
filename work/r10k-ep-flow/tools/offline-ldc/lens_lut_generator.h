/* SPDX-License-Identifier: AGPL-3.0-only.
 * Independent transcription of NX500 1.12 CLdcD5 main-basis construction.
 * Offline/portable kernel only; no device access, firmware blob or deployment.
 * Native lens floats required. MakerNote decimal pairs are a different domain.
 */
#ifndef NXL_RESEARCH_LENS_GENERATOR_H
#define NXL_RESEARCH_LENS_GENERATOR_H
#include <stdint.h>
#include <string.h>
struct nxl_lens_point { float k1, k2; };
static float nxl_fadd(float a,float b){volatile float v=a+b;return v;}
static float nxl_fmul(float a,float b){volatile float v=a*b;return v;}
static float nxl_fdiv(float a,float b){volatile float v=a/b;return v;}
static int nxl_finite(float a){uint32_t v;memcpy(&v,&a,4);return (v&0x7f800000u)!=0x7f800000u;}
static int nxl_lens_basis(struct nxl_lens_point p,int16_t out[137]){
 const float G=203.0625f;
 if(!nxl_finite(p.k1)||!nxl_finite(p.k2))return 0;
 float end=nxl_fmul(nxl_fadd(nxl_fmul(p.k2,G),p.k1),G);
 float scale=0;if(end>0)scale=nxl_fdiv(65536.0f,nxl_fadd(end,1.0f));
 for(unsigned i=0;i<137;i++){
  float g=(float)i*(203.0625f/128.0f),v;
  if(end>0){float z=nxl_fadd(nxl_fmul(nxl_fadd(nxl_fmul(g,p.k2),p.k1),g),1.0f);v=nxl_fadd(65536.0f,-nxl_fmul(z,scale));}
  else v=nxl_fmul(nxl_fmul(-65536.0f,g),nxl_fadd(nxl_fmul(p.k2,g),p.k1));
  double q=(double)v+0.5;
  if(!nxl_finite(v)||q<=-32769.0||q>=32768.0)return 0;
  out[i]=(int16_t)(int32_t)q;
 }return 1;
}
/* Returns1 on success; rejects unsupported alternate video basis and extents. */
static int nxl_lens_generate(const struct nxl_lens_point *points,unsigned count,
 uint32_t width,uint32_t height,int32_t zoom_q8,int16_t out[128],uint32_t *mode){
 if(!points||!out||!mode||!count||count>128||!width||!height||width>8191||height>8191||zoom_q8<0)return 0;
 if(width==1920||width==1728||height==1088||height==1152)return 0;
 uint32_t rr=3u*height<=2u*width?(13u*width*width)/36u:(13u*height*height)/16u;
 uint32_t log=0,n=rr;while(n>>1){n>>=1;log++;}
 unsigned subtract=width==3840?6u:5u;if(log<subtract)return 0;*mode=log-subtract;
 unsigned last=(rr>>*mode)+1u;if(last+2u>128u)return 0;
 unsigned index=(uint32_t)zoom_q8>>8;if(index>=count)index=count-1;
 unsigned alpha=(uint32_t)zoom_q8&255u,mix=index!=count-1&&alpha;
 int16_t first[137],next[137];
 if(!nxl_lens_basis(points[index],first)||(mix&&!nxl_lens_basis(points[index+1],next)))return 0;
 memset(out,0,256);
 for(unsigned i=0;i<=last;i++){
  unsigned j=(rr/2u+(i<<(*mode+7u)))/rr;if(j>=137)return 0;
  int32_t v=first[j];if(mix)v=((int32_t)(256-alpha)*v+(int32_t)alpha*next[j])/256;
  out[i]=(int16_t)v;
 }out[last+1u]=out[last];return 1;
}
#endif
