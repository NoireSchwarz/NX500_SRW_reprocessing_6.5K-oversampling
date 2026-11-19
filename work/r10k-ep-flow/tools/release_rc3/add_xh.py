from context import *
import shutil,ast
sys.path[:0]=[str(PY),str(SRC/'base/nx-lut-lite/tools'),str(SRC/'tools/log_video')]
import numpy as np
from native_codec import compile_uniform_rgb,GRID
from samsung_standard import read_cube,sample_cube
node=next(n for n in ast.parse((A.parent/'formal-rc1-authoring/lut_assets.py').read_text('utf8')).body if isinstance(n,ast.FunctionDef)and n.name=='native_sample')
ns=dict(np=np,GRID=GRID);exec(compile(ast.Module(body=[node],type_ignores=[]),'native-sampler','exec'),ns)
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'));rows=[]
originals=S/'PC_LUT_SOURCES/XH-5219';originals.mkdir()
spec=[('XH_5219_3.cube','xh_5219_bright','XH 5219 明亮','作者3號：明亮方向的5219配色。','適合日常與人像；普通／標準畫面輸入。'),
 ('XH_5219_7.cube','xh_5219_hasselblad','XH 5219 哈蘇風格','作者7號：哈蘇風格方向的5219配色。','適合人像與生活記錄；可與明亮版對照。')]
probes=np.random.default_rng(5219).random((8192,3))
for name,id,title,tone,scene in spec:
 assert not any(r['id']==id for r in catalog['looks'])
 p=Path(r'D:\Download')/name;cube=read_cube(p)
 assert cube.dimensions==3 and cube.size==33 and np.array_equal(cube.domain_min,[0,0,0])and np.array_equal(cube.domain_max,[1,1,1])and np.isfinite(cube.data).all()
 raw=compile_uniform_rgb(cube.data*255,cube.size,boundary='linear')
 assert len(raw)==19712 and not any(raw[19664:])and raw[19652:19664]==raw[19648:19652]*3
 shutil.copyfile(p,originals/name)
 for folder in [S/'looks',M/'looks',T/'base/nx-lut-lite/looks']:(folder/(id+'.bin')).write_bytes(raw)
 error=(ns['native_sample'](raw,probes)-np.clip(sample_cube(cube,probes),0,1))*255
 row=dict(id=id,name=title,tone=tone,scene=scene,group='portrait',input='STD',source=name,source_sha256=sha(p),source_grid=33,
  native_sha256=sha(M/'looks'/(id+'.bin')),native_grid=17,native_encoding='CrCbY-BT601-full-v1',approximation=False,numeric_resampling=True,
  input_confirmed_by_user='ordinary/standard',copyright_header='XH Copyright',original_PC_source='PC_LUT_SOURCES/XH-5219/'+name,physical_colour_matching_certified=False)
 catalog['looks'].append(row);rows.append(dict(id=id,source_sha256=row['source_sha256'],native_sha256=row['native_sha256'],native_table_bytes=len(raw),
  MAE8=float(abs(error).mean()),RMS8=float(np.sqrt(np.mean(error*error))),worst8=float(abs(error).max()),numerical_sample_count=len(probes),modeled_not_camera_measured=True))
U=T/'base/nx-lut-lite';put(U/'data/release-lut-catalog.json',catalog)
(U/'data/release-lut-allowlist.tsv').write_text(''.join(r['id']+'\t'+r['input']+'\t'+r['native_sha256']+'\n'for r in catalog['looks']),'ascii',newline='\n')
profiles=[{k:r[k]for k in ['id','name','tone','scene','group']}for r in catalog['looks']];put(U/'data/looks-info.json',profiles)
for name in ['release-lut-catalog.json','release-lut-allowlist.tsv','looks-info.json']:shutil.copyfile(U/'data'/name,M/'data'/name)
p=U/'src/look_info.h';s=p.read_text('utf8');begin=s.index('static const struct nxl_look_info nxl_look_infos[]={');q=lambda x:json.dumps(x,ensure_ascii=False)
p.write_text(s[:begin]+'static const struct nxl_look_info nxl_look_infos[]={\n'+''.join('{'+','.join(q(r[k])for k in ['id','name','tone','scene','group'])+'},\n'for r in profiles)+'};\n#endif\n','utf8',newline='\n')
assert len(catalog['looks'])==32 and sum(r['input']=='STD'for r in catalog['looks'])==25
put(W/'XH-conversion-validation.json',dict(passed=True,source_CUBEs_preserved=True,input_confirmed_by_user='STD',creator_style_labels_not_factory_calibration=True,
 looks=rows,catalog_count=32,ordinary_count=25,Log_count=7,hardware_tested=False))
print('PASS XH 5219 Bright and Hasselblad style; 32 LUTs total',flush=True)
