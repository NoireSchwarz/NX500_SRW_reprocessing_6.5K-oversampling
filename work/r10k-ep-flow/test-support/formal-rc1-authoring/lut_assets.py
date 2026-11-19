"""Convert original LUT data on PC; preserve sources and input conventions."""
from context import *
import ctypes,re,shutil,zipfile
sys.path[:0]=[str(PY),str(T/'base/nx-lut-lite/tools'),str(T/'tools/log_video')]
import numpy as np
from scipy.stats import qmc
from native_codec import compile_uniform_rgb,NATIVE,GRID,slotwrap
from samsung_standard import read_cube,sample_cube

def trad(s):
    n=ctypes.windll.kernel32.LCMapStringEx('zh-TW',0x04000000,s,len(s),None,0,None,None,0)
    if not n:return s
    b=ctypes.create_unicode_buffer(n)
    assert ctypes.windll.kernel32.LCMapStringEx('zh-TW',0x04000000,s,len(s),b,n,None,None,0)==n
    return b.value

L=W/'converted-luts';L.mkdir(exist_ok=True)
P=S/'PC_LUT_SOURCES';P.mkdir(exist_ok=True)
shutil.copytree(W/'inputs',P/'provided',dirs_exist_ok=True)
catalog=[];measurements=[]
oldinfo={r['id']:r for r in json.loads((T/'base/nx-lut-lite/data/looks-info.json').read_text('utf8'))}
names=['fuji_provia100f','kodak_portra400','mono_hp5']
for name in names:
 p=B/'nx-lut-demo/nx-lut-lite/looks'/(name+'.bin');data=p.read_bytes()
 assert len(data)==19712 and not any(data[19664:])
 (L/p.name).write_bytes(data)
 info=oldinfo[name]
 catalog.append(dict(id=name,name=info['name'],tone=trad(info['tone']),scene=trad(info['scene']),group=info['group'],
                     input='STD',source=str(p),source_sha256=sha(p),native_sha256=sha(L/p.name),approximation=False))

skin=Path(r'D:\Download\NX-LUT-Portrait-5219-Pack-v1\looks\ejay_skin_native.bin')
assert sha(skin)=='58be302e3e007fd66d171a3eba218ed891d550f08660ea5031f59e9efbb9f352'
shutil.copyfile(skin,L/skin.name)
catalog.append(dict(id='ejay_skin_native',name='EJAY 膚色最佳化',tone='選擇性調整紅橙色區，同色物件亦受影響。',
 scene='保留其他色調；不含人臉識別或磨皮。',group='portrait',input='STD',source=str(skin),source_sha256=sha(skin),native_sha256=sha(L/skin.name),approximation=False))

rows=[('std+5207n（66位后期不用导入）.cube','ejay_5207_n','5207 Noritsu','諾日士沖掃方向，暖調日光電影配色。','日光人像與街景；原表標準畫面輸入。'),
 ('std+5207n+fx（66位后期不用导入）.cube','ejay_5207_n_fx','5207 Noritsu FX','諾日士配色加藍色增強，暗部去紅。','適合藍天與海景；固定合適白平衡比較。'),
 ('std+5207s（66位后期不用导入）.cube','ejay_5207_s','5207 SP3000','SP3000沖掃方向，原表帶青色修正。','日光電影配色；與Noritsu版本比較。')]
pending=[('raw+g200fxn.cube','ejay_g200_fx_n','Gold 200 FX 暖','Gold 200配色，FX藍色增強與暖調版本。'),
 ('raw+g200nfxl.cube','ejay_g200_n_fx_l','Gold 200 Noritsu FX 冷','Gold 200諾日士配色，FX藍色增強與冷調版本。')]
sources={}
def convert(filename,name,title,tone,scene,input_kind='STD',group='cinema'):
 p=W/'inputs'/filename;c=read_cube(p)
 assert c.dimensions==3 and c.size in [33,64,65]
 assert np.array_equal(c.domain_min,[0,0,0]) and np.array_equal(c.domain_max,[1,1,1])
 assert np.all(np.isfinite(c.data))
 raw=compile_uniform_rgb(c.data*255,c.size,boundary='linear')
 assert len(raw)==19712 and raw[19652:19664]==raw[19648:19652]*3 and not any(raw[19664:])
 (L/(name+'.bin')).write_bytes(raw)
 sources[name]=c
 row=dict(id=name,name=title,tone=tone,scene=scene,group=group,input=input_kind,source=filename,
          source_sha256=sha(p),source_grid=c.size,filename_grid_claim_is_not_used=True,
          native_sha256=sha(L/(name+'.bin')),native_grid=17,native_encoding=NATIVE,approximation=False,
          numeric_resampling=True,physical_colour_matching_certified=False)
 catalog.append(row)
 return c
for row in rows:convert(*row)
for filename,name,title,tone in pending:
 convert(filename,name,title,tone,'作者用於普通／標準畫面；依使用者確認分類。',input_kind='STD',group='retro')
log_rows=[('vlog+5207n（66位后期不用导入）.cube','vlog_ejay_5207_n','VLog 5207 Noritsu','諾日士沖掃方向的日光電影配色。'),
 ('vlog+5207n+fx（66位后期不用导入）.cube','vlog_ejay_5207_n_fx','VLog 5207 Noritsu FX','諾日士配色加FX藍色增強。'),
 ('vlog+5207s+（66位电脑后期用）.cube','vlog_ejay_5207_s','VLog 5207 SP3000','SP3000沖掃方向的5207配色。'),
 ('vlog g200nfxl 66.cube','vlog_ejay_g200_n_fx_l','VLog Gold 200 FX 冷','Gold 200諾日士配色，FX藍色增強與冷調。'),
 ('vlog g200fxn 65.cube','vlog_ejay_g200_fx_n','VLog Gold 200 FX 暖','Gold 200配色，FX藍色增強與暖調。')]
added=[]
for filename,name,title,tone in log_rows:
 p=Path(r'D:\Download')/filename
 if p.is_file():
  shutil.copyfile(p,W/'inputs'/filename);shutil.copyfile(p,P/'provided'/filename)
  added.append(dict(name=filename,source=str(p),sha256=sha(p),bytes=p.stat().st_size,user_confirmed_input='V-Log'))
  convert(filename,name,title,tone,'作者原版V‑Log LUT；僅從VID的Log入口選擇。',input_kind='VLOG',group='loglut')
put(W/'log-input-provenance.json',added)

# The provided STD originals are not relabelled as V-Log. These separate
# derivatives explicitly perform official V709 rendering before the look.
official=W.parent/'r11q-vlog-work/PC_LUTs/NX500_VLog_to_V709_FULL.cube'
v709=read_cube(official)
axis=np.linspace(0,1,33);grid=np.stack(np.meshgrid(axis,axis,axis,indexing='ij'),axis=-1)[...,::-1].reshape(-1,3)
# The user will provide the creator's real V-Log versions. Do not substitute
# our own STD->V709 compositions or label STD files as V-Log originals.
for _,name,*_ in rows:
 p=S/'looks'/('vlog_'+name+'.bin')
 if p.exists():
  assert W.resolve()in p.resolve().parents and sha(p)==sha(L/p.name)
  p.unlink() # only our isolated, unpublished exploratory derivative
raw=compile_uniform_rgb(v709.data*255,v709.size,boundary='linear');(L/'vlog_v709.bin').write_bytes(raw)
catalog.append(dict(id='vlog_v709',name='V‑Log → V709',tone='松下官方V709還原，輸出展開為滿範圍。',
 scene='V‑Log／V‑Gamut專用；17點機內重採樣版本。',group='loglut',input='VLOG',
 source=str(official),source_sha256=sha(official),native_sha256=sha(L/'vlog_v709.bin'),approximation=False))

# Keep the historical Samsung table as a documented PC reference, never
# offer it as a V-Log compatible camera look.
legacy=P/'legacy-samsung';legacy.mkdir(exist_ok=True)
shutil.copyfile(B/'looks/SLOG_REC709.bin',legacy/'SLOG_REC709.bin')
(legacy/'README_zh.txt').write_text('歷史Samsung Log還原表，僅供舊素材取證；不可用於新的V‑Log。\n','utf8')

# Independent native interpolation on random in-range RGB, with explicit
# native codec boundary semantics. No camera fidelity is inferred.
def native_sample(raw,rgb):
 rgb=np.asarray(rgb);r,g,b=(rgb[:,i]*255 for i in range(3))
 p=np.stack([np.clip(128+.5*r-.418688*g-.081312*b,0,255),np.clip(128-.168736*r-.331264*g+.5*b,0,255),np.clip(.299*r+.587*g+.114*b,0,255)],axis=1)
 i=np.minimum((p//16).astype(int),15);a=np.array(GRID);f=(p-a[i])/(a[i+1]-a[i]);out=np.zeros_like(p)
 table=np.frombuffer(raw[:19652],np.uint8).reshape(17,17,17,4)
 for dz in [0,1]:
  for dy in [0,1]:
   for dx in [0,1]:
    step=np.array([dx,dy,dz]);w=np.prod(np.where(step,f,1-f),axis=1);j=i+step
    out+=table[j[:,2],j[:,1],j[:,0],:3]*w[:,None]
 cr,cb,y=out[:,0]-128,out[:,1]-128,out[:,2]
 return np.clip(np.stack([y+1.402*cr,y-.714136*cr-.344136*cb,y+1.772*cb],axis=1),0,255)/255

probes=qmc.Sobol(3,scramble=True,seed=4401).random_base2(14)
for row in catalog:
 if row['id']not in sources:continue
 c=sources[row['id']];raw=(L/(row['id']+'.bin')).read_bytes()
 e=(native_sample(raw,probes)-np.clip(sample_cube(c,probes),0,1))*255
 measurements.append(dict(id=row['id'],independent_points=len(probes),MAE8=float(abs(e).mean()),RMS8=float(np.sqrt(np.mean(e*e))),
  P95_max_channel8=float(np.quantile(abs(e).max(axis=1),.95)),worst8=float(abs(e).max()),
  original_source_preserved=True,not_camera_measurements=True))

active=[r for r in catalog if r['input']!='PENDING']
for row in active:shutil.copyfile(L/(row['id']+'.bin'),S/'looks'/(row['id']+'.bin'))
# Only this new candidate's public root removes legacy/test table entries.
for name in ['SLOG_REC709.bin','SLOG_REC709.native.json']:
 p=S/'looks'/name
 if p.exists():p.unlink()
data=T/'base/nx-lut-lite/data';put(data/'release-lut-catalog.json',dict(format='NXL_RELEASE_LUTS1',looks=catalog))
tsv=''.join(f"{r['id']}\t{r['input']}\t{r['native_sha256']}\n"for r in active)
(data/'release-lut-allowlist.tsv').write_text(tsv,'ascii',newline='\n')
profiles=[{k:r[k]for k in ['id','name','tone','scene','group']}for r in active]
put(data/'looks-info.json',profiles)
put(W/'lut-conversion-validation.json',dict(passed=True,source_files_copied_byte_exact=True,catalog=catalog,active_count=len(active),
 pending_ids=[r['id']for r in catalog if r['input']=='PENDING'],source_cube_sizes={r['id']:r.get('source_grid')for r in catalog if r.get('source_grid')},
 independent_native_interpolation=measurements,synthetic_apx_looks_excluded=True,SD_written=False,hardware_tested=False))
print(json.dumps({'passed':True,'active_looks':len(active),'pending_raw':[r['id']for r in catalog if r['input']=='PENDING'],
 'source_grids':{r['id']:r.get('source_grid')for r in catalog if r.get('source_grid')},'resampling_errors':measurements},ensure_ascii=False),flush=True)
