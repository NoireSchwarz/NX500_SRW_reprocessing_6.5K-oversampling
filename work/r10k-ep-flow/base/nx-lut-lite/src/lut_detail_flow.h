/* SPDX-License-Identifier: AGPL-3.0-only. UI hierarchy, no file/device I/O. */
#ifndef NXL_LUT_DETAIL_FLOW_H
#define NXL_LUT_DETAIL_FLOW_H
enum {LD_A,LD_B_LIST,LD_B_PARAMS};
enum {LD_NONE,LD_A_CHANGED,LD_B_CHANGED,LD_B_CATEGORY,LD_OPEN_B,LD_EDIT_B,LD_ARM_B,LD_COMMIT,LD_BACK,LD_PARENT,LD_LIVE};
struct ld_flow {int level,count,cursor,parameter,b_count,b_cursor,b_category,b_parameter,prepared;int a[4],b[4];};
static int ld_wrap(int n,int count){return count>0?(n%count+count)%count:0;}
static int ld_change(int*p,int parameter,int direction){int step=parameter?1:5,low=parameter?-10:0,high=parameter?10:100,n=*p+direction*step;if(n<low)n=low;if(n>high)n=high;if(n==*p)return 0;*p=n;return 1;}
static void ld_begin(struct ld_flow*s,int count,int cursor,int strength,int group){memset(s,0,sizeof *s);s->count=count;s->cursor=ld_wrap(cursor,count);s->a[0]=strength;s->b[0]=100;s->b_category=group;}
static int ld_key(struct ld_flow*s,unsigned code){
 if(code==125||code==126||code==83)return LD_LIVE;
 if(code==127){if(s->level==LD_A)return LD_PARENT;s->level--;return LD_BACK;}
 if(s->level==LD_A){
  if(code==173){s->level=LD_B_LIST;return LD_OPEN_B;}
  if(code==96)return LD_COMMIT;
  if(code==75||code==77||code==172||code==183){s->parameter=ld_wrap(s->parameter+(code==75?-1:1),4);return LD_NONE;}
  if(code==177||code==178)return ld_change(s->a+s->parameter,s->parameter,code==177?1:-1)?LD_A_CHANGED:LD_NONE;
  if(code==72||code==80||code==165||code==163){s->cursor=ld_wrap(s->cursor+((code==80||code==163)?1:-1),s->count);return LD_A_CHANGED;}
 }else if(s->level==LD_B_LIST){
  if(code==96&&s->b_count){s->level=LD_B_PARAMS;return LD_EDIT_B;}
  if(code==75||code==77||code==177||code==178){s->b_category=ld_wrap(s->b_category+((code==77||code==177)?1:-1),8);return LD_B_CATEGORY;}
  if(code==72||code==80||code==165||code==163){s->b_cursor=ld_wrap(s->b_cursor+((code==80||code==163)?1:-1),s->b_count);return LD_B_CHANGED;}
 }else{
  if(code==96){s->prepared=1;s->level=LD_A;return LD_ARM_B;}
  if(code==72||code==80||code==165||code==163){s->b_parameter=ld_wrap(s->b_parameter+((code==80||code==163)?1:-1),4);return LD_B_CHANGED;}
  if(code==75||code==77||code==177||code==178)return ld_change(s->b+s->b_parameter,s->b_parameter,(code==77||code==177)?1:-1)?LD_B_CHANGED:LD_NONE;
 }
 return LD_NONE;
}
#endif
