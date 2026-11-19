from context import *
import ast,zipfile
sys.path[:0]=[str(PY),str(T/'base/nx-lut-lite/tools'),str(SRC/'tools/log_video')]
import numpy as np
from scipy.stats import qmc
from native_codec import GRID
from samsung_standard import read_cube,sample_cube
from vlog_standard import input_to_working,WORKING_GAIN
tree=ast.parse((A/'lut_assets.py').read_text('utf8'));node=next(n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name=='native_sample')
ns=dict(np=np,GRID=GRID);exec(compile(ast.Module(body=[node],type_ignores=[]),'independent-native-sampler','exec'),ns);sample_native=ns['native_sample']
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'))['looks']
scene=qmc.Sobol(3,scramble=True,seed=1119).random_base2(14)**2.2
signal=input_to_working(scene);axis=np.linspace(0,1,4096);gray=input_to_working(np.stack([axis]*3,axis=-1));rows=[]
for row in catalog:
 if row['input']!='VLOG':continue
 source=Path(row['source']);source=source if source.is_absolute()else W/'inputs'/source
 assert source.is_file()and sha(source)==row['source_sha256']
 cube=read_cube(source);native=(S/'looks'/(row['id']+'.bin')).read_bytes()
 result={}
 for name,rgb in [('modeled_camera_RGB',signal),('neutral_ramp',gray)]:
  error=(sample_native(native,rgb)-np.clip(sample_cube(cube,rgb),0,1))*255
  result[name]=dict(points=len(rgb),MAE8=float(abs(error).mean()),RMS8=float(np.sqrt(np.mean(error**2))),P95max8=float(np.quantile(abs(error).max(axis=1),.95)),worst8=float(abs(error).max()))
 rows.append(dict(id=row['id'],**result))
with zipfile.ZipFile(OLD)as z:
 oldlooks=[n for n in z.namelist()if'/looks/'in n and n.endswith('.bin')]
put(W/'LUT-quality-domain.json',dict(passed=True,modeled_not_camera_measured=True,scene_model='nonnegative base RGB 0..1, verified working gamut matrix and working exposure',
 working_gain=WORKING_GAIN,physical_gray_certified=False,input_code_min8=float(signal.min()*255),input_code_max8=float(signal.max()*255),looks=rows,
 full_RGB_cube_diagnostics_also_retained='lut-conversion-validation.json',native_grid=17,source_grid_up_to=65,
  original_PC_sources_retained=True,lossless_conversion=False,R8f_bundled_look_paths=oldlooks))
print('PASS source identities, explicit camera-domain numerical error report, original sources retained',flush=True)
