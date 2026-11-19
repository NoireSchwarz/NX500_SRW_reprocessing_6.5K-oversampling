/* SPDX-License-Identifier: AGPL-3.0-only. Defined native CrCbY math.
 * Two graded looks retain metol M3 independent-strength normalization.
 * Hue=3 degrees/step; contrast=5 percent/step around128; neutral is0.
 * Sharpness is returned as a spatial ISP offset, never baked into this LUT.
 */
#ifndef NXL_LUT_PARAMETERS_H
#define NXL_LUT_PARAMETERS_H
struct lp_params {int strength,hue,sharp,contrast;};
static const int lp_cos[11]={16384,16362,16294,16182,16026,15826,15582,15296,14968,14599,14189};
static const int lp_sin[11]={0,857,1713,2563,3406,4240,5063,5872,6664,7438,8192};
static int lp_round(int n,int d){return n<0?-((-n+d/2)/d):(n+d/2)/d;}
static int lp_byte(int n){return n<0?0:n>255?255:n;}
static int lp_valid(const struct lp_params*p){return p->strength>=0&&p->strength<=100&&p->hue>=-10&&p->hue<=10&&p->sharp>=-10&&p->sharp<=10&&p->contrast>=-10&&p->contrast<=10;}
static void lp_grade(const unsigned char*src,const struct lp_params*p,int v[3]){
 int angle=p->hue<0?-p->hue:p->hue,cs=lp_cos[angle],sn=lp_sin[angle]*(p->hue<0?-1:1),cr=src[0]-128,cb=src[1]-128;
 v[0]=lp_byte(128+lp_round(cr*cs-cb*sn,16384));v[1]=lp_byte(128+lp_round(cr*sn+cb*cs,16384));v[2]=lp_byte(128+lp_round((src[2]-128)*(100+5*p->contrast),100));
}
static void lp_pixel(const unsigned char*base,const unsigned char*a,const unsigned char*b,const struct lp_params*pa,const struct lp_params*pb,unsigned char*out){
 int sum=pa->strength+pb->strength,r=sum?(pb->strength*100+sum/2)/sum:0,s=sum>100?100:sum,x[3],y[3];lp_grade(a,pa,x);lp_grade(b,pb,y);
 for(unsigned k=0;k<3;k++){int mid=(x[k]*(100-r)+y[k]*r+50)/100;out[k]=(unsigned char)((base[k]*(100-s)+mid*s+50)/100);}out[3]=0;
}
static int lp_sharp(const struct lp_params*a,const struct lp_params*b){int sum=a->strength+b->strength;return lp_round(a->strength*a->sharp+b->strength*b->sharp,sum>100?sum:100);}
#endif
