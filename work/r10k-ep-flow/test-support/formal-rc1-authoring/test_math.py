"""Execute the shipped ARM blend and pure UI flow, with independent oracles."""
from context import *
import math,re,struct
sys.path[:0]=[str(PY),str(SRC/'tools')]
from post_file_model import PostFileModel
from arm_model import ArmModel
V=next(W/('tests/math-'+str(i))for i in range(1,100)if not(W/('tests/math-'+str(i))).exists());V.mkdir(parents=True)
U=T/'base/nx-lut-lite/src'
src=V/'flow.c'
src.write_text(r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../build-tree/base/nx-lut-lite/src/lut_detail_flow.h"
#include "../../build-tree/base/nx-lut-lite/src/menu_srw_layout.h"
#include "../../build-tree/base/nx-lut-lite/src/ui_font.h"
#include "../../build-tree/base/nx-lut-lite/src/ui_i18n.h"
int main(void){unsigned n=0;struct ld_flow f;ld_begin(&f,7,3,75,1);
 assert(f.level==LD_A&&f.cursor==3&&f.a[0]==75);n++;
 assert(ld_key(&f,173)==LD_OPEN_B&&f.level==LD_B_LIST);f.b_count=4;n++;
 assert(ld_key(&f,96)==LD_EDIT_B&&f.level==LD_B_PARAMS&&!f.prepared);n++;
 assert(ld_key(&f,163)==LD_B_CHANGED&&f.b_parameter==1);n++;
 assert(ld_key(&f,177)==LD_B_CHANGED&&f.b[1]==1);n++;
 assert(ld_key(&f,96)==LD_ARM_B&&f.prepared&&f.level==LD_A);n++;
 assert(ld_key(&f,96)==LD_COMMIT);n++;
 assert(ld_key(&f,173)==LD_OPEN_B);assert(ld_key(&f,96)==LD_EDIT_B);
 assert(ld_key(&f,127)==LD_BACK&&f.level==LD_B_LIST);n++;
 assert(ld_key(&f,127)==LD_BACK&&f.level==LD_A);n++;
 assert(ld_key(&f,127)==LD_PARENT);n++;
 for(int p=0;p<4;p++){f.parameter=p;for(int i=0;i<100;i++)ld_key(&f,177);assert(f.a[p]==(p?10:100));for(int i=0;i<100;i++)ld_key(&f,178);assert(f.a[p]==(p?-10:0));n+=2;}
 assert(!NXL_LENS_TEST_UI&&nxl_srw_ui_index(9)<0&&nxl_srw_ui_index(13)<0&&nxl_srw_ui_index(10)<0);n++;
 for(int i=0;i<nxl_srw_ui_count();i++){assert(nxl_srw_ui_touch(i,104+(i%5)*56)==i);assert(nxl_srw_ui_touch(i,104+(i%5)*56+55)==i);n+=2;}
 for(unsigned i=0;i<sizeof nxl_ui_font/sizeof*nxl_ui_font;i++){if(i)assert(nxl_ui_font[i-1].cp<nxl_ui_font[i].cp);assert(nxl_ui_glyph(nxl_ui_font[i].cp)==nxl_ui_font+i);n++;}
 assert(strcmp(nxl_ui_text("COMPLETE"),"COMPLETE"));n++;
 printf("PASS actual ARM detail hierarchy, final OK, bounds, hidden diagnostics and glyphs checks=%u\n",n);return 0;}
'''.replace('../../build-tree/','../../build-tree/'),'utf8',newline='\n')
# This fixture lives at W/tests/math, so the relative path needs one more up.
src.write_text(src.read_text('utf8').replace('"../../build-tree/','"../../build-tree/'),'utf8',newline='\n')
host('test-flow',src,V/'flow.arm')
flow=ArmModel(V/'flow.arm').run();assert flow['exit']==0,flow

def table(seed):
 b=bytearray(19712)
 for i in range(4913):b[i*4:i*4+4]=bytes([(i*seed+7)%256,(i*13+seed)%256,(i*3+seed*5)%256,0])
 b[19652:19664]=b[19648:19652]*3
 return bytes(b)
base,a,b=[table(i)for i in [3,7,11]]
cs=[16384,16362,16294,16182,16026,15826,15582,15296,14968,14599,14189]
sn=[0,857,1713,2563,3406,4240,5063,5872,6664,7438,8192]
def rnd(n,d):return (1 if n>=0 else -1)*((abs(n)+d//2)//d)
def grade(v,p):
 cr,cb=v[0]-128,v[1]-128;c=cs[abs(p[1])];s=sn[abs(p[1])]*(1 if p[1]>=0 else -1)
 return [max(0,min(255,128+rnd(cr*c-cb*s,16384))),max(0,min(255,128+rnd(cr*s+cb*c,16384))),max(0,min(255,128+rnd((v[2]-128)*(100+5*p[3]),100)))]
def expected(pa,pb):
 total=pa[0]+pb[0];ratio=(pb[0]*100+total//2)//total if total else 0;s=min(total,100);out=bytearray(19712)
 for i in range(0,19652,4):
  aa,bb=grade(a[i:i+4],pa),grade(b[i:i+4],pb)
  for k in range(3):mid=(aa[k]*(100-ratio)+bb[k]*ratio+50)//100;out[i+k]=(base[i+k]*(100-s)+mid*s+50)//100
 out[19652:19664]=out[19648:19652]*3
 return bytes(out),rnd(pa[0]*pa[2]+pb[0]*pb[2],max(100,total))
cases=[((0,0,0,0),(0,0,0,0)),((100,0,0,0),(0,0,0,0)),((0,0,0,0),(100,0,0,0)),((100,0,0,0),(100,0,0,0)),
 ((80,0,0,0),(80,0,0,0)),((65,6,-4,3),(80,-8,7,-6)),((5,0,0,0),(0,0,0,0)),((50,-10,-10,-10),(50,10,10,10))]
for s in range(0,101,5):cases.append(((s,0,0,0),(0,0,0,0)))
results=[]
for i,(pa,pb)in enumerate(cases):
 root=V/('case-'+str(i));root.mkdir()
 for name,data in [('base.bin',base),('a.bin',a),('b.bin',b)]:(root/name).write_bytes(data)
 args=['/base.bin','/a.bin','/b.bin',*map(str,pa),*map(str,pb),'--output','/out.bin']
 r=PostFileModel(M/'bin/nxl-lutblend.arm',args,root).run();assert r['exit']==0,r
 want,sharp=expected(pa,pb);assert(root/'out.bin').read_bytes()==want
 assert 'sharp_offset='+str(sharp)in r['stdout']
 results.append(dict(A=pa,B=pb,passed=True,all4913nodes_byte_exact=True,sharp=sharp))
bad=PostFileModel(M/'bin/nxl-lutblend.arm',['none'],V).run();assert bad['exit']==2
put(W/'math-and-flow-validation.json',dict(passed=True,actual_ARM_blend_sha256=sha(M/'bin/nxl-lutblend.arm'),cases=results,flow=flow,
 M3_two_rounding_steps_checked=True,independent_integer_oracle=True,all_zero_factory_baseline=True,over100_normalized=True,
 no_hardware_executed=True,hardware_tested=False))
print('PASS',len(results),'actual ARM blend cases; all4913 nodes, bounds, final commit and glyph lookup',flush=True)
