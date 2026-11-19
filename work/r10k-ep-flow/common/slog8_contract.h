/* SPDX-License-Identifier: AGPL-3.0-only. Scene-linear output composition.
 * Samsung 2025: BT.2020 / D65. Existing sensor/WB calibration stays live.
 * Both OEM CC_RGBL/CC_RGBH matrices are left-composed; R2Y/SE stay OEM.
 * Native halfwords are signed11 with opaque padding. No Q7 RGB array is
 * guessed or substituted: output-row composition is independent of the
 * native input basis. First-column neutral gains stay equal when equal.
 */
#ifndef NXL_SLOG8_CONTRACT_H
#define NXL_SLOG8_CONTRACT_H
#include <stdint.h>
#define SLOG8_PROFILE 0x33524753u
#define SLOG8_SCENE_WHITE_Q16 165174u
#define SLOG8_NATIVE_BLACK 0u
#define SLOG8_NATIVE_WHITE 16383u
#define SLOG8_COLOR_WORDS 10u
/* Work reference: factory pipeline linear full-scale18%, not a measured gray card. */
#define SLOG8_GRAY_INPUT 2965u
#define SLOG8_GRAY_TARGET_CODE8 161u
#define SLOG8_REFERENCE_BASIS "NX500_MIDGRAY_MATCH_R11N"
#define SLOG8_OUTPUT_GAIN_TEXT "1.0408323021957888"
#define SLOG8_OUTPUT_OFFSET8_TEXT "-0.8793405230977542"
/* This is an explicit sensor-relative exposure reference, not a measured
 * reflectance calibration or a promise of scene12 highlight capture. */
static const int32_t slog8_mix_q20[3][3]={{657881,345278,45417},{72454,964208,11914},{17188,92289,939099}};
static inline int32_t slog8_s11(uint32_t v){v&=2047u;return(v&1024u)?(int32_t)v-2048:(int32_t)v;}
static inline unsigned slog8_word_mask(unsigned i){return i%5u==4u?0x7ffu:0x07ff07ffu;}
static __attribute__((noinline)) int slog8_compose(uint32_t out[10],const uint32_t in[10]){
 for(unsigned i=0;i<10;i++)out[i]=in[i];
 for(unsigned group=0;group<2;group++){
  int32_t a[3][3];
  for(unsigned r=0;r<3;r++)for(unsigned c=0;c<3;c++){
   unsigned half=group*10u+r*3u+c;
   a[r][c]=slog8_s11(in[half/2u]>>((half&1u)*16u));
  }
  for(unsigned r=0;r<3;r++)if(a[r][0]<=0)return 0;
  for(unsigned r=0;r<3;r++)for(unsigned c=0;c<3;c++){
   int64_t sum=0;for(unsigned k=0;k<3;k++)sum+=(int64_t)slog8_mix_q20[r][k]*a[k][c];
   int32_t v=(int32_t)(sum>=0?(sum+524288)>>20:-((-sum+524288)>>20));
   if(v< -1024||v>1023)return 0;
   unsigned half=group*10u+r*3u+c,word=half/2u,shift=(half&1u)*16u;
   out[word]=(out[word]&~(2047u<<shift))|(((uint32_t)v&2047u)<<shift);
  }
 }
 for(unsigned i=0;i<10;i++)if((out[i]^in[i])&~slog8_word_mask(i))return 0;
 return 1;
}
#endif
