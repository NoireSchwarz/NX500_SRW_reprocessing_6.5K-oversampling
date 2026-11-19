"""Reproducible, neutral-gamut Log reload fixtures; never accesses a camera.

NXLOG_TEST_V1 is *post-tone SDR recoding*, assuming sRGB input. It is not a
sensor Log mode or Samsung Log compatibility claim. Native Gamma candidates
are separate research artifacts and must never be put in the LUT slot.
"""
from pathlib import Path
import argparse, hashlib, json, math, struct, sys
import numpy as np

A1,B1,C1=.258984868,.0003645,.720504856
A2,B2,C2=-.20942,.016904,-.24597

def log_encode(x):
    x=np.asarray(x,dtype=float)
    if not np.all(np.isfinite(x)):raise ValueError('nonfinite input')
    out=np.zeros_like(x);upper=x>=.01;lower=(x>=-.05)&~upper
    out[upper]=A1*np.log10(x[upper]+B1)+C1
    out[lower]=A2*np.log10(-x[lower]+B2)+C2
    return out

def log_decode(y):
    y=np.asarray(y,dtype=float)
    if not np.all(np.isfinite(y)):raise ValueError('nonfinite input')
    upper=y>=.206561909
    return np.where(upper,10**((y-C1)/A1)-B1,
                    np.where(y>=0,-10**((y-C2)/A2)+B2,-.05))

def srgb_decode(x):
    x=np.clip(x,0,1)
    return np.where(x<=.04045,x/12.92,((x+.055)/1.055)**2.4)

def srgb_encode(x):
    x=np.clip(x,0,1)
    return np.where(x<=.0031308,12.92*x,1.055*x**(1/2.4)-.055)

def forward(x):return log_encode(srgb_decode(np.asarray(x,dtype=float)))
def inverse(x):return srgb_encode(log_decode(np.asarray(x,dtype=float)))
def sha(b):return hashlib.sha256(b).hexdigest()

def cube(path,func,title,edge=33):
    with path.open('w',encoding='ascii',newline='\n') as f:
        f.write('TITLE "'+title+'"\n')
        f.write('# Test only: assumes sRGB-coded input; native video input curve is uncalibrated.\n')
        f.write('# Neutral gamut; no BT.2020 conversion; no highlight recovery or added bit depth.\n')
        f.write(f'LUT_3D_SIZE {edge}\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n')
        for b in range(edge):
            for g in range(edge):
                for r in range(edge):
                    out=func(np.array([r,g,b])/(edge-1))
                    f.write(' '.join(f'{v:.10f}' for v in out)+'\n')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--repo',required=True,type=Path)
    ap.add_argument('--out',required=True,type=Path);ap.add_argument('--history',required=True,type=Path)
    a=ap.parse_args();root=a.repo/'work/r10k-ep-flow'
    sys.path.insert(0,str(root/'base/nx-lut-lite/tools'))
    from native_codec import GRID,SLOT,TABLE,NATIVE,rgb_to_ycc,ycc_to_rgb,slotwrap,sample
    from make_strengths import variant,validate,fnv32
    a.out.mkdir(parents=True,exist_ok=True);looks=a.out/'deploy/looks';looks.mkdir(parents=True,exist_ok=True)
    research=a.out/'native-gamma-NOT-DEPLOYED';research.mkdir(exist_ok=True)
    tables={};encoding={'version':1,'tables':{}}
    for name in ['NXLOG_REF_V1','NXLOG_TEST_V1']:
        buf=bytearray()
        for y in GRID:
            for cb in GRID:
                for cr in GRID:
                    if name=='NXLOG_REF_V1':values=(cr,cb,y)
                    else:
                        rgb=np.array(ycc_to_rgb((cr,cb,y),clip=True))/255
                        values=rgb_to_ycc(forward(rgb)*255)
                    buf.extend((*[int(float(v)+.5) for v in values],0))
        raw=slotwrap(buf);validate(raw);assert len(raw)==SLOT
        (looks/(name+'.bin')).write_bytes(raw)
        cube(looks/(name+'.cube'),lambda x:x if name=='NXLOG_REF_V1' else forward(x),name+' - POST SDR RELOAD TEST')
        cache=looks/'nxl-strengths'/name;cache.mkdir(parents=True,exist_ok=True)
        (cache/'source.bin').write_bytes(raw)
        strengths={}
        for level in [25,50,75]:
            b=variant(raw,level);validate(b)
            (cache/f'{level}.bin').write_bytes(b);(cache/f'{level}.fnv').write_text(f'{fnv32(b):08x}\n',encoding='ascii',newline='\n')
            strengths[str(level)]={'sha256':sha(b),'fnv32':f'{fnv32(b):08x}'}
        encoding['tables'][name+'.bin']={'space':NATIVE,'sha256':sha(raw)}
        tables[name]={'sha256':sha(raw),'bytes':len(raw),'strengths':strengths}
    (looks/'encoding.json').write_text(json.dumps(encoding,indent=2)+'\n',encoding='utf8')
    cube(a.out/'NXLOG_TEST_V1_to_sRGB_VIEW_ONLY.cube',inverse,'NXLOG TEST inverse - VIEW ONLY; do not load on camera',65)
    samples=np.r_[np.linspace(-.0499,.00999,3000),np.geomspace(.0101,12,3000)]
    roundtrip=float(np.max(abs(log_decode(log_encode(samples))-samples)));assert roundtrip<1e-10
    anchors=[{'scene':v,'code10':round(float(log_encode(v))*1023),'code8':round(float(log_encode(v))*255)} for v in [0,.01,.18,.9,1,12]]
    assert [r['code10'] for r in anchors[:4]]==[128,211,540,725]
    x=np.arange(256);neutral=[]
    raw=(looks/'NXLOG_TEST_V1.bin').read_bytes()
    for v in x:neutral.append(sample(raw,(128,128,int(v)))[2])
    neutral=np.array(neutral);assert np.all(np.diff(neutral)>0)
    neutral_error=float(np.max(abs(neutral-forward(x/255)*255)));assert neutral_error<2.6
    recovered=inverse(neutral/255)*255
    dense=np.linspace(0,1,10001);assert np.max(abs(inverse(forward(dense))-dense))<2e-6
    ref=(looks/'NXLOG_REF_V1.bin').read_bytes()
    rng=np.random.default_rng(112026);identity_error=0.;errors=[]
    for rgb in rng.uniform(0,255,(4096,3)):
        ycc=rgb_to_ycc(rgb);identity_error=max(identity_error,max(abs(a-b) for a,b in zip(sample(ref,ycc),ycc)))
        got=np.array(ycc_to_rgb(sample(raw,ycc),clip=False));want=forward(rgb/255)*255
        errors.append(float(np.max(abs(got-want))))
    assert identity_error<1e-10
    # Re-execute the exact firmware calculator and DDR writer for old, constrained candidates.
    from gamma_replay import GammaModel
    past=json.loads(a.history.read_text('utf8'));fits=[x for x in past['fits'] if x['maximum_X_gap_constraint']==4095]
    native=[]
    for fit in fits:
        assert len(fit['X'])==len(fit['Y'])==19 and max(np.diff(fit['X']))<=4095
        for bank in [0,1]:
            model=GammaModel(a.repo/'work/firmware/P7.bin',a.repo/'work/tooling/python')
            data=model.calculate(bank,fit['X'],fit['Y']);model.submit(bank)
            assert model.effective[1:20]==fit['X'] and model.effective[20:39]==fit['Y']
            fname=f'Scene{fit["white_scene_reflection"]}_bank{bank}_Gamma432_NOT_LUT.bin'
            (research/fname).write_bytes(data)
            native.append({'file':fname,'bank':bank,'X':fit['X'],'Y':fit['Y'],'bytes':432,'sha256':sha(data),'nodes_preserved':True,'ideal_interpolation_max_error_10bit':fit['max_abs_10bit_codes'],'camera_tested':False})
    result={'passed':True,'fixture':'NXLOG_TEST_V1','scope':'Stopped recording, same boot, existing LUT engine only',
      'input_assumption':'sRGB-coded SDR; actual NX500 video input EOTF is not calibrated',
      'gamut':'unchanged; no sensor or BT2020 calibration claimed','scene_range_assumed':[0,1],
      'curve_constants_source':'Existing 2026-11-08 project research; no new official compliance claim',
      'native_slot_space':NATIVE,'codec_domain':'Cr,Cb,Y 17x17x17; RGB channel recoding inside LUT',
      'anchors_scene_referred':anchors,'post_LUT_black_white_codes8':[float(neutral[0]),float(neutral[-1])],
      'original_P7_sha256':'f8e417cb5b55768851ba7e2b8017d3a98dc0b0c8444cc0a5061265cc08dbbf7f',
      'native_Gamma_calculator':'0x8038f90c','native_Gamma_DDR_writer':'0x8049e4d4',
      'native_neutral_plus_8bit_quantization_plus_inverse65cube_max_SDR_error_codes8':float(np.max(abs(np.interp(np.rint(neutral)/255,np.linspace(0,1,65),inverse(np.linspace(0,1,65)))*255-x))),
      'formula_roundtrip_max_scene_error':roundtrip,'native_neutral_max_error_codes8':neutral_error,
      'native_neutral_inverse_max_SDR_error_codes8':float(np.max(abs(recovered-x))),
      'native_RGB_4096_samples':{'p50':float(np.percentile(errors,50)),'p95':float(np.percentile(errors,95)),'p99':float(np.percentile(errors,99)),'max_channel_error_codes8':max(errors),'scope':'Working BT601-full matrix; clipping/extrapolation and 17-grid approximation, not measured ISP error'},
      'identity_random_YCC_max_error':identity_error,'native_Gamma_original_ARM_cases':native,'tables':tables,
      'not_proven':['Sensor-linear video Gamma consumption','Added highlight range','Same-boot camera consumption before user test','Samsung Log colour/metadata compatibility'],
      'runtime_code_changed':False,'camera_hardware_tested':False}
    (a.out/'curve-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    import matplotlib;matplotlib.use('Agg');import matplotlib.pyplot as plt
    fig,axs=plt.subplots(1,2,figsize=(10,3.8),layout='constrained')
    axs[0].plot(x,x,label='Neutral reference');axs[0].plot(x,forward(x/255)*255,label='SDR-to-Log test');axs[0].plot(x,neutral,'--',label='Native 17-grid interpolation')
    axs[0].set(xlabel='Input SDR code (sRGB hypothesis)',ylabel='Output code, 8 bit',title='Same-boot reload fixtures',xlim=(0,255),ylim=(0,255));axs[0].legend(fontsize=8)
    axs[1].plot(x,neutral-forward(x/255)*255);axs[1].set(xlabel='Input SDR code',ylabel='Native minus ideal, codes',title='Neutral-axis interpolation error')
    for ax in axs:ax.grid(alpha=.2)
    fig.savefig(a.out/'curve-and-error.png',dpi=160);plt.close(fig)
    with (a.out/'transfer-4097.csv').open('w',encoding='ascii',newline='\n') as f:
        f.write('input_sdr,assumed_linear,log_encoded,inverse_sdr\n')
        for v in np.linspace(0,1,4097):f.write(f'{v:.10f},{float(srgb_decode(v)):.10f},{float(forward(v)):.10f},{float(inverse(forward(v))):.10f}\n')
    print(json.dumps({k:result[k] for k in ['passed','post_LUT_black_white_codes8','native_neutral_max_error_codes8','native_RGB_4096_samples']},ensure_ascii=False))
if __name__=='__main__':main()
