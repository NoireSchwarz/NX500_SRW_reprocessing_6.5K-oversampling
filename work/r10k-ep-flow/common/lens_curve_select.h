/* SPDX-License-Identifier: AGPL-3.0-only.
 * Source-bound, PC-precomputed full-size LDC lookup. No floating-point
 * generator or current-lens query runs in the camera pipeline.
 */
#ifndef NXL_LENS_CURVE_SELECT_H
#define NXL_LENS_CURVE_SELECT_H
#include "../core/src/sha256.h"
#include "lens_curve_lut.h"
#define LC_MAGIC 0x3147434cu
#define LC_VERSION 2u
#define LC_EXACT 1u
#define LC_NOTE 2u
#define LC_FOCAL 3u
#ifndef LC_MODEL_CHECK_BYPASS
#define LC_MODEL_CHECK_BYPASS 0u
#endif
static void lc_u32(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void lc_sha(const void*p,unsigned n,char out[68]){unsigned char d[32];digest_bytes(p,n,d);memset(out,0,68);digest_hex(d,out);}
static int lc_lens_route(const struct lp_key*k,unsigned*bypass){
 *bypass=0;
 if(k->schema!=1||memcmp(k->sensor,lp_catalog[0].key.sensor,sizeof k->sensor))return 0;
 /* Add future prime lenses as independent routes with their OWN tables.
  * Do not infer a30mm LensType number or reuse the zoom's30mm table. */
 switch(k->lens){case 13u:return 1u;default:
  if(LC_MODEL_CHECK_BYPASS){*bypass=1;return 1u;}return 0;
 }
}
static int lc_resolve(const struct lp_key*key,struct lp_config*out,const char**reason){
 struct lp_key source=*key;memset(out,0,sizeof *out);out->key=source;
 if(reason)*reason="SRW_LDC_LENS_MODEL_NOT_SUPPORTED";
 unsigned bypass=0,route=(unsigned)lc_lens_route(&source,&bypass);if(!route)return 0;
 if(source.focal35x10<LC_FOCAL_MIN||source.focal35x10>LC_FOCAL_MAX){if(reason)*reason="SRW_LDC_FOCAL_OUTSIDE_LUT";return 0;}
 unsigned index=source.focal35x10-LC_FOCAL_MIN,method=LC_FOCAL;
 unsigned lo=lc_focal_anchors[index][0],hi=lc_focal_anchors[index][1],alpha=lc_focal_anchors[index][2];
 const unsigned char*recorded=NULL;unsigned profile=1000u+index;
 for(unsigned i=0;i<LC_CALIBRATION_COUNT;i++)if(i!=7u&&!memcmp(&source,&lp_catalog[i].key,sizeof source)){
  recorded=lp_catalog[i].packet;profile=lo=hi=i;alpha=0;method=LC_EXACT;break;
 }
 if(!recorded&&source.focal35x10==330u)method=LC_NOTE;
 unsigned char*p=out->source_packet;
 if(recorded)memcpy(p,recorded,272);
 else{lc_u32(p,0x80713a18u);memcpy(p+4,lc_focal_tables[index],256);lc_u32(p+260,3240);lc_u32(p+264,2160);lc_u32(p+268,18);}
 out->magic=LP_MAGIC;out->schema=2;out->bytes=sizeof *out;out->profile_index=profile;
 if(method==LC_EXACT)memcpy(out->profile_key,lp_catalog[profile].profile_key,65);else lc_sha(&source,sizeof source,out->profile_key);
 lc_sha(p+4,256,out->table_sha256);
 uint32_t*r=out->reserved;r[0]=LC_MAGIC;r[1]=method;r[2]=method!=LC_EXACT;r[3]=lo;r[4]=hi;r[5]=alpha;
 r[6]=route;r[7]=bypass;r[8]=source.focal35x10;r[9]=LC_VERSION;r[10]=18;r[11]=index;
 if(reason)*reason="OK";return 1;
}
static const char*lp_method(const struct lp_config*c){return c->schema==1?"recorded-table":c->reserved[1]==LC_EXACT?"recorded-table-lookup":c->reserved[1]==LC_NOTE?"precomputed-note-table":"precomputed-focal-table";}
static unsigned lp_approximate(const struct lp_config*c){return c->schema==2?c->reserved[2]:0;}
#endif
